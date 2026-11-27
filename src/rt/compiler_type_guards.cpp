// rt-lint: cold-file type proofs run at compile time; no runtime string comparisons here
#include "rt/compiler_type_guards.h"

namespace nift::rt::detail {

KindSet type_label_kind(std::string_view spelling);

// `type(x) != "<label>"` whose body exits: after the if, x has that label.
bool UnitCompiler::type_call_guard(const Node& condition, NodeId& source, KindSet& kind) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines) return false;
    // `type(x) != "<label>" || …` with an exiting body: after the if, that
    // disjunct is false, so x has the label.
    if (condition.kind == NodeKind::Logical && condition.op == Op::Or)
        return type_call_guard(ast_.at(condition.a), source, kind) ||
               type_call_guard(ast_.at(condition.b), source, kind);
    if (condition.kind != NodeKind::Binary || condition.op != Op::Ne) return false;
    const Node& call = ast_.at(condition.a);
    const Node& label = ast_.at(condition.b);
    if (call.kind != NodeKind::Call || call.count != 1 || label.kind != NodeKind::String) return false;
    const Node& callee = ast_.at(call.a);
    if (callee.kind != NodeKind::Ident || text(callee) != "type" || find_binding("type") ||
        plan_.index.count("type") || plan_.struct_index.count("type")) return false;
    const int owner = current_struct();
    if (owner >= 0 && plan_.structs[static_cast<std::size_t>(owner)].method("type")) return false;
    const KindSet narrowed = type_label_kind(text(label));
    if (narrowed == 0) return false;
    const Node& input = ast_.at(ast_.list(call)[0]);
    if (input.kind != NodeKind::Ident) return false;
    const Binding* binding = find_binding(text(input));
    if (!binding || binding->cell || binding->global || binding->decl_node == kNoNode ||
        assigned_.count(text(input))) return false;
    source = binding->decl_node;
    kind = narrowed;
    return true;
}

// A conjunction `… && type(x) == "<label>" && …`: the true branch and the
// rest of the conjunction see x as that label.
bool UnitCompiler::type_call_positive_guard(const Node& condition, NodeId& source, KindSet& kind) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines) return false;
    const Node& n = condition;
    if (n.kind == NodeKind::Logical && n.op == Op::And)
        return type_call_positive_guard(ast_.at(n.a), source, kind) ||
               type_call_positive_guard(ast_.at(n.b), source, kind);
    if (n.kind != NodeKind::Binary || n.op != Op::Eq) return false;
    NodeId call_id = n.a, label_id = n.b;
    if (ast_.at(call_id).kind != NodeKind::Call) std::swap(call_id, label_id);
    const Node& call = ast_.at(call_id);
    const Node& label = ast_.at(label_id);
    if (call.kind != NodeKind::Call || call.count != 1 || label.kind != NodeKind::String) return false;
    const Node& callee = ast_.at(call.a);
    if (callee.kind != NodeKind::Ident || text(callee) != "type" || find_binding("type") ||
        plan_.index.count("type") || plan_.struct_index.count("type")) return false;
    const int owner = current_struct();
    if (owner >= 0 && plan_.structs[static_cast<std::size_t>(owner)].method("type")) return false;
    const KindSet narrowed = type_label_kind(text(label));
    if (narrowed == 0) return false;
    const Node& input = ast_.at(ast_.list(call)[0]);
    if (input.kind != NodeKind::Ident) return false;
    const Binding* binding = find_binding(text(input));
    if (!binding || binding->cell || binding->global || binding->decl_node == kNoNode ||
        assigned_.count(text(input))) return false;
    source = binding->decl_node;
    kind = narrowed;
    return true;
}

bool UnitCompiler::block_exits(const Node& body) const {
    if (body.kind == NodeKind::Return || body.kind == NodeKind::Throw ||
        body.kind == NodeKind::Break || body.kind == NodeKind::Continue)
        return true;
    if (body.count == 0) return false;
    const Node& last = ast_.at(ast_.list(body)[body.count - 1]);
    return last.kind == NodeKind::Return || last.kind == NodeKind::Throw ||
           last.kind == NodeKind::Break || last.kind == NodeKind::Continue;
}

// `x != null` / `null != x`: the true branch sees a non-null value. The
// remaining kinds are unknown when the only proved kind was null.
bool UnitCompiler::null_test_guard(const Node& condition, NodeId& source, KindSet& kind) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines) return false;
    if (condition.kind != NodeKind::Binary || condition.op != Op::Ne) return false;
    NodeId ident = kNoNode;
    if (ast_.at(condition.b).kind == NodeKind::Null && ast_.at(condition.a).kind == NodeKind::Ident)
        ident = condition.a;
    else if (ast_.at(condition.a).kind == NodeKind::Null && ast_.at(condition.b).kind == NodeKind::Ident)
        ident = condition.b;
    if (ident == kNoNode) return false;
    const Binding* binding = find_binding(text(ast_.at(ident)));
    if (!binding || binding->cell || binding->global || binding->decl_node == kNoNode ||
        assigned_.count(text(ast_.at(ident)))) return false;
    const KindSet raw = kinds_.of(ident);
    if (raw & kKindUnknown) return false;  // still unproved: keep the generic dispatch
    const KindSet remaining = static_cast<KindSet>(raw & ~kKindNull);
    if (remaining == 0) return false;  // only null was proved: no useful narrowing
    source = binding->decl_node;
    kind = remaining;
    return true;
}

KindSet type_label_kind(std::string_view spelling) {
    if (spelling == R"("null")") return kKindNull;
    if (spelling == R"("bool")") return kKindBool;
    if (spelling == R"("int")") return kKindInt;
    if (spelling == R"("float")") return kKindFrac;
    if (spelling == R"("number")") return kKindNumber;
    if (spelling == R"("string")") return kKindString;
    if (spelling == R"("array")") return kKindArray;
    if (spelling == R"("object")") return kKindObject;
    if (spelling == R"("bytes")") return kKindBytes;
    return 0;
}

// `t == "bytes"` where `t := type(x)` and both are stable: the true branch
// sees x as that kind. This is legacy's type-dispatch idiom.
bool UnitCompiler::type_test_guard(const Node& condition, NodeId& source, KindSet& kind) const {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines) return false;
    if (condition.kind != NodeKind::Binary || condition.op != Op::Eq) return false;
    const Node& label = ast_.at(condition.b);
    const Node& name = ast_.at(condition.a);
    if (label.kind != NodeKind::String || name.kind != NodeKind::Ident) return false;
    const KindSet narrowed = type_label_kind(text(label));
    if (narrowed == 0) return false;
    const Binding* alias = find_binding(text(name));
    if (!alias || alias->cell || alias->global || alias->decl_node == kNoNode ||
        assigned_.count(text(name))) return false;
    const Node* initializer = nullptr;
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& n = ast_.at(id);
        if (n.kind == NodeKind::Declare && n.a == alias->decl_node) { initializer = &ast_.at(n.b); break; }
    }
    if (!initializer || initializer->kind != NodeKind::Call || initializer->count != 1) return false;
    const Node& test = ast_.at(initializer->a);
    if (test.kind != NodeKind::Ident || text(test) != "type" || find_binding("type") ||
        plan_.index.count("type") || plan_.struct_index.count("type")) return false;
    const int owner = current_struct();
    if (owner >= 0 && plan_.structs[static_cast<std::size_t>(owner)].method("type")) return false;
    const Node& input = ast_.at(ast_.list(*initializer)[0]);
    if (input.kind != NodeKind::Ident) return false;
    const Binding* target = find_binding(text(input));
    if (!target || target->cell || target->global || target->decl_node == kNoNode ||
        assigned_.count(text(input))) return false;
    const auto original = kinds_.bindings.find(ast_.list(*initializer)[0]);
    if (original == kinds_.bindings.end() || original->second != target->decl_node) return false;
    source = target->decl_node;
    kind = narrowed;
    return true;
}


} // namespace nift::rt::detail
