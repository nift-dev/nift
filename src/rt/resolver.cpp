#include "rt/resolver.h"

#include "rt/alias.h"
#include "rt/native.h"
#include "rt/structs.h"

namespace nift::rt {

bool Scopes::declare(std::string_view name, const Binding& binding) {
    auto& scope = scopes_.back();
    for (const auto& entry : scope)
        if (entry.first == name) return false;
    scope.emplace_back(name, binding);
    return true;
}

bool Scopes::update(std::string_view name, const Binding& binding) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
        for (auto& entry : *scope)
            if (entry.first == name) { entry.second = binding; return true; }
    return false;
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

// Direct calls by name of the unit's functions inside `node` (any depth).
void direct_callees(const Ast& ast, NodeId node, const UnitPlan& plan, std::vector<std::size_t>& out) {
    std::vector<NodeId> stack{node};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::Call && n.a != kNoNode && ast.at(n.a).kind == NodeKind::Ident) {
            const auto fn = plan.index.find(ast.text(ast.at(n.a)));
            if (fn != plan.index.end()) out.push_back(fn->second - 1u);
        }
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
    }
}

// CP12 phase B: legacy registers a function when its declaration statement
// runs, so a call that can run before then reaches no function (or another
// callable of that name). Calls are bound at compile time, which is exact
// when no top-level statement before a declaration can reach that function,
// directly or through other functions; a unit where one can is refused
// (`fn-called-before-declaration`, at the statement). Conservative: every
// call in the statement counts, executed or not.
bool check_declaration_order(const Ast& ast, NodeId root, UnitPlan& plan, const std::vector<std::uint32_t>& position,
                             const std::unordered_set<std::string>* existing) {
    const std::size_t n = plan.functions.size();
    std::vector<std::vector<std::size_t>> edges(n);
    for (std::size_t f = 0; f < n; ++f) direct_callees(ast, ast.at(plan.functions[f]).b, plan, edges[f]);
    const Node& program = ast.at(root);
    const NodeId* statements = ast.list(program);
    for (std::uint32_t k = 0; k < program.count; ++k) {
        if (ast.at(statements[k]).kind == NodeKind::FnDecl) continue;
        std::vector<std::size_t> work;
        direct_callees(ast, statements[k], plan, work);
        std::vector<char> seen(n, 0);
        while (!work.empty()) {
            const std::size_t f = work.back();
            work.pop_back();
            if (seen[f]) continue;
            seen[f] = 1;
            const auto name = ast.text(ast.at(ast.at(plan.functions[f]).a));
            if (position[f] > k && (!existing || !existing->count(std::string(name))))
                return reject(plan, ast.at(statements[k]), "fn-called-before-declaration");
            for (std::size_t g : edges[f]) work.push_back(g);
        }
    }
    return true;
}

} // namespace

UnitPlan plan_unit(const Ast& ast, NodeId root, const std::unordered_set<std::string>* existing_functions) {
    UnitPlan plan;
    const Node& program = ast.at(root);
    const NodeId* statements = ast.list(program);
    bool executable_seen = false, late = false;
    std::vector<std::uint32_t> position;  // statement index of each function's declaration
    for (std::uint32_t k = 0; k < program.count; ++k) {
        const Node& node = ast.at(statements[k]);
        if (node.kind != NodeKind::FnDecl) { executable_seen = true; continue; }
        late = late || executable_seen;
        if (!admit_function(ast, statements[k], plan)) return plan;
        position.push_back(k);
    }
    // Declarations after statements: admitted when no call can run first.
    if (late && !check_declaration_order(ast, root, plan, position, existing_functions)) return plan;
    plan.method_of.assign(plan.functions.size(), -1);
    if (!plan_structs(ast, root, plan)) return plan;  // CP12b: methods appended to functions
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
