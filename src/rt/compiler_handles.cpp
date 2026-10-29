#include "rt/compiler_handles.h"

#include "rt/builtin_bind.h"

#include <unordered_set>

#include <string>
#include <vector>


// CP17 W13: handle values are a forgery barrier. A handle may appear only as
// a local binding's value, a method receiver with a native bound for that
// handle family, or the argument of the free close(handle). Everything else
// (aggregates, calls, returns, captures, comparison, rendering, interpolation)
// is refused before execution with the same reason for every shape.
namespace nift::rt::detail {

namespace {

// Lexical function ownership: main is 0; every FnDecl/Lambda is its own
// function. Only names declared in the current function are local. A handle
// read in a nested function whose binding lives in an enclosing function is a
// capture, even when the immediate use is a permitted receiver/close.
struct FunctionOwner {
    NodeId parent = kNoNode;  // index, not node id
    std::unordered_set<std::string_view> decls;
};

std::size_t owner_index(NodeId fn_node) { return static_cast<std::size_t>(fn_node) + 1u; }

void collect_function_owners(const Ast& ast, NodeId id, std::size_t owner, std::vector<FunctionOwner>& owners,
                             std::vector<std::size_t>& node_owner) {
    if (id == kNoNode || id >= ast.size()) return;
    node_owner[id] = owner;
    const Node& n = ast.at(id);
    if (n.kind == NodeKind::FnDecl || n.kind == NodeKind::Lambda) {
        const std::size_t inner = owner_index(id);
        owners[inner].parent = owner;
        const NodeId* params = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) {
            const NodeId param = params[k];
            node_owner[param] = inner;
            owners[inner].decls.insert(ast.text(ast.at(param)));
        }
        collect_function_owners(ast, n.b, inner, owners, node_owner);
        return;
    }
    if (n.kind == NodeKind::Declare && n.a != kNoNode && ast.at(n.a).kind == NodeKind::Ident)
        owners[owner].decls.insert(ast.text(ast.at(n.a)));
    if (n.kind == NodeKind::For) {
        const NodeId* names = ast.list(ast.at(n.a));
        for (std::uint32_t k = 0; k < ast.at(n.a).count; ++k)
            owners[owner].decls.insert(ast.text(ast.at(names[k])));
    }
    if (n.kind == NodeKind::Try && n.b != kNoNode) owners[owner].decls.insert(ast.text(ast.at(n.b)));
    for (NodeId child : {n.a, n.b, n.c}) collect_function_owners(ast, child, owner, owners, node_owner);
    const NodeId* list = ast.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k) collect_function_owners(ast, list[k], owner, owners, node_owner);
}

bool handle_read_crosses_function(const Ast& ast, NodeId id, const std::vector<std::size_t>& node_owner,
                                  const std::vector<FunctionOwner>& owners) {
    if (ast.at(id).kind != NodeKind::Ident) return false;
    const std::size_t owner = node_owner[id];
    const std::string_view name = ast.text(ast.at(id));
    if (owners[owner].decls.count(name) != 0) return false;
    for (std::size_t parent = owners[owner].parent; parent != kNoNode; parent = owners[parent].parent)
        if (owners[parent].decls.count(name) != 0) return true;
    return false;
}


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

// A managed handle inside a `thread(...)` argument is admitted at compile time
// so the isolate transfer can refuse it with the specific non-transferable
// resource error at the spawn site (CP23). Ordinary calls keep the CP17
// handle-flow barrier.
bool inside_thread_argument(const Ast& ast, NodeId id, const std::vector<NodeId>& parent) {
    NodeId current = id;
    while (current < parent.size()) {
        const NodeId p = parent[current];
        if (p == kNoNode) return false;
        const Node& pn = ast.at(p);
        if (pn.kind == NodeKind::Array || pn.kind == NodeKind::Object || pn.kind == NodeKind::Property) {
            current = p;
            continue;
        }
        if (pn.kind != NodeKind::Call || pn.a == kNoNode) return false;
        const Node& callee = ast.at(pn.a);
        if (callee.kind != NodeKind::Ident || ast.text(callee) != "thread")  // rt-lint: allow-string-compare compile
            return false;
        const NodeId* args = ast.list(pn);
        for (std::uint32_t k = 0; k < pn.count; ++k)
            if (args[k] == current) return true;
        return false;
    }
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
                       std::uint32_t& offset, const SessionCompileContext* session) {
    // The validation loop below only examines nodes with a handle kind. Avoid
    // building ownership/parent graphs when none of its cases can apply.
    bool has_handles = false;
    for (NodeId id = 0; id < ast.size(); ++id) has_handles = has_handles || (kinds.of(id) & kKindHandle);
    if (!has_handles) return false;
    std::vector<FunctionOwner> owners(ast.size() + 1u);
    std::vector<std::size_t> node_owner(ast.size(), 0);
    collect_function_owners(ast, root, 0, owners, node_owner);
#if !defined(NIFT_SESSION_RESOURCE_MUTANT) || NIFT_SESSION_RESOURCE_MUTANT != 5
    if (session) for (const auto& symbol : session->symbols) owners[0].decls.insert(symbol.name);
#else
    (void)session;
#endif
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
        if (handle_read_crosses_function(ast, id, node_owner, owners)) {
            reason = "host-handle-flow";
            offset = ast.at(id).begin;
            return true;
        }
        if (inside_thread_argument(ast, id, parent)) {
            reason = "non-transferable resource: cannot transfer a managed file/stream/process handle between isolates";
            offset = ast.at(id).begin;
            return true;
        }
        if (handle_position_allowed(ast, id, parent, kinds)) continue;
        reason = "host-handle-flow";
        offset = ast.at(id).begin;
        return true;
    }
    return false;
}

} // namespace nift::rt::detail
