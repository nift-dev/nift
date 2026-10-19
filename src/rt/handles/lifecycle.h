#pragma once
// Lifecycle records of the CP17 handle table (D-035): one per resource, from
// create() until the resource is both settled and no longer in a slot. Kept
// apart from slots so that "reference count reached zero / handle closed" and
// "resource fully reclaimed" are distinct. Not thread-safe: HandleTable calls
// it with its lock held.
#include "rt/handles/types.h"

#include <cstdint>
#include <unordered_map>

namespace nift::rt::handles {

struct LifeRecord {
    LifeState state = LifeState::Active;
    TerminationCause cause = TerminationCause::None;
    ContextId owner = kDetached;      // the resource's owner (moves with detach/transfer while open)
    ReleaseReason reason = ReleaseReason::Explicit;
    HandleKind kind = HandleKind::None;
    Value handle;
    bool slot_live = true;            // the handle is still open
    bool obligation = false;          // closed but not yet settled: the owner must wait for it
    bool ticket_issued = false;
};

bool is_settled(LifeState state);
TerminationCause cause_of(ReleaseReason reason);

class LifecycleStore {
public:
    std::uint64_t add(HandleKind kind, const Value& handle, ContextId owner);
    LifeRecord* find(std::uint64_t id);
    const LifeRecord* find(std::uint64_t id) const;
    void erase(std::uint64_t id) { records_.erase(id); }
    std::size_t size() const { return records_.size(); }
    // Applies a final outcome to an unsettled record. Returns false when the
    // record is already settled. Fills `failure` (and sets `failed`) for
    // Failed/Lost, with default codes and messages where the reporter gave none.
    static bool settle(LifeRecord& record, CleanupOutcome&& outcome, CleanupFailure& failure, bool& failed);

private:
    std::unordered_map<std::uint64_t, LifeRecord> records_;
    std::uint64_t next_ = 1;
};

} // namespace nift::rt::handles
