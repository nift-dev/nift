#include "rt/vm_parallel_cleanup.h"
#include "rt/vm_coro.h"
#include "rt/parallel_task.h"
#include "rt/shared_state_internal.h"

// rt-lint: cold-file emergency structured task cleanup
namespace nift::rt {
Vm::~Vm() { coro_abort_parallel(); }
void Vm::finish_run(VmResult& result, std::string& output) {
    coro_output_ = nullptr;
    if (coro_) coro_teardown(result);
    drain_parallel_output(output);
    close_handle_runtime(result);
    result_stats(result);
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
    shared_detail::registry().cv.notify_all();
    for (const auto& record : coro_->parallel_live) {
        if (!record || record->complete()) continue;
        ParallelBlock blocked;
        std::unique_lock<std::mutex> lock(record->mutex);
        record->cv.wait(lock, [&] { return record->state != ParallelTaskState::Pending; });
    }
}
}
