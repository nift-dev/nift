#include "rt/vm_parallel_cleanup.h"
#include "rt/vm_coro.h"
#include "rt/parallel_task.h"
#include "rt/shared_state_internal.h"

// rt-lint: cold-file emergency structured task cleanup
namespace nift::rt {
// CP23 x CP19 (reconciliation item 1): workers settle (abort, then join)
// before the HandleTable shuts down, so no worker can use a resource that is
// being closed; the table shutdown is CP19's (it may be a persistent session's).
Vm::~Vm() {
    coro_abort_parallel();
    handle_table_.shutdown_noexcept();
}
// CP23 x CP19 (item 2): tasks finish and their output drains first; the Script
// context then closes through CP19's complete_run, which keeps a persistent
// session's context open across commands (finish_session_resources closes it).
void Vm::finish_run(VmResult& result, std::string& output) {
    coro_output_ = nullptr;
    if (coro_) coro_teardown(result);
    drain_parallel_output(output);
    complete_run(result);
}
void Vm::coro_abort_parallel() noexcept {
    if (!coro_ || coro_->parallel_live.empty()) return;
    for (const auto& record : coro_->parallel_live) {
        if (!record || record->complete()) continue;
        if (coro_->parallel && coro_->parallel->cancel_queued(record)) continue;
        {
            std::lock_guard<std::mutex> lock(record->mutex);
            if (record->state == ParallelTaskState::Pending) record->cancelled = true;
        }
        notify_parallel_wait_states(record);
    }
    shared_detail::notify_shared_cancellation();
    for (const auto& record : coro_->parallel_live) {
        if (!record || record->complete()) continue;
        ParallelBlock blocked(ParallelWaitKind::Cleanup);
        std::unique_lock<std::mutex> lock(record->mutex);
        record->cv.wait(lock, [&] { return record->state != ParallelTaskState::Pending; });
    }
}
}
