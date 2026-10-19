// TEMPORARY SCAFFOLDING - DELETE WHEN CP17's HANDLE TABLE IS INTEGRATED (see handle_adapter_tmp.h).
#include "rt/host/handle_adapter_tmp.h"

#include <algorithm>

namespace nift::rt::host::tmp_handles {

ContextId TemporaryTable::open_context(ContextKind kind, ContextId parent) {
    const ContextId id = next_context_++;
    contexts_.push_back(Context{id, kind, parent, true});
    return id;
}

bool TemporaryTable::build_family(ContextId id) const {
    for (const auto& c : contexts_)
        if (c.id == id) return c.kind != ContextKind::Script || (c.parent != kDetached && build_family(c.parent));
    return false;
}

HandleId TemporaryTable::create(ContextId owner, std::shared_ptr<void> resource) {
    if (!resource) return 0;
    const HandleId id = next_handle_++;  // never reused: a stale id never resolves
    entries_.push_back(Entry{id, owner, std::move(resource)});
    return id;
}

std::shared_ptr<void> TemporaryTable::resolve(HandleId id) const {
    for (const auto& e : entries_) if (e.id == id) return e.resource;
    return nullptr;
}

CleanupOutcome TemporaryTable::run(Entry entry, ReleaseReason reason) {
    if (!hook_) return CleanupOutcome::success();
    return hook_(CleanupEvent{entry.id, entry.owner, reason, entry.resource});
}

bool TemporaryTable::release(HandleId id, ContextId expected_owner) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id && e.owner == expected_owner; });
    if (it == entries_.end()) return false;
    Entry e = std::move(*it);
    entries_.erase(it);
    run(std::move(e), ReleaseReason::Explicit);
    return true;
}

bool TemporaryTable::drop(HandleId id) {
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.id == id; });
    if (it == entries_.end()) return false;
    Entry e = std::move(*it);
    entries_.erase(it);
    run(std::move(e), ReleaseReason::Abandoned);
    return true;
}

bool TemporaryTable::detach(HandleId id, ContextId expected_owner) {
    if (build_family(expected_owner)) return false;
    for (auto& e : entries_) if (e.id == id && e.owner == expected_owner) { e.owner = kDetached; return true; }
    return false;
}

std::vector<CleanupOutcome> TemporaryTable::close_context(ContextId id) {
    std::vector<CleanupOutcome> out;
    std::vector<Entry> ending;
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) if (it->owner == id) ending.push_back(*it);  // newest first
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.owner == id; }), entries_.end());
    for (auto& c : contexts_) if (c.id == id) c.open = false;
    for (auto& e : ending) out.push_back(run(std::move(e), ReleaseReason::ContextEnd));
    return out;
}

void TemporaryTable::shutdown() {
    std::vector<Entry> left;
    left.swap(entries_);
    for (auto& e : left) {
        const ReleaseReason reason = e.owner == kDetached ? ReleaseReason::Shutdown : ReleaseReason::ContextEnd;
        run(std::move(e), reason);
    }
}

} // namespace nift::rt::host::tmp_handles
