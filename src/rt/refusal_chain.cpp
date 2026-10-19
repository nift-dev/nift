#include "rt/refusal_chain.h"

#include "rt/refusal_names.h"

// rt-lint: cold-file test tooling (refusal inventory), never on an execution path

namespace nift::rt::inventory {

// A masked call's arguments are still inventoried: `name := [args]` is
// inserted before the statement (`name` spelled by the callee's name, so the
// declaration has a valid identifier; it becomes a masked name).
void Masker::spill_arguments(NodeId statement, NodeId call) {
    const Node c = ast_.at(call);
    if (c.count == 0 || c.a == kNoNode) return;
    if (!spill_name_found_) {
        spill_name_ = spill_name(ast_, root_);
        spill_name_found_ = true;
    }
    if (spill_name_.second == 0) return;
    std::vector<NodeId> args(ast_.list(c), ast_.list(c) + c.count);
    // if (true) { name := [args] }: a block of its own, so the name may
    // shadow a binding without a redeclaration error.
    const NodeId ident = ast_.add(NodeKind::Ident, spill_name_.first, spill_name_.second);
    const NodeId array = ast_.add(NodeKind::Array, c.begin, c.end);
    ast_.set_list(array, args);
    const NodeId decl = ast_.add(NodeKind::Declare, c.begin, c.end);
    ast_.at(decl).a = ident;
    ast_.at(decl).b = array;
    const NodeId block = ast_.add(NodeKind::Block, c.begin, c.end);
    ast_.set_list(block, {decl});
    const NodeId cond = ast_.add(NodeKind::True, c.begin, c.begin);
    const NodeId branch = ast_.add(NodeKind::If, c.begin, c.end);
    ast_.at(branch).a = cond;
    ast_.at(branch).b = block;
    for (NodeId m : {ident, array, decl, block, cond, branch}) masked_.insert(m);
    add_name(ast_.text(ast_.at(ident)));
    replace_in_parent(statement, {branch, statement});
}

// `postfix-receiver`: a member, index or method on a call result
// (`s.trim().upper()`, `f().k`). The access becomes `[receiver, args...]`, so
// the inner call is still inventoried; a method's own name is refused next
// anyway unless it is length/size (compiler_call.cpp), so it is recorded.
NodeId Masker::statement_of(NodeId id) const {
    while (id != kNoNode && !statement_[id]) id = parent_[id];
    return id;
}

// `target` takes `source`'s contents (its span kept) and is masked.
void Masker::become(NodeId target, NodeId source) {
    const Node copy = ast_.at(source);
    const std::uint32_t begin = ast_.at(target).begin, end = ast_.at(target).end;
    ast_.at(target) = copy;
    ast_.at(target).begin = begin;
    ast_.at(target).end = end;
    masked_.insert(target);
}

bool Masker::unchain(std::uint32_t offset) {
    for (NodeId id = 0; id < ast_.size(); ++id) {
        const Node& n = ast_.at(id);
        if (!reachable_[id] || n.begin != offset || masked_.count(id)) continue;
        if (n.kind != NodeKind::Member && n.kind != NodeKind::Index) continue;
        const NodeKind left = ast_.at(n.a).kind;
        if (left == NodeKind::Ident || left == NodeKind::Member || left == NodeKind::Index) continue;
        const NodeId p = parent_[id];
        NodeId target = id;
        // Shape of the refused access, for the inventory: receiver -> access.
        const Node& recv = ast_.at(n.a);
        std::string shape = node_kind_name(recv.kind);
        if (recv.kind == NodeKind::Call && recv.a != kNoNode) {
            const Node& callee = ast_.at(recv.a);
            if (callee.kind == NodeKind::Ident) shape = "call(" + std::string(ast_.text(callee)) + ")";
            else if ((callee.kind == NodeKind::Member || callee.kind == NodeKind::SafeMember) && callee.b != kNoNode)
                shape = "method(" + std::string(ast_.text(ast_.at(callee.b))) + ")";
        }
        if (n.kind == NodeKind::Index) shape += "->index";
        else if (p != kNoNode && ast_.at(p).kind == NodeKind::Call && ast_.at(p).a == id)
            shape += "->method:" + std::string(ast_.text(ast_.at(n.b)));
        else shape += "->member";
        detail_ = shape;
        if (n.kind == NodeKind::Member && p != kNoNode && ast_.at(p).kind == NodeKind::Call && ast_.at(p).a == id) {
            target = p;
            const std::string_view name = ast_.text(ast_.at(n.b));
            if (name != "length" && name != "size") extra_.push_back("method:" + std::string(name));
            if (statement_of(p) != kNoNode) spill_arguments(statement_of(p), p);
        }
        become(target, n.a);  // the access is replaced by its receiver (the inner call), kinds aside
        return true;
    }
    return false;
}

} // namespace nift::rt::inventory
