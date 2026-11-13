#include "rt/parallel_starve.h"
#include "rt/shared_state_internal.h"

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

// Allocation-free by construction: the queue is swapped with spare_ (built with the runtime; std::deque::swap
// neither allocates nor throws), the limit message is a static string, failure_code gets a 14-character literal (in
// the string's own buffer), and everything else is integer and flag stores, mutex locks and condition notifies.
void ParallelRuntime::starve(std::uint32_t offset) noexcept {
    std::lock_guard<std::mutex> one_sweep(starve_mutex_);
    NIFT_STARVE_NO_ALLOCATION(true);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!limit_failed_) {  // the first limit stays authoritative (a budget failure noted earlier keeps its text)
            limit_failed_ = true; fatal_.store(kParallelResourcesExhausted, std::memory_order_release);
            limit_offset_ = offset;
        }
        starved_ = true;  // sticky: submit refuses every new task from now on
        if (kStarveSweeps) spare_.swap(queue_);
        for (const auto& record : spare_) { --active_; release_locked(record->reserved_bytes); }  // as cancel_queued
    }
    for (const auto& record : spare_) {  // never ran, never will: failed with the limit, waiters woken
        {
            std::lock_guard<std::mutex> lock(record->mutex);
            record->state = kStarveCancels ? ParallelTaskState::Cancelled : ParallelTaskState::Failed;
            record->failure_reason = StopReason::Error; record->failure_catchable = false;
            record->failure_code = kStarveCancels ? "runtime.cancelled" : "limit.exceeded";
            record->failure_starved = !kStarveCancels; record->failure_offset = record->spawn_offset;
        }
        notify_parallel_wait_states(record); record->cv.notify_all();
    }
    spare_.clear();  // keeps the deque's map: only the records' references are dropped
    NIFT_STARVE_NO_ALLOCATION(false);
    // Wakes the shared waits (they now fail with fatal_limit()) and wait_idle. resume_cv_ needs no notify: a sweep
    // changes no CPU permit (computing_); the starving block's released permit was notified by block_worker (M1).
    cv_.notify_all(); shared_detail::notify_shared_cancellation();
}

void ParallelRuntime::note_limit(const std::string& message, std::uint32_t offset, const char* reason) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (limit_failed_) return;  // idempotent: the first recorded limit stays authoritative
        limit_failed_ = true; limit_message_ = message; limit_offset_ = offset;
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
