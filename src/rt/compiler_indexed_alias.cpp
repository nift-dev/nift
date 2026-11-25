#include "rt/compiler_indexed_alias.h"

#include <optional>

// rt-lint: cold-file compile-time proof of indexed alias roots and uses
namespace nift::rt::detail {

bool UnitCompiler::stable_indexed_root(NodeId binding, const Node& path, bool stable_members, bool direct) const {
    const bool keyed = kinds(path.a) == kKindObject;
    if (keyed && direct) return false;
    const bool computed = keyed && ast_.at(path.b).kind != NodeKind::String;
    // Raw spelling is sufficient only for ordinary quoted keys without
    // escapes. All other writes must obey the selected member's type proof.
    const auto plain_key = [&](const Node& key) {
        if (key.kind != NodeKind::String || key.count || key.a != kNoNode) return std::string_view{};
        const auto spelling = text(key);
        if (spelling.size() < 2 || spelling.front() != '"' || spelling.back() != '"') return std::string_view{};
        const auto body = spelling.substr(1, spelling.size() - 2);
        return body.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                       std::string_view::npos ? spelling : std::string_view{};
    };
    // A projected child can share the aggregate value only while its
    // parent member cannot be replaced. Inserts at disjoint keys are safe.
    const auto selected_key = keyed ? plain_key(ast_.at(path.b)) : std::string_view{};
    const auto disjoint_key = [&](const Node& key) {
        const auto other = plain_key(key);
        return !selected_key.empty() && !other.empty() && other != selected_key;
    };
    const NodeKind literal_kind = kinds(node_id(path)) == kKindObject ? NodeKind::Object : NodeKind::Array;
    const auto aggregate_of_literals = [&](NodeId id) {
        const Node& literal = ast_.at(id);
        if (literal.kind != (keyed ? NodeKind::Object : NodeKind::Array)) return false;
        const NodeId* items = ast_.list(literal);
        for (std::uint32_t k = 0; k < literal.count; ++k) {
            const Node& item = ast_.at(items[k]);
            if (keyed && item.kind != NodeKind::Property) return false;
            if ((keyed ? ast_.at(item.b) : item).kind != literal_kind) return false;
        }
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
    std::unordered_set<NodeId> keyed_reads;
    if (keyed) for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& call = ast_.at(id);
        if (call.kind != NodeKind::Call || call.count) continue;
        const Node& method = ast_.at(call.a);
        if (method.kind != NodeKind::Member || text(ast_.at(method.b)) != "stringify") continue;
        NodeId receiver = method.a;
        while (ast_.at(receiver).kind == NodeKind::Member) receiver = ast_.at(receiver).a;
        if (ast_.at(receiver).kind == NodeKind::Ident) keyed_reads.insert(receiver);
    }
    struct Visit { NodeId id, parent, outer, consumer, callable; };
    std::vector<Visit> pending{{root_, kNoNode, kNoNode, kNoNode, kNoNode}};
    // A key call may have effects, but cannot reach a root confined to
    // the current callable. Legacy validates against the pre-key receiver.
    std::optional<NodeId> root_callable;
    const auto same_callable = [&](NodeId callable) {
        if (!computed) return true;
        if (!root_callable) root_callable = callable;
        return *root_callable == callable;
    };
    bool declared = false;
    while (!pending.empty()) {
        const auto item = pending.back();
        pending.pop_back();
        if (item.id == kNoNode) continue;
        const Node& n = ast_.at(item.id);
        if (n.kind == NodeKind::Command || n.kind == NodeKind::Opaque || n.kind == NodeKind::TemplateStmt)
            return false;
        if (n.kind == NodeKind::Declare && n.a == binding) {
            if (!same_callable(item.callable) || !aggregate_of_literals(n.b)) return false;
            declared = true;
        }
        const auto resolved = kinds_.bindings.find(item.id);
        if (item.id != binding && n.kind == NodeKind::Ident &&
            resolved != kinds_.bindings.end() && resolved->second == binding) {
            if (!same_callable(item.callable)) return false;
            if (keyed_reads.count(item.id)) continue;
            if (item.parent == kNoNode) return false;
            const Node& parent = ast_.at(item.parent);
            const Node* outer = item.outer == kNoNode ? nullptr : &ast_.at(item.outer);
            if (parent.kind == NodeKind::Assign && parent.a == item.id && !stable_members && !direct &&
                aggregate_of_literals(parent.b)) {
            } else if (parent.kind == NodeKind::Index && parent.a == item.id &&
                       (is_identity_path(outer, parent) ||
                        (outer && outer->kind == NodeKind::Declare && !outer->flags &&
                         outer->b == item.parent && indexed_aggregate_path(parent) &&
                         observed_array_alias(outer->a, true, true, stable_members)) ||
                        (outer && outer->kind == NodeKind::Lambda && !outer->count &&
                         outer->b == item.parent && ast_.at(parent.b).kind == NodeKind::Number) ||
                        (outer && outer->kind == NodeKind::Index && outer->a == item.parent &&
                         item.consumer != kNoNode && is_print_read(&ast_.at(item.consumer), item.outer)) ||
                        (outer && outer->kind == NodeKind::Assign && outer->a == item.parent &&
                         ast_.at(outer->b).kind == literal_kind &&
                         (keyed ? (stable_members ? disjoint_key(ast_.at(parent.b))
                                                  : ast_.at(parent.b).kind == NodeKind::String)
                                : !stable_members)))) {
            } else if (parent.kind == NodeKind::Declare && parent.b == item.id &&
                       observed_array_alias(parent.a, false)) {
            } else if (parent.kind == NodeKind::Member && parent.a == item.id && outer &&
                       outer->kind == NodeKind::Call && outer->a == item.parent) {
                const auto method = text(ast_.at(parent.b));
                if (keyed && method != "stringify") return false;
                if (stable_members && (method == "clear" || method == "remove")) return false;
                const NodeId* args = ast_.list(*outer);
                // An inserted literal preserves the selected value's kind.
                // Projected children still require a stable parent slot.
                const bool insert = !stable_members && !direct && method == "insert" && outer->count == 2 &&
                                    ast_.at(args[1]).kind == literal_kind;
                if (method != "clear" && method != "remove" && method != "stringify" && !insert &&
                    !(method == "push" && outer->count == 1 && ast_.at(args[0]).kind == literal_kind)) return false;
            } else {
                return false;
            }
        }
        const auto callable = n.kind == NodeKind::FnDecl || n.kind == NodeKind::Lambda ? item.id : item.callable;
        for (NodeId child : {n.a, n.b, n.c}) pending.push_back({child, item.id, item.parent, item.outer, callable});
        for (std::uint32_t k = 0; k < n.count; ++k)
            pending.push_back({ast_.list(n)[k], item.id, item.parent, item.outer, callable});
    }
    return declared;
}

bool UnitCompiler::observed_array_alias(NodeId binding, bool rebind, bool forward, bool members,
                                        bool indexed_writes) const {
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
        if (rebind && !members && call.kind == NodeKind::Assign) {
            reads.insert(call.a);
            const Node& target = ast_.at(call.a);
            if (indexed_writes && target.kind == NodeKind::Index &&
                ast_.at(target.a).kind == NodeKind::Ident && kinds(target.a) == kKindArray &&
                kinds(target.b) && !(kinds(target.b) & ~kKindNumber))
                reads.insert(target.a);
        }
        if (members && call.kind == NodeKind::Declare && !call.flags && call.b != kNoNode &&
            top_level_decl_ids_.count(id) && kinds(call.b) == kKindArray) {
            const NodeId receiver = member_receiver(call.b);
            if (receiver != kNoNode && observed_array_alias(call.a, true, false, false, true)) reads.insert(receiver);
        }
        if (call.kind != NodeKind::Call) continue;
        const Node& member = ast_.at(call.a);
        // Immediate scalar member use reloads the live slot on every read.
        // Only the actual print builtin consumes it here.
        if (rebind && !forward && !members && !indexed_writes &&
            member.kind == NodeKind::Ident && text(member) == "print" && call.count == 1 &&
            !kinds_.bindings.count(call.a) && !plan_.index.count(text(member))) {
            const NodeId value = ast_.list(call)[0];
            const Node& projection = ast_.at(value);
            const KindSet kind = kinds(value);
            if (projection.kind == NodeKind::Member && kinds(projection.a) == kKindObject &&
                kind && !(kind & ~kKindScalar))
                reads.insert(projection.a);
        }
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
