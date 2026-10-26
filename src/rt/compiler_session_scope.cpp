#include "rt/compiler_session_scope.h"
// rt-lint: cold-file bounded method-local admission under D-038
namespace nift::rt::detail {
bool session_isolated_method_local(const Ast& ast, const UnitPlan& plan, const Node& local) {
    for (std::size_t f = 0; f < plan.functions.size(); ++f) {
        if (!plan.is_method(f)) continue;
        const auto& fn = ast.at(plan.functions[f]);
        if (local.begin < fn.begin || local.end > fn.end) continue;
        // The local has ordinary lexical meaning inside this method. Without
        // any call it cannot become a dynamic binding seen by another reader.
        // More involved call paths retain a conservative before-effects refusal.
        std::vector<NodeId> pending{fn.b};
        while (!pending.empty()) {
            const auto id = pending.back(); pending.pop_back();
            if (id == kNoNode) continue;
            const auto& node = ast.at(id);
            if (node.kind == NodeKind::Call) return false;
            for (auto child : {node.a, node.b, node.c}) pending.push_back(child);
            for (std::uint32_t k = 0; k < node.count; ++k) pending.push_back(ast.list(node)[k]);
        }
        return true;
    }
    return false;
}
} // namespace nift::rt::detail
