#include "rt/compiler_indexed_alias.h"

// rt-lint: cold-file compile-time proof of indexed alias roots and uses
namespace nift::rt::detail {

bool UnitCompiler::stable_indexed_root(NodeId binding, KindSet element, bool stable_members) const {
    const NodeKind literal_kind = element == kKindObject ? NodeKind::Object : NodeKind::Array;
    const auto array_of_literals = [&](NodeId id) {
        const Node& literal = ast_.at(id);
        if (literal.kind != NodeKind::Array) return false;
        const NodeId* items = ast_.list(literal);
        for (std::uint32_t k = 0; k < literal.count; ++k)
            if (ast_.at(items[k]).kind != literal_kind) return false;
        return true;
    };
    const auto is_identity_path = [&](const Node* outer, const Node& path) {
        std::uint16_t function = 0;
        return outer && indexed_identity_argument(*outer, function) == &path;
    };
    const auto is_print_read = [&](const Node* consumer, NodeId value) {
        if (!consumer || consumer->kind != NodeKind::Call || consumer->count != 1 ||
            ast_.list(*consumer)[0] != value || kinds_.bindings.count(consumer->a)) return false;
        const Node& head = ast_.at(consumer->a);
        return head.kind == NodeKind::Ident && text(head) == "print" && !plan_.index.count(text(head));
    };
    struct Visit { NodeId id, parent, outer, consumer; };
    std::vector<Visit> pending{{root_, kNoNode, kNoNode, kNoNode}};
    bool declared = false;
    while (!pending.empty()) {
        const auto item = pending.back();
        pending.pop_back();
        if (item.id == kNoNode) continue;
        const Node& n = ast_.at(item.id);
        if (n.kind == NodeKind::Command || n.kind == NodeKind::Opaque || n.kind == NodeKind::TemplateStmt)
            return false;
        if (n.kind == NodeKind::Declare && n.a == binding) {
            if (!array_of_literals(n.b)) return false;
            declared = true;
        }
        const auto resolved = kinds_.bindings.find(item.id);
        if (item.id != binding && n.kind == NodeKind::Ident &&
            resolved != kinds_.bindings.end() && resolved->second == binding) {
            if (item.parent == kNoNode) return false;
            const Node& parent = ast_.at(item.parent);
            const Node* outer = item.outer == kNoNode ? nullptr : &ast_.at(item.outer);
            if (parent.kind == NodeKind::Assign && parent.a == item.id && !stable_members &&
                array_of_literals(parent.b)) {
            } else if (parent.kind == NodeKind::Index && parent.a == item.id &&
                       (is_identity_path(outer, parent) ||
                        (outer && outer->kind == NodeKind::Declare && !outer->flags &&
                         outer->b == item.parent && indexed_array_path(parent) &&
                         observed_array_alias(outer->a, true, true, stable_members)) ||
                        (outer && outer->kind == NodeKind::Lambda && !outer->count &&
                         outer->b == item.parent && ast_.at(parent.b).kind == NodeKind::Number) ||
                        (outer && outer->kind == NodeKind::Index && outer->a == item.parent &&
                         item.consumer != kNoNode && is_print_read(&ast_.at(item.consumer), item.outer)) ||
                        (outer && outer->kind == NodeKind::Assign && outer->a == item.parent &&
                         !stable_members && ast_.at(outer->b).kind == literal_kind))) {
            } else if (parent.kind == NodeKind::Declare && parent.b == item.id &&
                       observed_array_alias(parent.a, false)) {
            } else if (parent.kind == NodeKind::Member && parent.a == item.id && outer &&
                       outer->kind == NodeKind::Call && outer->a == item.parent) {
                const auto method = text(ast_.at(parent.b));
                if (stable_members && (method == "clear" || method == "remove")) return false;
                if (method != "clear" && method != "remove" && method != "stringify" &&
                    !(method == "push" && outer->count == 1 &&
                      ast_.at(ast_.list(*outer)[0]).kind == literal_kind)) return false;
            } else {
                return false;
            }
        }
        for (NodeId child : {n.a, n.b, n.c}) pending.push_back({child, item.id, item.parent, item.outer});
        for (std::uint32_t k = 0; k < n.count; ++k)
            pending.push_back({ast_.list(n)[k], item.id, item.parent, item.outer});
    }
    return declared;
}

bool UnitCompiler::observed_array_alias(NodeId binding, bool rebind, bool forward, bool members) const {
    std::unordered_map<NodeId, NodeId> copies;
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& node = ast_.at(id);
        if (node.kind == NodeKind::Command || node.kind == NodeKind::Opaque ||
            node.kind == NodeKind::TemplateStmt) return false;
    }
    // Projected members may be read as ordinary shared values only when
    // the root proof forbids slot replacement and this alias cannot rebind.
    const auto member_receiver = [&](NodeId id) {
        const Node& member = ast_.at(id);
        if (member.kind == NodeKind::Member) return member.a;
        if (member.kind == NodeKind::Index) {
            const Node& key = ast_.at(member.b);
            if (key.kind == NodeKind::String && key.count == 0) return member.a;
        }
        return kNoNode;
    };
    std::unordered_set<NodeId> reads;
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& call = ast_.at(id);
        if (forward && top_level_decl_ids_.count(id) && call.kind == NodeKind::Declare && !call.flags &&
            call.b != kNoNode && ast_.at(call.a).kind == NodeKind::Ident && !captured_binding(call.a)) {
            const Node& init = ast_.at(call.b);
            std::uint16_t identity = 0;
            const Node* use = init.kind == NodeKind::Ident ? &init : indexed_identity_argument(init, identity, true);
            if (use) copies.emplace(node_id(*use), call.a);
        }
        if (rebind && !members && call.kind == NodeKind::Assign) reads.insert(call.a);
        if (members && call.kind == NodeKind::Declare && !call.flags && call.b != kNoNode &&
            top_level_decl_ids_.count(id) && kinds(call.b) == kKindArray) {
            const NodeId receiver = member_receiver(call.b);
            if (receiver != kNoNode && observed_array_alias(call.a, true)) reads.insert(receiver);
        }
        if (call.kind != NodeKind::Call) continue;
        const Node& member = ast_.at(call.a);
        if (member.kind != NodeKind::Member) continue;
        const auto method = text(ast_.at(member.b));
        if (members && !call.count && method == "stringify") {
            const NodeId receiver = member_receiver(member.a);
            if (receiver != kNoNode) reads.insert(receiver);
        }
        if ((!call.count && method == "stringify") ||
            (rebind && !members && call.count == 1 && method == "push" &&
             ast_.at(ast_.list(call)[0]).kind == NodeKind::Number)) reads.insert(member.a);
    }
    std::unordered_set<NodeId> bindings{binding};
    std::vector<NodeId> pending{binding};
    while (!pending.empty()) {
        const NodeId current = pending.back();
        pending.pop_back();
        for (const auto& [use, declaration] : kinds_.bindings) {
            if (declaration != current || use == current || reads.count(use)) continue;
            const auto copy = copies.find(use);
            if (copy == copies.end()) return false;
            if (bindings.insert(copy->second).second) pending.push_back(copy->second);
        }
    }
    return true;
}

} // namespace nift::rt::detail
