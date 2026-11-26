#include "rt/parallel_starve.h"
#include "rt/shared_state_internal.h"

#include <mutex>
#include <thread>

#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
#include <cstdlib>
#include <new>
#endif

// rt-lint: cold-file C141 (R1, R2): the runtime's fatal limit state: a cleanup wait's starve (noexcept), a noted limit
namespace nift::rt {

#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
// Test-only (the fault-injection builds): any allocation by this thread while it starves the runtime throws, which in
// the noexcept cleanup path would be std::terminate (exit 134): every starve the tests drive proves it allocates
// nothing.
namespace { thread_local bool no_allocation = false; }
#define NIFT_STARVE_NO_ALLOCATION(on) (no_allocation = (on))
#else
#define NIFT_STARVE_NO_ALLOCATION(on) static_cast<void>(0)
#endif

// C141a (W1, D-087): phase 1, in block_worker's mutex_ hold, so the failure (starved_, the limit, fatal_), the
// released permit, the refusal of new work and the empty queue are one critical section: no worker, help_timer, new
// worker or submit can start or admit work in between. Allocation-free: flag stores, std::deque::swap with spare_
// (built with the runtime: no allocation, no throw) and arithmetic.
void ParallelRuntime::exhaust_locked(bool& exhausted) noexcept {
    NIFT_STARVE_NO_ALLOCATION(true);
    exhausted = true;
    if (!kSweepOutsideLock) sweep_locked();
    NIFT_STARVE_NO_ALLOCATION(false);
}

void ParallelRuntime::sweep_locked() noexcept {
    if (starved_) return;  // a later exhausted block: the first one recorded the failure and swept
    starved_ = true; swept_pending_ = true; starver_ = std::this_thread::get_id();
    if (!limit_failed_) {  // the first limit stays authoritative (a budget failure noted earlier keeps its text)
        limit_failed_ = true; fatal_.store(kParallelResourcesExhausted, std::memory_order_release);
        offset_pending_ = true;  // phase 2 has the cleanup wait's offset
    }
    if (kStarveSweeps) spare_.swap(queue_);
    for (const auto& record : spare_) { --active_; release_locked(record->reserved_bytes); }  // as cancel_queued
}

// Phase 2, after the lock hold, by the thread whose phase 1 swept (starver_): the limit's offset, then the swept
// records failed and their waiters woken; a later starving block finds nothing to do but notify. Allocation-free:
// failure_code gets a 14-character literal (in the string's own buffer), the rest is flag stores, locks, notifies.
void ParallelRuntime::starve(std::uint32_t offset) noexcept {
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
    // C141a (W1), test-only: with NIFT_TEST_STARVE_AFTER_DEQUEUE set, the window between block_worker and this phase
    // is held open: workers are woken (as a spurious wake-up or another task's notify may) and the starve waits until
    // nothing is queued (a worker took it, or phase 1 swept it), polled under mutex_, at most 10 s.
    if (std::getenv("NIFT_TEST_STARVE_AFTER_DEQUEUE")) {
        cv_.notify_all();
        wait_for_test([this] { std::lock_guard<std::mutex> lock(mutex_); return queue_.empty(); });
    }
    // C141a (W1), test-only: with NIFT_TEST_HOLD_PHASE2 set, phase 2 waits until a submit has been refused (an
    // observer between the phases), at most 10 s.
    if (std::getenv("NIFT_TEST_HOLD_PHASE2")) wait_for_test([this] { return refusals_for_test_.load() != 0; });
#endif
    NIFT_STARVE_NO_ALLOCATION(true);
    bool owner = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (kSweepOutsideLock) sweep_locked();  // test-only M19: f448c7b8's second lock hold (the W1 window)
        owner = swept_pending_ && starver_ == std::this_thread::get_id();
        if (owner) {
            swept_pending_ = false;
            if (offset_pending_) { limit_offset_ = offset; offset_pending_ = false; }
        }
    }
    if (owner) {
        for (const auto& record : spare_) {  // never ran, never will: failed with the limit, waiters woken
            {
                std::lock_guard<std::mutex> lock(record->mutex);
                record->failure_reason = StopReason::Error; record->failure_catchable = false;
                record->failure_code = kStarveCancels ? "runtime.cancelled" : "limit.exceeded";
                record->failure_resources_exhausted = !kStarveCancels; record->failure_offset = record->spawn_offset;
                record->state = kStarveCancels ? ParallelTaskState::Cancelled : ParallelTaskState::Failed;
            }
            notify_parallel_wait_states(record); record->cv.notify_all();
        }
        spare_.clear();  // keeps the deque's map: only the records' references are dropped
    }
    NIFT_STARVE_NO_ALLOCATION(false);
    // Wakes the shared waits (they now fail with fatal_limit()) and wait_idle. resume_cv_ needs no notify: phase 2
    // changes no CPU permit; the starving block's released permit was notified by block_worker (M1).
    cv_.notify_all(); shared_detail::notify_shared_cancellation();
}

void ParallelRuntime::note_limit(const std::string& message, std::uint32_t offset, const char* reason) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (limit_failed_) return;  // idempotent: the first recorded limit stays authoritative
        limit_message_ = message; limit_offset_ = offset; limit_failed_ = true;
        fatal_.store(reason, std::memory_order_release);
    }
    shared_detail::notify_shared_cancellation();  // blocked shared waits observe fatal_limit() (D-085)
}

}  // namespace nift::rt

#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
void* operator new(std::size_t size) {
    if (nift::rt::no_allocation) throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
#endif
