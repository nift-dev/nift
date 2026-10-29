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
