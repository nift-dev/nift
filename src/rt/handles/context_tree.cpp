#include "rt/handles/context_tree.h"

#include <algorithm>

namespace nift::rt::handles {

HandleStatus ContextTree::open(ContextKind kind, ContextId parent, ContextId& out) {
    bool build_family = is_build_kind(kind);
    if (parent != kDetached) {
        HandleStatus status;
        ExecContext* p = find(parent, status);
        if (!p) return status;
        if (p->state != ContextState::Open) return HandleStatus::ContextClosed;
        build_family = build_family || p->build_family;
    }
    const ContextId id = next_++;
    ExecContext& c = contexts_[id];
    c.kind = kind;
    c.parent = parent;
    c.build_family = build_family;
    if (parent != kDetached) contexts_[parent].children.push_back(id);
    else roots_.push_back(id);
    out = id;
    return HandleStatus::Ok;
}

ExecContext* ContextTree::find(ContextId id, HandleStatus& status) {
    const auto it = contexts_.find(id);
    if (it != contexts_.end()) { status = HandleStatus::Ok; return &it->second; }
    status = (id != kDetached && id < next_) ? HandleStatus::ContextClosed : HandleStatus::UnknownContext;
    return nullptr;
}

HandleStatus ContextTree::may_access(ContextId accessor, ContextId owner) const {
    if (accessor == owner || owner == kDetached || accessor == kAnyAccessor) return HandleStatus::Ok;
    // Nested contexts (a page render in a build, a hook) may use their
    // ancestors' handles; nothing else may.
    for (ContextId c = accessor; c != kDetached;) {
        const auto it = contexts_.find(c);
        if (it == contexts_.end()) break;
        c = it->second.parent;
        if (c == owner) return HandleStatus::Ok;
    }
    return HandleStatus::NotOwned;
}

void ContextTree::collect(ContextId id, std::vector<ContextId>& order) {
    ExecContext& c = contexts_[id];
    c.state = ContextState::Closing;
    // Iterate a copy: children are only erased by erase(), never during this walk.
    const std::vector<ContextId> children = c.children;
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (contexts_.count(*it)) collect(*it, order);
    }
    order.push_back(id);
}

std::vector<ContextId> ContextTree::begin_close(ContextId id) {
    std::vector<ContextId> order;
    collect(id, order);
    return order;
}

void ContextTree::erase(ContextId id) {
    const auto it = contexts_.find(id);
    if (it == contexts_.end()) return;
    const ContextId parent = it->second.parent;
    // Children still present (closed concurrently by another thread) are
    // re-rooted so roots() keeps reaching them.
    for (const ContextId child : it->second.children) {
        const auto ct = contexts_.find(child);
        if (ct == contexts_.end()) continue;
        ct->second.parent = kDetached;
        roots_.push_back(child);
    }
    std::vector<ContextId>& siblings = parent != kDetached && contexts_.count(parent)
                                           ? contexts_[parent].children : roots_;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), id), siblings.end());
    contexts_.erase(it);
}

} // namespace nift::rt::handles
