#include "rt/unit_check.h"

// rt-lint: cold-file compile-time check, once per unit

#include <utility>
#include <vector>

namespace nift::rt {

namespace {

// Pushes the children of `n` so that they pop in pre-order (a, b, c, list).
template <class Entry, class Make>
void push_children(const Ast& ast, const Node& n, std::vector<Entry>& stack, const Make& make) {
    const NodeId* list = ast.list(n);
    for (std::uint32_t k = n.count; k-- > 0;)
        if (list[k] != kNoNode) stack.push_back(make(list[k]));
    for (NodeId child : {n.c, n.b, n.a})
        if (child != kNoNode) stack.push_back(make(child));
}

} // namespace

UnitShape check_unit_depth(const Ast& ast, NodeId root) {
    UnitShape shape;
    if (root == kNoNode) return shape;
    std::vector<std::pair<NodeId, std::size_t>> stack{{root, 1}};
    while (!stack.empty()) {
        const auto [id, depth] = stack.back();
        stack.pop_back();
        const Node& n = ast.at(id);
        if (depth > kMaxUnitTreeDepth) {
            shape.ok = false;
            shape.offset = n.begin;
            return shape;
        }
        push_children(ast, n, stack, [depth = depth](NodeId c) { return std::make_pair(c, depth + 1); });
    }
    return shape;
}

NodeId first_opaque_node(const Ast& ast, NodeId root) {
    if (root == kNoNode) return kNoNode;
    std::vector<NodeId> stack{root};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::Opaque) return id;
        push_children(ast, n, stack, [](NodeId c) { return c; });
    }
    return kNoNode;
}

} // namespace nift::rt
