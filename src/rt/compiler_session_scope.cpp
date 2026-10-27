#include "rt/compiler_session_scope.h"
#include <algorithm>
#include <cctype>
#include "rt/native.h"
// rt-lint: cold-file bounded method-local admission under D-038
namespace nift::rt::detail {
namespace {
struct BareFieldScan {
    const Ast& ast;
    std::string_view field;
    std::vector<bool> scopes;
    bool found = false;
    bool bound() const { return std::find(scopes.begin(), scopes.end(), true) != scopes.end(); }
    void bind(NodeId id) {
        if (id == kNoNode) return;
        const auto& n = ast.at(id);
        if ((n.kind == NodeKind::Ident || n.kind == NodeKind::Param) && ast.text(n) == field)
            scopes.back() = true;
        if (n.kind == NodeKind::Pattern) for (std::uint32_t k = 0; k < n.count; ++k) bind(ast.list(n)[k]);
    }
    void function(const Node& n) {
        scopes.push_back(false);
        for (std::uint32_t k = 0; k < n.count; ++k) bind(ast.list(n)[k]);
        node(n.b); scopes.pop_back();
    }
    void node(NodeId id) {
        if (id == kNoNode || found) return;
        const auto& n = ast.at(id);
        switch (n.kind) {
            case NodeKind::Ident: found = ast.text(n) == field && !bound(); return;
            case NodeKind::Param: return;
            case NodeKind::Member: case NodeKind::SafeMember: node(n.a); return;
            case NodeKind::Property: node(n.b); return;
            case NodeKind::Declare: node(n.b); bind(n.a); return;
            case NodeKind::FnDecl: case NodeKind::Lambda: function(n); return;
            case NodeKind::Block: {
                scopes.push_back(false);
                for (std::uint32_t k = 0; k < n.count; ++k) node(ast.list(n)[k]);
                scopes.pop_back(); return;
            }
            case NodeKind::For: {
                const auto& header = ast.at(n.a);
                node(header.a); scopes.push_back(field == "loop");
                for (std::uint32_t k = 0; k < header.count; ++k) bind(ast.list(header)[k]);
                node(n.b); scopes.pop_back(); return;
            }
            case NodeKind::Try:
                node(n.a); scopes.push_back(false); bind(n.b); node(n.c); scopes.pop_back(); return;
            default: break;
        }
        for (auto child : {n.a, n.b, n.c}) node(child);
        for (std::uint32_t k = 0; k < n.count; ++k) node(ast.list(n)[k]);
    }
};
} // namespace
bool session_method_uses_bare_field(const Ast& ast, NodeId schema, std::string_view field) {
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 2
    (void)ast; (void)schema; (void)field; return true;
#endif
    BareFieldScan scan{ast, field, {}, false};
    const auto& declaration = ast.at(schema);
    for (std::uint32_t k = 0; k < declaration.count; ++k) {
        const auto& member = ast.at(ast.list(declaration)[k]);
        if (member.kind == NodeKind::FnDecl) scan.function(member);
    }
    return scan.found;
}
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
        // any user call it cannot become a dynamic binding seen by another reader.
        // Native print cannot execute script; its argument calls are scanned too.
        std::vector<NodeId> pending{fn.b};
        while (!pending.empty()) {
            const auto id = pending.back(); pending.pop_back();
            if (id == kNoNode) continue;
            const auto& node = ast.at(id);
            if (node.kind == NodeKind::Call) {
                NativeInfo native{};
                if (node.a == kNoNode || ast.at(node.a).kind != NodeKind::Ident ||
                    !lookup_native(ast.text(ast.at(node.a)), native)) return false;
            }
            for (auto child : {node.a, node.b, node.c}) pending.push_back(child);
            for (std::uint32_t k = 0; k < node.count; ++k) pending.push_back(ast.list(node)[k]);
        }
        return true;
    }
    return false;
}
} // namespace nift::rt::detail
