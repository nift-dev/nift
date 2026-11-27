#include "rt/prepared_outer.h"

#include "rt/module_data.h"
#include "rt/source_scan.h"
#include "rt/text_type.h"
#include "rt/vm_mutants.h"

#include <unordered_map>
#include <vector>

namespace nift::rt {

namespace {

// A declaration `name := text` legacy types int: int text, or untyped text
// whose value is certainly an integral number (legacy's value type is then
// int; an integral double such as 1e2 or 2.0 included, probes k_*.f).
bool int_typed(const Ast& ast, const Node& n, const UnitKinds* kinds) {
    const std::string_view src = ast.source();
    const std::uint32_t op = detail::find_in(src, ":=", n.begin, n.end);
    if (op == n.end) return false;
    const TextType t = infer_text_type(detail::trim(src.substr(op + 2, n.end - op - 2)));
    if (t.kind != TextType::Kind::Known) return false;
    bool ok = t.known == 2 || (t.known == -1 && kinds && n.b != kNoNode && kinds->of(n.b) == kKindInt);
    MUTATE_OUTER_DOUBLE(ok, t.known);
    return ok;
}

} // namespace

bool outer_bindings_int_typed(const Ast& ast, NodeId root, const UnitPlan& plan, std::size_t f,
                              std::string_view name, const UnitKinds* kinds) {
    const NodeId own = f < plan.functions.size() ? plan.functions[f] : kNoNode;
    std::vector<NodeId> stack{root};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode || id == own) continue;
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::Param && ast.text(n) == name) return false;  // rt-lint: allow-string-compare
        if (n.kind == NodeKind::Declare && n.a != kNoNode && ast.at(n.a).kind == NodeKind::Ident &&
            ast.text(ast.at(n.a)) == name && !int_typed(ast, n, kinds))  // rt-lint: allow-string-compare
            return false;
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
    }
    return true;
}

// The expression parameter is local to the new callable. A spelling
// confined to that parameter and its read cannot influence the prepared
// declaration through another scope.
// A unique local with an int declaration cannot acquire an ambient text
// type at preparation time. Both engines then bind the integral value as int.
bool prepared_module_integer_copy(const Ast& ast, const Node& declaration, const UnitPlan& plan,
                                  const UnitKinds* kinds) {
    if (!kinds || declaration.b == kNoNode || !imported_module_data(plan, declaration))
        return false;
    const auto& source = ast.at(declaration.b);
    if (source.kind != NodeKind::Ident || kinds->of(declaration.b) != kKindInt) return false;
    const auto resolution = kinds->bindings.find(declaration.b);
    if (resolution == kinds->bindings.end()) return false;
    const auto name = ast.text(source);
    const auto same_name = [&](NodeId id) {
        return id != kNoNode && ast.at(id).kind == NodeKind::Ident &&
               ast.text(ast.at(id)) == name;  // rt-lint: allow-string-compare
    };
    const Node* origin = nullptr;
    for (NodeId id = 0; id < ast.size(); ++id) {
        const auto& n = ast.at(id);
        if ((n.kind == NodeKind::Param && ast.text(n) == name) || n.kind == NodeKind::Command)
            return false;
        if (n.kind == NodeKind::Try && same_name(n.b)) return false;
        if (n.kind == NodeKind::StructField && same_name(n.a)) return false;
        if (n.kind == NodeKind::ForBinding)
            for (std::uint32_t k = 0; k < n.count; ++k)
                if (same_name(ast.list(n)[k])) return false;
        if (n.kind != NodeKind::Declare) continue;
        if (n.a == resolution->second) origin = &n;
        else if (same_name(n.a)) return false;
        else if (n.a != kNoNode && ast.at(n.a).kind != NodeKind::Ident) return false;
    }
    return origin && origin->begin < declaration.begin && kinds->of(origin->b) == kKindInt &&
           int_typed(ast, *origin, kinds);
}

std::unordered_set<NodeId> prepared_lambda_literals(const Ast& ast) {
    std::unordered_map<std::string_view, std::size_t> names;
    for (NodeId id = 0; id < ast.size(); ++id) {
        const Node& node = ast.at(id);
        if (node.kind == NodeKind::Ident || node.kind == NodeKind::Param) ++names[ast.text(node)];
    }
    std::unordered_set<NodeId> proven;
    for (NodeId id = 0; id < ast.size(); ++id) {
        const Node& lambda = ast.at(id);
        if (lambda.kind != NodeKind::Lambda || lambda.count != 1 ||
            (lambda.flags & (kFlagBlockBody | kFlagAsync))) continue;
        const Node& body = ast.at(lambda.b);
        if (body.kind != NodeKind::Binary || body.op != Op::Add || ast.at(body.a).kind != NodeKind::String ||
            ast.at(body.a).count != 0 || ast.at(body.b).kind != NodeKind::Ident) continue;
        const Node& parameter = ast.at(ast.list(lambda)[0]);
        const auto name = ast.text(parameter);
        if (!(parameter.flags & kFlagVariadic) && ast.text(ast.at(body.b)) == name && names.at(name) == 2)
            proven.insert(id);
    }
    return proven;
}

} // namespace nift::rt
