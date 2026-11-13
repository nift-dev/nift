#include "rt/parallel_block.h"

#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
#include <cstdlib>
#endif

namespace nift::rt {

bool ParallelRuntime::block_worker(ParallelWaitKind kind, bool* exhausted) {
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
    // C141a (S1), test-only: with NIFT_TEST_S1 set, a worker blocking while work is queued and another task is
    // already blocked first waits (polled under mutex_, at most 10 s) until a worker is waiting to resume, so the
    // permit it releases goes to that resumer's wait: the schedule of the S1 lost wake-up.
    if (std::getenv("NIFT_TEST_S1"))
        wait_for_test([this] {
            std::lock_guard<std::mutex> held(mutex_);
            return queue_.empty() || blocked_ == 0 || resuming_ != 0;
        });
#endif
    std::lock_guard<std::mutex> lock(mutex_);
    const bool native = kind == ParallelWaitKind::Blocking;
    if (native && native_blocked_ == blocking_capacity_) return false;
    --computing_; ++blocked_;
    if (native) ++native_blocked_; else ++transient_blocked_;
    try { NIFT_INJECT_WAIT(kind); ensure_worker_locked(); }
    catch (...) {  // C141: a cleanup wait keeps its block (its permit stays released) and starves the runtime
        if (!exhausted || kCleanupRethrow) {
            ++computing_; --blocked_; if (native) --native_blocked_; else --transient_blocked_;
            throw;
        }
        exhaust_locked(*exhausted);  // C141a (W1): phase 1 of the starve, in this lock hold
        if (!kStarveNotifies) return true;  // test-only M17: without the M1 notify below
    }
    resume_cv_.notify_one(); cv_.notify_all();
    return true;
}

void ParallelRuntime::resume_worker(ParallelWaitKind kind) {
    std::unique_lock<std::mutex> lock(mutex_);
    ++resuming_;
    if (kind == ParallelWaitKind::Blocking) --native_blocked_; else --transient_blocked_;
    resume_cv_.wait(lock, [&] { return computing_ < limit_; });
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
    // C141a (S1), test-only: with NIFT_TEST_S1 set and work queued, the resumer takes its permit only once a second
    // permit is free too and every idle worker thread is parked in worker_loop's wait (none is about to check the
    // queue by itself); polled, and resuming_ still counts it, so no worker may start the queued task meanwhile.
    if (std::getenv("NIFT_TEST_S1") && !queue_.empty()) {
        lock.unlock();
        wait_for_test([this] {
            std::lock_guard<std::mutex> held(mutex_);
            const std::size_t idle = workers_.size() - computing_ - blocked_;
            return queue_.empty() || (computing_ + 2 <= limit_ && parked_for_test_ != 0 && parked_for_test_ == idle);
        });
        lock.lock();
        resume_cv_.wait(lock, [&] { return computing_ < limit_; });
    }
#endif
    --resuming_; --blocked_; ++computing_;
    // C141a (S1): resuming_ == 0 is part of worker_loop's predicate, so the last resumer wakes the idle workers when
    // work is queued and a permit is still free; otherwise a queued task could wait for an unrelated notify.
    if (kResumeWakes && resuming_ == 0 && !queue_.empty() && computing_ < limit_) cv_.notify_all();
}

}  // namespace nift::rt
