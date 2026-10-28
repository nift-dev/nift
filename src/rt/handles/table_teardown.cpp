#include "rt/handles/table_teardown.h"
// rt-lint: cold-file handle table destruction (once per table)

#include <mutex>

namespace nift::rt::handles {

HandleTable::~HandleTable() { shutdown_noexcept(); }

void HandleTable::shutdown_noexcept() noexcept {
    try {
        shutdown();
        return;
    } catch (...) {
        // CP17 rework (F2): shutdown() walks the contexts and builds a report,
        // which allocates. A destructor (often run while an allocation failure
        // unwinds) must not need an allocation to release what it owns.
    }
    teardown_after_failure();
}

// Releases every live handle without building reports: each slot is taken
// once and its hook runs at most once (run_cleanup releases a cleanup whose
// bookkeeping fails without allocating, abandon_cleanup_locked); then waits
// for cleanups in flight elsewhere. One slot per step: bounded by the live
// slots (plus any a hook creates). The lost reports are recorded.
void HandleTable::teardown_after_failure() noexcept {
    reports_lost_.store(true, std::memory_order_relaxed);
    std::unique_lock<std::mutex> lock(mu_);
    for (;;) {
        bool found = false;
        std::uint32_t index = 0;
        for (std::size_t i = 0; i < store_.size() && !found; ++i) {
            if (store_.slot(static_cast<std::uint32_t>(i)).state == SlotState::Live) {
                index = static_cast<std::uint32_t>(i);
                found = true;
            }
        }
        if (found) {
            Started started = begin_cleanup_locked(index, ReleaseReason::Shutdown);
            try {
                run_cleanup(started, lock, nullptr);
            } catch (...) {
                if (!lock.owns_lock()) lock.lock();  // released and recorded lost by run_cleanup
            }
            continue;
        }
        if (detached_in_flight_ == 0 && obligations_ == 0 && !contexts_.any_in_flight()) break;
        idle_.wait(lock);
    }
}

} // namespace nift::rt::handles
