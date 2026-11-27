#include "rt/compiler_binary.h"
#include "rt/module_link.h"

namespace nift::rt::detail {

namespace {

OpCode binary_opcode(Op op) {
    switch (op) {
        case Op::Add: return OpCode::Add;
        case Op::Sub: return OpCode::Sub;
        case Op::Mul: return OpCode::Mul;
        case Op::Div: return OpCode::Div;
        case Op::Mod: return OpCode::Mod;
        case Op::Eq: return OpCode::Eq;
        case Op::Ne: return OpCode::Ne;
        case Op::Lt: return OpCode::Lt;
        case Op::Le: return OpCode::Le;
        case Op::Gt: return OpCode::Gt;
        case Op::Ge: return OpCode::Ge;
        default: return OpCode::Nop;
    }
}
} // namespace

OpCode fused_not_jump(Op op) {
    switch (op) {
        case Op::Lt: return OpCode::JmpNotLt;
        case Op::Le: return OpCode::JmpNotLe;
        case Op::Gt: return OpCode::JmpNotGt;
        case Op::Ge: return OpCode::JmpNotGe;
        case Op::Eq: return OpCode::JmpNotEq;
        case Op::Ne: return OpCode::JmpNotNe;
        default: return OpCode::Nop;
    }
}

// Kinds an arithmetic operator accepts without a legacy type error (or a
// prepared-path rendering of aggregates in `+`).
namespace {

bool arithmetic_kinds_ok(Op op, KindSet l, KindSet r) {
    if ((l | r) & kKindUnknown) return false;  // may be anything: refused (review D1); probe: caller
    const auto only = [](KindSet k, KindSet allowed) { return (k & ~allowed) == 0; };
    if (op == Op::Add) {
        if (only(l, kKindNumber) && only(r, kKindNumber)) return true;
        // Concatenation: one side is a string, the other a scalar.
        if (only(l, kKindScalar) && only(r, kKindScalar)) {
            for (KindSet a = 1; a <= kKindObject; a <<= 1)
                for (KindSet b = 1; b <= kKindObject; b <<= 1)
                    if ((l & a) && (r & b) && !((a | b) & kKindString) && !((a & kKindNumber) && (b & kKindNumber)))
                        return false;
            return true;
        }
        return false;
    }
    return only(l, kKindNumber) && only(r, kKindNumber);
}

} // namespace

bool ordering_kinds_ok(KindSet l, KindSet r) {
    if ((l | r) & kKindUnknown) return false;  // may be anything: refused (review D1)
    return ((l & ~kKindNumber) == 0 && (r & ~kKindNumber) == 0) || ((l & ~kKindString) == 0 && (r & ~kKindString) == 0);
}


bool UnitCompiler::binary(const Node& node, std::uint16_t dst) {
    const OpCode op = binary_opcode(node.op);
    if (op == OpCode::Nop) return fail(node, "binary-operator");
    if (!infix_left_ok(node) || !comparison_chain_ok(node)) return false;
    if (const int rejected = literal_ordering_error(node, node, dst); rejected >= 0)
        return rejected != 0;
    KindSet l = kinds(node.a), r = kinds(node.b);
    // CP23: use the kind of an atomic handle's contents.
    if (l & kKindAtomicInt) l |= kKindInt;
    if (r & kKindAtomicInt) r |= kKindInt;
    l &= ~kKindAtomic;
    r &= ~kKindAtomic;
    bool numeric_guard = false, bad_kind = false, order_check = false, add_guard = false;
    switch (op) {
        case OpCode::Add: case OpCode::Sub: case OpCode::Mul: case OpCode::Div: case OpCode::Mod:
            // Legacy's string + callable is a run-time error with a fixed
            // message; compile it and let concatenate report it (handles_lambda_string).
            if (op == OpCode::Add && ((l & kKindFunction) || (r & kKindFunction)) &&
                ((l & kKindString) || (r & kKindString)))
                break;
            if (!arithmetic_kinds_ok(node.op, l, r) && !unknown_defers(l | r)) {
                // An unproved operand is legacy's run-time dispatch: numbers
                // (and string/array/bytes concatenation for `+`), else its
                // reached arithmetic error.
                const bool unproved = ((l | r) & (kKindUnknown | kKindNull)) != 0 || l == 0 || r == 0;
                const bool package_scope = plan_.linked && plan_.linked->locate(node.begin).module > 0;
                const bool array_add = op == OpCode::Add && l == kKindArray && r == kKindArray && package_scope;
                add_guard = op == OpCode::Add && (unproved || array_add) && package_scope;
                const bool dynamic_arith = op != OpCode::Add && unproved && package_scope;
                const auto widened = [](KindSet k) { return (k & kKindNumber) && (k & ~kKindNumber); };
                numeric_guard = session_ && op != OpCode::Add && !((l | r) & kKindUnknown) &&
                    (widened(l) || widened(r) || node.begin < session_->unit_begin);
                bad_kind = !numeric_guard && !add_guard && !dynamic_arith;
            }
            break;
        case OpCode::Lt: case OpCode::Le: case OpCode::Gt: case OpCode::Ge:
            bad_kind = !ordering_kinds_ok(l, r) && !unknown_defers(l | r);
            order_check = bad_kind && order_guarded(node);
            bad_kind = bad_kind && !order_check;
            break;
        default: break;
    }
    if (bad_kind && !numeric_diagnostic_tree(node)) return fail(node, op_type_reason(node.op));
    const std::uint16_t mark = fb_->top();
    const auto numeric_flags = fb_->flags();
    std::uint16_t left = 0, right = 0;
    // D2 family A: operands are evaluated once, left to right; a left operand in its
    // binding's own register is retained when the right operand can rebind it.
#if defined(NIFT_D2_MUTANT) && NIFT_D2_MUTANT == 1
    if (!expr_any(node.a, left) || !expr_any(node.b, right)) return false;
#else
    if (!expr_any(node.a, left) || !retain_receiver(left, left < mark, node.b) || !expr_any(node.b, right))
        return false;
#endif
    if (bad_kind) return fail(node, op_type_reason(node.op));
    if (order_check && !order_guard(node, left, right)) return false;
    fb_->set_offset(site(node));
#if !defined(NIFT_SESSION_ADMISSION_MUTANT) || NIFT_SESSION_ADMISSION_MUTANT != 9
    if (numeric_guard) {
        const auto saved_flags = fb_->flags();
        fb_->set_flags(numeric_flags);
        fb_->emit(OpCode::SessionNumeric, 0, left, right);
        fb_->set_flags(saved_flags);
    }
#endif
    fb_->emit(op, dst, left, right);
    fb_->release_to(mark);
    return true;
}


} // namespace nift::rt::detail
