#include "rt/refusal_mask.h"

#include "rt/refusal_names.h"

#include <algorithm>

// rt-lint: cold-file test tooling (refusal inventory), never on an execution path

namespace nift::rt::inventory {

namespace {

bool is_expression(NodeKind kind) {
    return kind >= NodeKind::Number && kind <= NodeKind::Await;
}

template <typename F>
void for_children(const Ast& ast, NodeId id, F&& f) {
    const Node& n = ast.at(id);
    if (n.a != kNoNode) f(n.a);
    if (n.b != kNoNode) f(n.b);
    if (n.c != kNoNode) f(n.c);
    const NodeId* list = ast.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k) f(list[k]);
}

bool starts_with(const std::string& text, std::string_view prefix) {
    return text.size() >= prefix.size() && std::string_view(text).substr(0, prefix.size()) == prefix;
}

// Reasons that name a construct; independent of masks unless they name a
// masked binding (handled separately).
bool identity_reason(const std::string& reason) {
    return starts_with(reason, "method:") || starts_with(reason, "statement:") ||
           starts_with(reason, "expression:") || starts_with(reason, "opaque:");
}

bool name_reason(const std::string& reason) {
    return reason == "free-name" || reason == "unresolved-name" || reason == "assign-free-name" ||
           reason == "fn-as-value" || reason == "call-binding";
}

} // namespace

void Masker::reindex() {
    parent_.assign(ast_.size(), kNoNode);
    reachable_.assign(ast_.size(), 0);
    statement_.assign(ast_.size(), 0);
    std::vector<NodeId> stack{root_};
    reachable_[root_] = 1;
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const NodeKind kind = ast_.at(id).kind;
        const bool block = kind == NodeKind::Program || kind == NodeKind::Block;
        const NodeId* list = ast_.list(ast_.at(id));
        for (std::uint32_t k = 0; k < ast_.at(id).count && block; ++k) statement_[list[k]] = 1;
        for_children(ast_, id, [&](NodeId child) {
            if (reachable_[child]) return;  // a tree; defensive against shared ids
            reachable_[child] = 1;
            parent_[child] = id;
            stack.push_back(child);
        });
    }
}

std::size_t Masker::depth(NodeId id) const {
    std::size_t d = 0;
    for (NodeId p = parent_[id]; p != kNoNode; p = parent_[p]) ++d;
    return d;
}

bool Masker::within(NodeId id, NodeId ancestor) const {
    for (NodeId p = id; p != kNoNode; p = parent_[p])
        if (p == ancestor) return true;
    return false;
}

NodeId Masker::innermost_statement(std::uint32_t offset) const {
    NodeId best = kNoNode;
    for (NodeId id = 0; id < ast_.size(); ++id) {
        if (!reachable_[id] || !statement_[id]) continue;
        const Node& n = ast_.at(id);
        if (!(n.begin == offset || (n.begin <= offset && offset < n.end))) continue;
        if (best == kNoNode || n.end - n.begin < ast_.at(best).end - ast_.at(best).begin ||
            (n.end - n.begin == ast_.at(best).end - ast_.at(best).begin && depth(id) > depth(best)))
            best = id;
    }
    return best;
}

// Expressions only, and not names that are part of a construct's shape
// (declared names, member names, object keys, parameters).
bool Masker::maskable(NodeId id) const {
    const Node& n = ast_.at(id);
    if (!is_expression(n.kind) && n.kind != NodeKind::Opaque) return false;
    // Structural parts of a literal: the compiler expects their kind.
    if (n.kind == NodeKind::Property || n.kind == NodeKind::StringPart) return false;
    if (masked_.count(id)) return false;
    const NodeId p = parent_[id];
    if (p == kNoNode) return false;
    const Node& parent = ast_.at(p);
    switch (parent.kind) {
        case NodeKind::Declare: case NodeKind::FnDecl: case NodeKind::StructDecl: case NodeKind::EnumDecl:
        case NodeKind::StructField: case NodeKind::Export: return parent.a != id;
        case NodeKind::Member: case NodeKind::SafeMember: return parent.b != id;
        case NodeKind::Property: return parent.a != id;
        case NodeKind::Try: return parent.b != id;
        case NodeKind::Pattern: case NodeKind::ForBinding: case NodeKind::Param: return false;
        default: return true;
    }
}

std::vector<NodeId> Masker::candidates(NodeId statement, std::uint32_t offset) const {
    std::vector<NodeId> out;
    for (NodeId id = 0; id < ast_.size(); ++id)
        if (id != statement && reachable_[id] && ast_.at(id).begin == offset && within(id, statement) && maskable(id))
            out.push_back(id);
    std::sort(out.begin(), out.end(), [this](NodeId x, NodeId y) {
        const std::uint32_t sx = ast_.at(x).end - ast_.at(x).begin, sy = ast_.at(y).end - ast_.at(y).begin;
        if (sx != sy) return sx < sy;
        return depth(x) > depth(y);
    });
    return out;
}

bool Masker::touches_mask(NodeId id) const {
    std::vector<NodeId> stack{id};
    while (!stack.empty()) {
        const NodeId n = stack.back();
        stack.pop_back();
        if (masked_.count(n)) return true;
        if (ast_.at(n).kind == NodeKind::Ident && names_.count(std::string(ast_.text(ast_.at(n))))) return true;
        for_children(ast_, n, [&](NodeId child) { stack.push_back(child); });
    }
    return false;
}

bool Masker::consequential(std::uint32_t offset, const std::string& reason) {
    reindex();
    // A construct's own name is not caused by a mask, even where a chain of
    // calls shares the masked offset.
    if (identity_reason(reason)) return false;
    if (starts_with(reason, "callable:")) return names_.count(reason.substr(9)) > 0;
    if (rung_.count(offset)) return true;
    if (name_reason(reason)) {
        for (NodeId id = 0; id < ast_.size(); ++id)
            if (reachable_[id] && ast_.at(id).kind == NodeKind::Ident && ast_.at(id).begin == offset)
                return names_.count(std::string(ast_.text(ast_.at(id)))) > 0;
        return false;
    }
    const NodeId statement = innermost_statement(offset);
    if (statement == kNoNode) return false;
    for (NodeId id = 0; id < ast_.size(); ++id)
        if (reachable_[id] && ast_.at(id).begin == offset && within(id, statement) && touches_mask(id)) return true;
    return false;
}

void Masker::null_node(NodeId id) {
    // The statement's target takes the masked value: its kinds change.
    NodeId s = id;
    while (s != kNoNode && !statement_[s]) s = parent_[s];
    if (s != kNoNode) {
        const Node& st = ast_.at(s);
        const bool assigns = st.kind == NodeKind::Declare || st.kind == NodeKind::Assign ||
                             st.kind == NodeKind::CompoundAssign;
        if (assigns && st.a != kNoNode && within(id, st.b)) {
            const Node& target = ast_.at(st.a);
            if (target.kind == NodeKind::Ident) add_name(ast_.text(target));
            if (target.kind == NodeKind::Pattern)
                for (std::uint32_t k = 0; k < target.count; ++k) add_name(ast_.text(ast_.at(ast_.list(target)[k])));
        }
        if (st.kind == NodeKind::Return)
            for (NodeId f = parent_[s]; f != kNoNode; f = parent_[f])
                if (ast_.at(f).kind == NodeKind::FnDecl && ast_.at(f).a != kNoNode) {
                    add_name(ast_.text(ast_.at(ast_.at(f).a)));
                    break;
                }
    }
    if (ast_.at(id).kind == NodeKind::Call && s != kNoNode) spill_arguments(s, id);
    Node& n = ast_.at(id);
    n.kind = NodeKind::Null;
    n.op = Op::None;
    n.flags = 0;
    n.a = n.b = n.c = kNoNode;
    n.count = 0;
    n.reason = OpaqueReason::None;
    masked_.insert(id);
}

void Masker::replace_in_parent(NodeId statement, const std::vector<NodeId>& replacement) {
    const NodeId p = parent_[statement];
    const Node& parent = ast_.at(p);
    std::vector<NodeId> list;
    const NodeId* items = ast_.list(parent);
    for (std::uint32_t k = 0; k < parent.count; ++k) {
        if (items[k] != statement) { list.push_back(items[k]); continue; }
        list.insert(list.end(), replacement.begin(), replacement.end());
    }
    ast_.set_list(p, list);
}

void Masker::add_declared_names(NodeId statement) {
    const Node& n = ast_.at(statement);
    switch (n.kind) {
        case NodeKind::Declare: {
            const Node& target = ast_.at(n.a);
            if (target.kind == NodeKind::Ident) add_name(ast_.text(target));
            for (std::uint32_t k = 0; target.kind == NodeKind::Pattern && k < target.count; ++k)
                add_name(ast_.text(ast_.at(ast_.list(target)[k])));
            return;
        }
        case NodeKind::FnDecl: case NodeKind::StructDecl: case NodeKind::EnumDecl:
            if (n.a != kNoNode) add_name(ast_.text(ast_.at(n.a)));
            return;
        default:
            for (std::string_view name : declared_names_in_text(ast_.text(n))) add_name(name);
            return;
    }
}

void Masker::statement_action(NodeId statement) {
    const Node node = ast_.at(statement);
    if (node.kind == NodeKind::For && node.b != kNoNode) {
        // while (true) { body }: the body stays loop code and is inventoried.
        if (node.a != kNoNode)
            for (std::uint32_t k = 0; k < ast_.at(node.a).count; ++k)
                add_name(ast_.text(ast_.at(ast_.list(ast_.at(node.a))[k])));
        const NodeId cond = ast_.add(NodeKind::True, node.begin, node.begin);
        const NodeId loop = ast_.add(NodeKind::While, node.begin, node.end);
        ast_.at(loop).a = cond;
        ast_.at(loop).b = node.b;
        masked_.insert(cond);
        masked_.insert(loop);
        replace_in_parent(statement, {loop});
        return;
    }
    if (node.kind == NodeKind::Try && node.a != kNoNode && node.c != kNoNode) {
        if (node.b != kNoNode) add_name(ast_.text(ast_.at(node.b)));
        const NodeId cond = ast_.add(NodeKind::True, node.begin, node.begin);
        const NodeId branch = ast_.add(NodeKind::If, node.begin, node.end);
        ast_.at(branch).a = cond;
        ast_.at(branch).b = node.a;
        ast_.at(branch).c = node.c;
        masked_.insert(cond);
        masked_.insert(branch);
        replace_in_parent(statement, {branch});
        return;
    }
    add_declared_names(statement);
    replace_in_parent(statement, {});
}

void Masker::hoist_functions() {
    const Node& program = ast_.at(root_);
    std::vector<NodeId> fns, rest;
    const NodeId* items = ast_.list(program);
    for (std::uint32_t k = 0; k < program.count; ++k)
        (ast_.at(items[k]).kind == NodeKind::FnDecl ? fns : rest).push_back(items[k]);
    fns.insert(fns.end(), rest.begin(), rest.end());
    ast_.set_list(root_, fns);
    hoisted_ = true;
}

bool Masker::mask_at(std::uint32_t offset, const std::string& reason) {
    reindex();
    extra_.clear();
    detail_.clear();
    if (reason.rfind("postfix-receiver", 0) == 0 && unchain(offset)) {
        ++rung_[offset];  // a repeat at this offset is consequential
        return true;
    }
    if ((reason == "fn-after-statement" || reason == "fn-called-before-declaration") && !hoisted_) {
        hoist_functions();
        return true;
    }
    const NodeId statement = innermost_statement(offset);
    if (statement == kNoNode) return false;
    const std::vector<NodeId> list = candidates(statement, offset);
    // Masked expressions drop out of the candidates, so the smallest one left
    // is always the next rung; then the statement itself.
    ++rung_[offset];
    if (!list.empty()) {
        // A refused callee is masked with its call (its arguments stay, see
        // null_node).
        NodeId target = list.front();
        const NodeId p = parent_[target];
        if (p != kNoNode && ast_.at(p).kind == NodeKind::Call && ast_.at(p).a == target) target = p;
        // A refused call (its callee's name or arity): the smallest call here.
        const bool call_reason = starts_with(reason, "method") || starts_with(reason, "callable:") ||
                                 reason == "callee" || reason == "static-error:arity";
        for (NodeId c : list)
            if (call_reason && ast_.at(c).kind == NodeKind::Call) {
                target = c;
                break;
            }
        // A refused method: the call becomes its receiver, so an inner chain
        // (`s.trim().upper()`: `trim`) is still inventoried.
        if (starts_with(reason, "method:")) {
            const std::string name = reason.substr(7);
            for (NodeId c : list) {
                const Node& call = ast_.at(c);
                if (call.kind != NodeKind::Call || call.a == kNoNode) continue;
                const Node& callee = ast_.at(call.a);
                if ((callee.kind != NodeKind::Member && callee.kind != NodeKind::SafeMember) || callee.b == kNoNode ||
                    ast_.text(ast_.at(callee.b)) != name)
                    continue;
                if (statement_of(c) != kNoNode) spill_arguments(statement_of(c), c);
                become(c, callee.a);
                return true;
            }
        }
        null_node(target);
        return true;
    }
    statement_action(statement);
    return true;
}

bool Masker::mask_opaque(NodeId id) {
    reindex();
    if (!reachable_[id]) return false;
    if (statement_[id]) statement_action(id);
    else null_node(id);
    return true;
}

} // namespace nift::rt::inventory
