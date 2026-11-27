#include "rt/compiler_incdec.h"

namespace nift::rt::detail {

bool UnitCompiler::inc_dec_expr(const Node& node, std::uint16_t dst) {
    const Node& target = ast_.at(node.a);
    if (target.kind != NodeKind::Ident) return fail(target, "incdec-target");
    const Binding* found = find_binding(text(target));
    if (!found || found->invocation) return fail(target, write_refusal(found, text(target), "assign-free-name"));
    if (found->const_array) return fail(target, "declare-const");
    if (found->readonly) return fail(target, "assign-catch-binding");
    const Binding b = *found;
    if (kinds(node.a) & kKindAtomic) return atomic_update(node, b, dst, true);
    if (!kinds_within(node.a, kKindNumber, node, "type:increment")) return false;
    // CP25: after the numeric check; a loop-code prefix is legacy's unapplied read (below).
    const bool loop_prefix = loop_reachable() && node.op != Op::PostInc && node.op != Op::PostDec;
    if (b.const_scalar && !loop_prefix) return const_write(node, nullptr, &dst, b, true);
    const std::uint16_t mark = fb_->top();
    std::uint16_t work = 0;
    if (!temp(work)) return false;
    if (b.global && !b.cell) {
        if (loop_reachable() && (fb_->set_offset(node.begin), !global_check_defined(b.reg, text(target)))) return false;
        if (!global_read_value(b.reg, work, text(target))) return false;
    } else if (!read_binding(b, work)) {
        return false;
    }
    if (work == b.reg) {
        std::uint16_t copy = 0;
        if (!temp(copy)) return false;
        fb_->emit(OpCode::Move, copy, work);
        work = copy;
    }
    const bool post = node.op == Op::PostInc || node.op == Op::PostDec;
    if (loop_reachable() && !post) {
        // Legacy's expression parser treats `++x`/`--x` on a loop location as
        // an unapplied prefix read, not a conventional increment/decrement.
        if (work != dst) fb_->emit(OpCode::Move, dst, work);
        fb_->release_to(mark);
        return builder_ok();
    }
    if (post) fb_->emit(OpCode::Move, dst, work);
    const bool up = node.op == Op::PreInc || node.op == Op::PostInc;
    fb_->emit(OpCode::AddI, work, work, jump_operand(up ? 1 : -1));
    std::uint16_t untyped = 0;
    if (!fb_->constant(Value::integer(-1), untyped)) return false;
    if (b.cell || b.upvalue) {
        if (!store_binding(b, work, untyped)) return false;
        fb_->note(fb_->here() - 1u, std::string(text(target)));
    } else {
        std::uint16_t binding_type = b.type_reg;
        if (b.global && !global_read(b.type_reg, binding_type)) return false;
        const std::size_t check = fb_->emit(OpCode::CheckType, work, binding_type);
        fb_->note(check, std::string(text(target)));
        if (b.global) global_put(b, work);
        else if (work != b.reg) fb_->emit(OpCode::Move, b.reg, work);
    }
    if (!post) fb_->emit(OpCode::Move, dst, work);
    fb_->release_to(mark);
    return builder_ok();
}

} // namespace nift::rt::detail
