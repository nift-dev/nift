#include "rt/compiler_handles.h"

#include "rt/builtin_bind.h"

#include <string>
#include <vector>

// CP17 W13: handle values are a forgery barrier. A handle may appear only as
// a local binding's value, a method receiver with a native bound for that
// handle family, or the argument of the free close(handle). Everything else
// (aggregates, calls, returns, captures, comparison, rendering, interpolation)
// is refused before execution with the same reason for every shape.
namespace nift::rt::detail {

namespace {

// Legacy file/stream write uses a literal argument's raw text (interpolation
// is not applied to that spelling), unlike an evaluated value.
bool contains_interpolated_literal(const Ast& ast, NodeId id) {
    if (id == kNoNode) return false;
    const Node& n = ast.at(id);
    if (n.kind == NodeKind::String && n.count != 0) return true;
    if (contains_interpolated_literal(ast, n.a) || contains_interpolated_literal(ast, n.b) ||
        contains_interpolated_literal(ast, n.c))
        return true;
    const NodeId* list = ast.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k)
        if (contains_interpolated_literal(ast, list[k])) return true;
    return false;
}

bool handle_position_allowed(const Ast& ast, NodeId id, const std::vector<NodeId>& parent,
                             const UnitKinds& kinds) {
    const NodeId p = id < parent.size() ? parent[id] : kNoNode;
    if (p == kNoNode) return false;
    const Node& pn = ast.at(p);
    // A local binding: `x := handle` / `x = handle` (and the target identifier
    // itself, whose inferred kinds include the handle kind).
    if ((pn.kind == NodeKind::Declare || pn.kind == NodeKind::Assign) && ast.at(pn.a).kind == NodeKind::Ident)
        return pn.a == id || pn.b == id;
    if (pn.kind == NodeKind::Member && pn.a == id) {
        const NodeId call = p < parent.size() ? parent[p] : kNoNode;
        if (call == kNoNode) return false;
        const Node& cn = ast.at(call);
        if (cn.kind != NodeKind::Call || cn.a != p) return false;
        const natives::BuiltinSpec* spec = nullptr;
        return bind_method(kinds.of(id), ast.text(ast.at(pn.b)), spec) == MethodBinding::Bound;
    }
    if (pn.kind == NodeKind::Call) {
        const Node& callee = ast.at(pn.a);
        const bool named = callee.kind == NodeKind::Ident;
        const bool closes = named && ast.text(callee) == "close";  // rt-lint: allow-string-compare compile time
        if (closes && pn.count >= 1) {
            const NodeId* args = ast.list(pn);
            if (args[0] == id) return true;
        }
    }
    return false;
}

} // namespace

NodeId write_literal_argument(const Ast& ast, const Node& call, const natives::BuiltinSpec& spec) {
    if (spec.id != natives::BuiltinId::FileWrite && spec.id != natives::BuiltinId::FileWriteLine &&
        spec.id != natives::BuiltinId::StreamWrite && spec.id != natives::BuiltinId::StreamWriteLine)
        return kNoNode;
    const NodeId* args = ast.list(call);
    for (std::uint32_t k = 0; k < call.count; ++k)
        if (contains_interpolated_literal(ast, args[k])) return args[k];
    return kNoNode;
}

bool check_handle_flow(const Ast& ast, NodeId root, const UnitKinds& kinds, std::string& reason,
                       std::uint32_t& offset) {
    std::vector<NodeId> parent(ast.size(), kNoNode);
    std::vector<NodeId> stack{root};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const Node& n = ast.at(id);
        const auto add = [&](NodeId child) {
            if (child == kNoNode || child >= parent.size() || parent[child] != kNoNode) return;
            parent[child] = id;
            stack.push_back(child);
        };
        add(n.a);
        add(n.b);
        add(n.c);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) add(list[k]);
    }
    for (NodeId id = 0; id < ast.size(); ++id) {
        if ((kinds.of(id) & kKindHandle) == 0) continue;
        if (handle_position_allowed(ast, id, parent, kinds)) continue;
        reason = "host-handle-flow";
        offset = ast.at(id).begin;
        return true;
    }
    return false;
}

} // namespace nift::rt::detail
