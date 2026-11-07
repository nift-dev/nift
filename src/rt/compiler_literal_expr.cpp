#include "rt/compiler_literal_expr.h"

#include "rt/literal.h"

namespace nift::rt::detail {

bool UnitCompiler::has_call(NodeId id) const {
    if (id == kNoNode) return false;
    const Node& n = ast_.at(id);
    if (n.kind == NodeKind::Call) return true;
    if (has_call(n.a) || has_call(n.b) || has_call(n.c)) return true;
    const NodeId* list = ast_.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k)
        if (has_call(list[k])) return true;
    return false;
}

// H-A (combined-b3-b5.md): can evaluating `id` rebind a binding? A call can write any
// top-level binding (main's registers are the globals' slots) or a captured cell; an
// assignment, compound assignment, increment or await can write a local directly. A
// receiver held in a binding's own register is copied to a temporary before such an
// operand, so the operation uses the receiver as it was when it was evaluated.
bool UnitCompiler::may_rebind(NodeId id) const {
#if defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 14
    (void)id;
    return false;  // M14 (H-A): a receiver in its binding's register is never copied (read late)
#endif
    if (id == kNoNode) return false;
    const Node& n = ast_.at(id);
    if (n.kind == NodeKind::Call || n.kind == NodeKind::Assign || n.kind == NodeKind::CompoundAssign ||
        n.kind == NodeKind::IncDec || n.kind == NodeKind::Await)
        return true;
    if (may_rebind(n.a) || may_rebind(n.b) || may_rebind(n.c)) return true;
    const NodeId* list = ast_.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k)
        if (may_rebind(list[k])) return true;
    return false;
}

// H-A: `reg` holds a receiver; when it is the binding's own register (`own_register`: not a
// temporary the read loaded) and `operand`, evaluated next, can rebind the binding, the receiver
// is copied to a temporary (a counted reference) and `reg` names the copy. A receiver read into a
// temporary is already retained.
bool UnitCompiler::retain_receiver(std::uint16_t& reg, bool own_register, NodeId operand) {
    if (!own_register || !may_rebind(operand)) return true;
    std::uint16_t retained = 0;
    if (!temp(retained)) return false;
    fb_->emit(OpCode::Move, retained, reg);
    reg = retained;
    return true;
}

// Literal (or negated numeric literal) value of `node`.
bool UnitCompiler::constant_of(const Node& node, Value& value, bool& is_constant) {
    is_constant = true;
    switch (node.kind) {
        case NodeKind::Number:
            if (!decode_number_literal(text(node), value)) return fail(node, "number-literal");
            return true;
        case NodeKind::String:
            if (node.count != 0) { is_constant = false; return true; }  // interpolation lowering
            if (!decode_string_literal(text(node), value)) return fail(node, "string-literal");
            return true;
        case NodeKind::True: case NodeKind::False: value = Value::boolean(node.kind == NodeKind::True); return true;
        case NodeKind::Null: value = Value::null(); return true;
        case NodeKind::Unary: {
            const Node& operand = ast_.at(node.a);
            if (node.op != Op::Neg || operand.kind != NodeKind::Number) break;
            switch (decode_negative_number_literal(text(operand), value)) {
                case NegativeLiteral::Decoded: return true;
                case NegativeLiteral::Zero: break;  // -0 stays a run-time negation
                case NegativeLiteral::Invalid: return fail(operand, "number-literal");
            }
            break;
        }
        default: break;
    }
    is_constant = false;
    return true;
}

}  // namespace nift::rt::detail
