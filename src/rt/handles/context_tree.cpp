#include "rt/handles/context_tree.h"

#include <algorithm>

namespace nift::rt::handles {

HandleStatus ContextTree::open(ContextKind kind, ContextId parent, ContextId& out, std::string label) {
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
    c.label = std::move(label);
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

HandleStatus ContextTree::cancel(ContextId id) {
    HandleStatus status;
    ExecContext* c = find(id, status);
    if (!c) return status;
    if (c->state == ContextState::Closing) return HandleStatus::ContextClosed;
    std::vector<ContextId> work{id};
    while (!work.empty()) {
        ExecContext& each = contexts_[work.back()];
        work.pop_back();
        if (each.state == ContextState::Open) each.state = ContextState::Cancelling;
        for (const ContextId child : each.children) {
            if (contexts_.count(child)) work.push_back(child);
        }
    }
    return HandleStatus::Ok;
}

void ContextTree::collect(ContextId id, std::vector<ContextId>& order, std::vector<ContextId>& elsewhere) const {
    const auto self = contexts_.find(id);
    if (self == contexts_.end()) return;
    // Read-only: nothing is marked until the whole walk has allocated.
    for (auto it = self->second.children.rbegin(); it != self->second.children.rend(); ++it) {
        const auto child = contexts_.find(*it);
        if (child == contexts_.end()) continue;
        if (child->second.state == ContextState::Closing) elsewhere.push_back(*it);  // another close owns it
        else collect(*it, order, elsewhere);
    }
    order.push_back(id);
}

std::vector<ContextId> ContextTree::begin_close(ContextId id, std::uint64_t closer, std::vector<ContextId>& elsewhere) {
    // CP17 x CP18 (2026-10-27): two phases, so an allocation failure in the
    // walk leaves every context as it was (open, retryable). Marking a context
    // Closing before a throw stranded it: no closer would ever end it and
    // shutdown() (the table's destructor) waited for it forever.
    std::vector<ContextId> order;
    collect(id, order, elsewhere);
    for (const ContextId each : order) {  // no allocation from here
        const auto it = contexts_.find(each);
        it->second.before_close = it->second.state;
        it->second.state = ContextState::Closing;
        it->second.closer = closer;
    }
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
