#include "rt/resolver.h"

#include "rt/alias.h"
#include "rt/native.h"

namespace nift::rt {

bool Scopes::declare(std::string_view name, const Binding& binding) {
    auto& scope = scopes_.back();
    for (const auto& entry : scope)
        if (entry.first == name) return false;
    scope.emplace_back(name, binding);
    return true;
}

const Binding* Scopes::find(std::string_view name) const {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
        for (auto entry = scope->rbegin(); entry != scope->rend(); ++entry)
            if (entry->first == name) return &entry->second;
    return nullptr;
}

namespace {

bool reject(UnitPlan& plan, const Node& node, std::string reason) {
    plan.ok = false;
    plan.reason = std::move(reason);
    plan.offset = node.begin;
    return false;
}

bool admit_function(const Ast& ast, NodeId id, UnitPlan& plan) {
    const Node& fn = ast.at(id);
    if (fn.flags & (kFlagAsync | kFlagDeferredBody | kFlagMethod)) return reject(plan, fn, "fn-flags");
    if (fn.a == kNoNode || fn.b == kNoNode) return reject(plan, fn, "fn-shape");
    const std::string_view name = ast.text(ast.at(fn.a));
    if (reserved_name(name)) return reject(plan, fn, "fn-name-reserved");
    if (plan.index.count(name)) return reject(plan, fn, "fn-redeclared");
    const NodeId* params = ast.list(fn);
    for (std::uint32_t k = 0; k < fn.count; ++k) {
        const Node& param = ast.at(params[k]);
        if (param.kind != NodeKind::Param || (param.flags & kFlagVariadic)) return reject(plan, param, "fn-param");
        if (reserved_name(ast.text(param))) return reject(plan, param, "param-name-reserved");
    }
    plan.functions.push_back(id);
    plan.index.emplace(name, static_cast<std::uint16_t>(plan.functions.size()));
    return true;
}

} // namespace

UnitPlan plan_unit(const Ast& ast, NodeId root) {
    UnitPlan plan;
    const Node& program = ast.at(root);
    const NodeId* statements = ast.list(program);
    bool executable_seen = false;
    for (std::uint32_t k = 0; k < program.count; ++k) {
        const Node& node = ast.at(statements[k]);
        if (node.kind != NodeKind::FnDecl) { executable_seen = true; continue; }
        // Legacy registers a function when its declaration runs; requiring
        // every declaration to precede the first executable statement makes
        // compile-time binding of calls exact.
        if (executable_seen) { reject(plan, node, "fn-after-statement"); return plan; }
        if (!admit_function(ast, statements[k], plan)) return plan;
    }
    if (plan.functions.size() >= 0x7fff) reject(plan, program, "too-many-functions");
    if (plan.ok) plan_escaping_params(ast, root, plan);
    return plan;
}

void collect_assigned(const Ast& ast, NodeId node, std::unordered_set<std::string_view>& names) {
    if (node == kNoNode) return;
    const Node& n = ast.at(node);
    if ((n.kind == NodeKind::Assign || n.kind == NodeKind::CompoundAssign || n.kind == NodeKind::IncDec) &&
        n.a != kNoNode && ast.at(n.a).kind == NodeKind::Ident)
        names.insert(ast.text(ast.at(n.a)));
    collect_assigned(ast, n.a, names);
    collect_assigned(ast, n.b, names);
    collect_assigned(ast, n.c, names);
    const NodeId* list = ast.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k) collect_assigned(ast, list[k], names);
}

} // namespace nift::rt
