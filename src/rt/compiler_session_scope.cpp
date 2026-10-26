#include "rt/compiler_session_scope.h"
#include <algorithm>
#include <cctype>
// rt-lint: cold-file bounded method-local admission under D-038
namespace nift::rt::detail {
bool session_lambda_assignment_spelling(const Ast& ast, const Node& node) {
    const auto begin = ast.at(node.a).end, end = ast.at(node.b).begin;
    if (end <= begin) return false;
    const auto separator = ast.source().substr(begin, end - begin);
    const auto equal = separator.find('=');
    if (equal == std::string_view::npos || equal == 0 || equal + 1 == separator.size()) return false;
    const auto space = [](unsigned char c) { return std::isspace(c) != 0; };
    // The legacy textual lambda parser treats an unseparated assignment head
    // as part of the parameter list. Admit only the proven separated spelling.
    return std::all_of(separator.begin(), separator.begin() + equal, space) &&
        std::all_of(separator.begin() + equal + 1, separator.end(), space);
}
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
