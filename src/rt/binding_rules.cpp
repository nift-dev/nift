#include "rt/binding_rules.h"

#include "rt/reserved_names.h"

#include <string_view>
#include <vector>

// rt-lint: cold-file runs once per unit before execution; never on a hot path

namespace nift::rt {

namespace {

struct Scoped {
    NodeId name;
    NodeId scope;
    const char* what;
};

// The first call `name(…)` whose callee lies inside `scope`, by position.
bool first_call(const Ast& ast, std::string_view name, const Node& scope, std::uint32_t& at) {
    bool found = false;
    for (NodeId id = 0; id < ast.size(); ++id) {
        const Node& n = ast.at(id);
        if (n.kind != NodeKind::Call || n.a == kNoNode) continue;
        const Node& callee = ast.at(n.a);
        if (callee.kind != NodeKind::Ident || callee.begin < scope.begin || callee.end > scope.end) continue;
        if (ast.text(callee) != name) continue;  // rt-lint: allow-string-compare cold path, once per unit
        if (!found || callee.begin < at) at = callee.begin;
        found = true;
    }
    return found;
}

}  // namespace

bool reserved_name_called_in(const Ast& ast, std::string_view name, const Node& scope) {
    std::uint32_t at = 0;
    return first_call(ast, name, scope, at);
}

BindingCallViolation reserved_binding_call(const Ast& ast) {
    std::vector<Scoped> bindings;
    for (NodeId id = 0; id < ast.size(); ++id) {
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::For && n.a != kNoNode && n.b != kNoNode) {
            const Node& header = ast.at(n.a);
            if (header.kind != NodeKind::ForBinding) continue;
            const NodeId* names = ast.list(header);
            for (std::uint32_t k = 0; k < header.count; ++k) bindings.push_back({names[k], n.b, "for-loop"});
        } else if (n.kind == NodeKind::Try && n.b != kNoNode && n.c != kNoNode) {
            bindings.push_back({n.b, n.c, "catch"});
        }
    }
    BindingCallViolation result;
    for (const Scoped& binding : bindings) {
        const Node& name = ast.at(binding.name);
        const std::string_view text = ast.text(name);
        std::uint32_t call = 0;
        if (!reserved_name(text) || !first_call(ast, text, ast.at(binding.scope), call)) continue;
        if (result.found && result.offset <= name.begin) continue;
        result.found = true;
        result.offset = name.begin;
        result.call = call;
        result.message = std::string(binding.what) + " binding '" + std::string(text) +
                         "' has the name of a built-in and is called in its scope; rename the binding";
    }
    return result;
}

}  // namespace nift::rt
