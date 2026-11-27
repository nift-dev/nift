#include "rt/compiler_compound.h"

// D2 family B (owner rulings 03:27/03:29): a compound assignment on a variable
// (`x op= g()`, `s += k()`) reads its target before the right-hand side, once,
// left to right, in every context (sessions included). Three things stay
// separate: the captured operand (the snapshot `t`), the binding as the rhs
// left it, and the final write-back. A fatal failure before the commit
// (overflow, the binding-type check, a string-append memory fault) leaves the
// binding as the rhs left it: no rollback to the snapshot, no partial commit.
//
// Taken only when the rhs can rebind (may_rebind); otherwise compound_op's
// in-place path (linear `s += t`) runs unchanged. On this path the operation
// runs on the snapshot, which the binding may still share; a string `+=` whose
// binding the rhs left unchanged appends in place (2b), so that path stays
// linear (cost: docs/runtime-2/checkpoints/combined-b3-b5.md section 27).

namespace nift::rt::detail {

bool UnitCompiler::compound_snapshot(const Node& node, const Binding& b) {
    const Node& target = ast_.at(node.a);
    const std::string_view name = text(target);
    const std::uint16_t mark = fb_->top();
    const bool indirect = b.cell || b.upvalue;
    std::uint16_t t = 0, value = 0, binding_type = b.type_reg;
    // 1. The snapshot.
    if (indirect) {
        if (!read_binding(b, t)) return false;
    } else if (b.global) {
        if (loop_reachable()) {
            fb_->set_offset(node.begin);
            global_check_defined(b.reg, name);
        }
        if (!global_read_value(b.reg, t, name)) return false;
    } else {
#if defined(NIFT_D2_MUTANT) && NIFT_D2_MUTANT == 4
        t = b.reg;  // D2-4: an own register's snapshot is the register itself (read late again)
#else
        if (!temp(t)) return false;
        fb_->emit(OpCode::Move, t, b.reg);
#endif
    }
    // 2. The rhs (it may rebind the binding; the snapshot is not affected).
    if (!expr_any(node.b, value)) return false;
#if defined(NIFT_D2_MUTANT) && NIFT_D2_MUTANT == 5
    // D2-5: the number operation runs through the restore form (the snapshot put back, then
    // the in-place path): a fatal operation rolls the binding back to the snapshot.
    if (((kinds(node.a) | kinds(node.b)) & ~kKindNumber) == 0) {
        std::uint16_t untyped = 0;
        if (indirect) {
            if (!fb_->constant(Value::integer(-1), untyped) || !store_binding(b, t, untyped)) return false;
        } else if (b.global) {
            fb_->emit(OpCode::SetGlobal, b.reg, t);
        } else {
            fb_->emit(OpCode::Move, b.reg, t);
        }
    }
#endif
    // 2b. A string `+=` whose binding still holds a value equal to the snapshot (the rhs left it
    // unchanged) appends in place on the binding, exactly as compound_op does: the operands are
    // equal, so the result is the same, and `s += f(x)` loops stay linear (the binding's string is
    // unshared once the snapshot is dropped). Cells keep the snapshot path (never in place).
    std::size_t done = 0;
    std::uint16_t current = b.reg;
#if defined(NIFT_D2_MUTANT) && NIFT_D2_MUTANT == 8
    const bool unchanged_path = false;  // D2-8: every such append copies (s += f(i) is quadratic)
#else
    const bool unchanged_path = true;
#endif
    const bool strings = node.op == Op::AddAssign && kinds(node.a) == kKindString && kinds(node.b) == kKindString;
    if (unchanged_path && !indirect && strings) {
        std::uint16_t reg = b.reg, type = b.type_reg;
        if (b.global && !global_read(b.reg, current)) return false;
        fb_->set_offset(node.begin);
#if defined(NIFT_D2_MUTANT) && NIFT_D2_MUTANT == 7
        const std::size_t changed = 0;  // D2-7: the in-place append without the unchanged-binding check
#else
        const std::size_t changed = fb_->emit(OpCode::JmpNotEq, t, current);
#endif
        fb_->emit(OpCode::LoadNull, t);
        if (current != b.reg) fb_->emit(OpCode::LoadNull, current);
        if (b.global && (!global_take(b, reg, name) || !global_read(b.type_reg, type))) return false;
        fb_->set_offset(node.begin);
        fb_->emit(OpCode::AddAssign, reg, value);
        fb_->note(fb_->emit(OpCode::CheckType, reg, type), std::string(name));
        if (b.global) global_put(b, reg);
        done = fb_->emit(OpCode::Jmp);
        if (changed != 0 && !fb_->patch(changed, fb_->here())) return builder_ok();
    }
    // 3. The operation, on the snapshot (its errors at the statement, as before).
    fb_->set_offset(node.begin);
    switch (node.op) {
        case Op::AddAssign: fb_->emit(OpCode::AddAssign, t, value); break;
        case Op::SubAssign: fb_->emit(OpCode::Sub, t, t, value); break;
        case Op::MulAssign: fb_->emit(OpCode::Mul, t, t, value); break;
        case Op::DivAssign: fb_->emit(OpCode::Div, t, t, value); break;
        case Op::ModAssign: fb_->emit(OpCode::Mod, t, t, value); break;
        default: return fail(node, "compound-operator");
    }
    if (loop_type_error_ambiguous() && binding_error_possible(b, kinds(node.a))) return fail(node, "context:assignment-error");
    // 4. The binding-type check, then the write-back (nothing is written before it).
    if (indirect) {
        std::uint16_t untyped = 0;
        if (!fb_->constant(Value::integer(-1), untyped)) return builder_ok();
        if (!store_binding(b, t, untyped)) return false;
        fb_->note(fb_->here() - 1u, std::string(name));
    } else {
        if (b.global && !global_read(b.type_reg, binding_type)) return false;  // after the rhs, as before
        fb_->note(fb_->emit(OpCode::CheckType, t, binding_type), std::string(name));
        if (b.global) fb_->emit(OpCode::SetGlobal, b.reg, t);  // a plain put: nothing was taken
        else if (t != b.reg) fb_->emit(OpCode::Move, b.reg, t);
    }
    // The snapshot's reference is dropped, so the binding's string is unshared again and the next
    // `s += t` appends in place without a first copy.
    if (done != 0 && !fb_->patch(done, fb_->here())) return builder_ok();
    if (t >= mark) fb_->emit(OpCode::LoadNull, t);
    if (current != b.reg) fb_->emit(OpCode::LoadNull, current);
    fb_->release_to(mark);
    return builder_ok();
}

} // namespace nift::rt::detail
