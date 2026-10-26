#pragma once
// Lifecycle records of the CP17 handle table (D-035): one per resource, from
// create() until the resource is both settled and no longer in a slot. Kept
// apart from slots so that "reference count reached zero / handle closed" and
// "resource fully reclaimed" are distinct. Not thread-safe: HandleTable calls
// it with its lock held.
#include "rt/handles/types.h"

#include <cstdint>
#include <vector>

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
    bool in_use = false;              // pool bookkeeping
    std::uint64_t sequence = 0;       // creation order in its table (1-based; HandleTable::creation_count)
};

bool is_settled(LifeState state);
TerminationCause cause_of(ReleaseReason reason);
// The settled state an outcome reports (Pending as a final report: Lost).
LifeState settled_state(CleanupOutcome::Result result);
// Default code and message for a failed settled state (Failed, Lost, CleanupIncomplete).
const char* default_failure_code(LifeState state);
const char* default_failure_message(LifeState state);
// The CleanupFailure for an outcome, with the defaults applied.
CleanupFailure make_failure(const LifeRecord& base, LifeState state, CleanupOutcome&& outcome);

// A pool of records; ids are pool index + 1 (0 = none). Reusing an id is safe:
// a record is erased only after it settled and its slot was given up, and the
// single ticket ever issued for it is spent by then (complete() empties it).
class LifecycleStore {
public:
    // CP17 x CP18 allocation safety: makes room for one more record, so the
    // next add() and every erase() up to then cannot allocate (may throw
    // std::bad_alloc; changes nothing visible).
    void reserve_one();
    std::uint64_t add(HandleKind kind, const Value& handle, ContextId owner);
    LifeRecord* find(std::uint64_t id);
    const LifeRecord* find(std::uint64_t id) const;
    void erase(std::uint64_t id);
    std::size_t size() const { return live_; }
    // Applies a final outcome to an unsettled record. Returns false when the
    // record is already settled. Fills `failure` (and sets `failed`) for
    // Failed/Lost, with default codes and messages where the reporter gave none.
    static bool settle(LifeRecord& record, CleanupOutcome&& outcome, CleanupFailure& failure, bool& failed);

private:
    std::vector<LifeRecord> records_;
    std::vector<std::uint64_t> free_;
    std::size_t live_ = 0;
};

} // namespace nift::rt::handles
