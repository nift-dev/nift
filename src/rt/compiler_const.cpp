#include "rt/compiler_const.h"

namespace nift::rt::detail {

// CP25: a write to a `const`/`immut` scalar binding raises legacy's fatal
// Error at the statement. A plain assignment raises before its value. For a
// compound assignment or increment/decrement (`update`: the binding), legacy
// evaluates the value and computes the operation first, so its errors
// (division or modulo by zero, operand types) come before the const Error;
// the operation runs on a copy and is never stored.
//
// D-058 (owner, 2026-10-30): the same order in every context, loop-evaluator
// code included (D-009 #2: const is immutable everywhere, also when declared
// in a loop; D-009 #5: the Error is reported at the write). Approved
// differences from legacy's loop evaluator: it never enforces a const
// declared there, evaluates a plain assignment's value before rejecting an
// outer const, rejects a compound assignment to an outer const before its
// value, and reports a write in a loop-called function at the call site.
bool UnitCompiler::const_write(const Node& node, const Node* value, const std::uint16_t* result,
                               const Binding& b, bool update) {
    const std::uint16_t mark = fb_->top();
    std::uint16_t reg = 0;
    if (!temp(reg)) return false;
    if (value != nullptr && !expr_to(node_id(*value), reg)) return false;
    const Node& target = ast_.at(node.a);
    if (update) {
        std::uint16_t cur = 0;
        if (!temp(cur)) return false;
        bool lazy_temp = false;
        if (b.lazy_for) {
            if (!for_read_source(b, cur, lazy_temp)) return false;
        } else if (b.global && !b.cell) {
            if (!global_read_value(b.reg, cur, text(target))) return false;
        } else if (!read_binding(b, cur)) {
            return false;
        }
        if (cur == b.reg) {
            std::uint16_t copy = 0;
            if (!temp(copy)) return false;
            fb_->emit(OpCode::Move, copy, cur);
            cur = copy;
        }
        fb_->set_offset(node.begin);
        if (node.kind == NodeKind::IncDec) {
            const bool up = node.op == Op::PreInc || node.op == Op::PostInc;
            fb_->emit(OpCode::AddI, cur, cur, jump_operand(up ? 1 : -1));
        } else {
            switch (node.op) {
                case Op::AddAssign: fb_->emit(OpCode::AddAssign, cur, reg); break;
                case Op::SubAssign: fb_->emit(OpCode::Sub, cur, cur, reg); break;
                case Op::MulAssign: fb_->emit(OpCode::Mul, cur, cur, reg); break;
                case Op::DivAssign: fb_->emit(OpCode::Div, cur, cur, reg); break;
                case Op::ModAssign: fb_->emit(OpCode::Mod, cur, cur, reg); break;
                default: return fail(node, "compound-operator");
            }
        }
    }
    if (!legacy_fatal(node, "cannot assign to const binding: " + std::string(text(target)), reg)) return false;
    if (result && *result != reg) fb_->emit(OpCode::Move, *result, reg);
    fb_->release_to(mark);
    return builder_ok();
}

} // namespace nift::rt::detail
