#include "rt/alias.h"

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt {

namespace {

struct Analysis {
    const Ast& ast;
    UnitPlan& plan;
    bool changed = false;

    bool is_name(NodeId id, std::string_view name) const {
        if (id == kNoNode) return false;
        const Node& n = ast.at(id);
        return n.kind == NodeKind::Ident && ast.text(n) == name;  // rt-lint: allow-string-compare compile time
    }

    bool pure_method(const Node& member) const {
        const std::string_view m = ast.text(ast.at(member.b));
        return m == "length" || m == "size";  // rt-lint: allow-string-compare compile time
    }

    // Does the subtree `id` use `name` in an escaping position?
    bool escapes(NodeId id, std::string_view name) const {
        if (id == kNoNode) return false;
        const Node& n = ast.at(id);
        switch (n.kind) {
            case NodeKind::Assign: case NodeKind::CompoundAssign: case NodeKind::IncDec: {
                const Node& target = ast.at(n.a);
                if (is_name(n.a, name)) return true;
                if ((target.kind == NodeKind::Index || target.kind == NodeKind::Member) && is_name(target.a, name))
                    return true;
                break;
            }
            case NodeKind::Call: {
                const Node& callee = ast.at(n.a);
                if (callee.kind == NodeKind::Member && is_name(callee.a, name) && !pure_method(callee)) return true;
                if (callee.kind == NodeKind::Ident) {
                    const auto fn = plan.index.find(ast.text(callee));
                    const NodeId* args = ast.list(n);
                    for (std::uint32_t k = 0; fn != plan.index.end() && k < n.count; ++k) {
                        const auto& params = plan.escaping[fn->second - 1u];
                        if (k < params.size() && params[k] && is_name(args[k], name)) return true;
                    }
                }
                break;
            }
            default: break;
        }
        if (escapes(n.a, name) || escapes(n.b, name) || escapes(n.c, name)) return true;
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k)
            if (escapes(list[k], name)) return true;
        return false;
    }
};

} // namespace

namespace {

// Does any call to a user function in the subtree pass a place argument?
bool has_place_call(const Ast& ast, const UnitPlan& plan, NodeId id) {
    if (id == kNoNode) return false;
    const Node& n = ast.at(id);
    if (n.kind == NodeKind::Call && ast.at(n.a).kind == NodeKind::Ident && plan.index.count(ast.text(ast.at(n.a)))) {
        const NodeId* args = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k)
            if (is_place_argument(ast, args[k])) return true;
    }
    if (has_place_call(ast, plan, n.a) || has_place_call(ast, plan, n.b) || has_place_call(ast, plan, n.c)) return true;
    const NodeId* list = ast.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k)
        if (has_place_call(ast, plan, list[k])) return true;
    return false;
}

} // namespace

void plan_escaping_params(const Ast& ast, NodeId root, UnitPlan& plan) {
    // Only needed (and only computed) when some call passes a place.
    if (plan.functions.empty() || !has_place_call(ast, plan, root)) return;
    plan.escaping.assign(plan.functions.size(), {});
    for (std::size_t f = 0; f < plan.functions.size(); ++f)
        plan.escaping[f].assign(ast.at(plan.functions[f]).count, false);
    Analysis analysis{ast, plan};
    do {
        analysis.changed = false;
        for (std::size_t f = 0; f < plan.functions.size(); ++f) {
            const Node& fn = ast.at(plan.functions[f]);
            const NodeId* params = ast.list(fn);
            for (std::uint32_t k = 0; k < fn.count; ++k) {
                if (plan.escaping[f][k] || !analysis.escapes(fn.b, ast.text(ast.at(params[k])))) continue;
                plan.escaping[f][k] = true;
                analysis.changed = true;
            }
        }
    } while (analysis.changed);
}

bool is_place_argument(const Ast& ast, NodeId node) {
    const NodeKind kind = ast.at(node).kind;
    return kind == NodeKind::Member || kind == NodeKind::Index || kind == NodeKind::SafeMember ||
           kind == NodeKind::SafeIndex;
}

} // namespace nift::rt
