#include "rt/compiler_atomic.h"

#include "rt/compiler_expr.h"

namespace nift::rt::detail {

bool UnitCompiler::atomic_update(const Node& node, const Binding& b, std::uint16_t dst, bool expression) {
    const Node& target = ast_.at(node.a);
    const std::uint16_t mark = fb_->top();
    std::uint16_t handle = 0;
    bool lazy_temp = false;
    if (b.lazy_for) {
        if (!for_read_source(b, handle, lazy_temp)) return false;
    } else if (b.global) {
        if (loop_reachable() && (fb_->set_offset(node.begin), !global_check_defined(b.reg, text(target))))
            return false;
        if (!global_read_value(b.reg, handle, text(target))) return false;
    } else if (!read_binding(b, handle)) {
        return false;
    }
    std::uint16_t operand = 0;
    if (node.kind == NodeKind::IncDec) {
        if (!temp(operand)) return false;
        const bool up = node.op == Op::PreInc || node.op == Op::PostInc;
        fb_->emit(OpCode::LoadI, operand, jump_operand(up ? 1 : -1));
    } else if (!expr_any(node.b, operand)) {
        return false;
    }
    std::uint8_t op = 0;
    switch (node.kind == NodeKind::IncDec ? Op::AddAssign : node.op) {
        case Op::AddAssign: op = 0; break;
        case Op::SubAssign: op = 1; break;
        case Op::ModAssign: op = 2; break;
        case Op::AndAssign: op = 3; break;
        case Op::OrAssign: op = 4; break;
        case Op::XorAssign: op = 5; break;
        case Op::MulAssign: op = 6; break;
        case Op::DivAssign: op = 7; break;
        default: return fail(node, "compound-operator");
    }
    std::uint8_t mode = 0;  // compound: the new value
    if (node.kind == NodeKind::IncDec) {
        const bool post = node.op == Op::PostInc || node.op == Op::PostDec;
        mode = post ? 2 : 1;
    }
    std::uint16_t out = dst;
    if (!expression && !temp(out)) return false;
    fb_->set_offset(site(node));
    const std::uint8_t saved = fb_->flags();
    fb_->set_flags(static_cast<std::uint8_t>(saved | static_cast<std::uint8_t>((mode << 4) | (op & 0x0f))));
    const std::size_t at = fb_->emit(OpCode::AtomicUpdate, out, handle, operand);
    fb_->set_flags(saved);
    fb_->note(at, std::string(text(node)));
    fb_->release_to(mark);
    return builder_ok();
}

}  // namespace nift::rt::detail
