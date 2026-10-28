#include "rt/handles/table_settle.h"
// rt-lint: cold-file handle lifecycle settlement (once per closed resource)

namespace nift::rt::handles {

bool HandleTable::settle(std::uint64_t id, CleanupOutcome&& outcome) {
    std::lock_guard<std::mutex> lock(mu_);
    LifeRecord* r = records_.find(id);
    if (!r || is_settled(r->state)) return false;
    const ContextId owner = r->owner;
    CleanupFailure failure;
    bool failed = false;
    try {
        // The report is built before anything waiters depend on changes.
        LifecycleStore::settle(*r, std::move(outcome), failure, failed);
        if (failed) {
            const ExecContext* c = owner == kDetached ? nullptr : contexts_.find(owner);
            if (c) failure.context_label = c->label;
        }
    } catch (...) {
        // CP17 x CP18: an allocation failure building the report. The ticket
        // is spent, so nothing else would ever settle this record: settle it
        // Lost and release what waiters wait for, without allocating.
        lose_record_locked(id, owner);
        throw;
    }
    ++settled_;
    release_record_locked(id, owner);
    if (failed) {
        try {
            failures_of(owner).push_back(std::move(failure));
        } catch (...) {
            reports_lost_.store(true, std::memory_order_relaxed);
            throw;
        }
    }
    return true;
}

void HandleTable::release_record_locked(std::uint64_t id, ContextId owner) noexcept {
    if (LifeRecord* r = records_.find(id)) {
        if (r->obligation) {
            r->obligation = false;
            if (std::size_t* n = obligations_of(owner)) --*n;
            --obligations_;
        }
        if (!r->slot_live) records_.erase(id);  // reserved: never allocates
    }
    idle_.notify_all();
}

void HandleTable::lose_record_locked(std::uint64_t id, ContextId owner) noexcept {
    reports_lost_.store(true, std::memory_order_relaxed);
    if (LifeRecord* r = records_.find(id); r && !is_settled(r->state)) {
        r->state = LifeState::Lost;
        ++settled_;
    }
    release_record_locked(id, owner);
}

void HandleTable::abandon_cleanup_locked(std::uint64_t record, ContextId owner) noexcept {
    lose_record_locked(record, owner);
    finish_locked(owner);
}

} // namespace nift::rt::handles
