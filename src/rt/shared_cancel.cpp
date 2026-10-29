#include "rt/shared_cancel.h"

#include "rt/future.h"
#include "rt/parallel_task.h"
#include "rt/shared_state_internal.h"

namespace nift::rt {

using namespace shared_detail;

bool shared_cancel(const Value& task_value, SharedOpError& error) {
    FutureObject* future = future_of(task_value);
    if (future == nullptr || !future->parallel) {
        fail(error, "runtime.invalid_argument", "cancel: expected a task handle");
        return false;
    }
    const std::shared_ptr<ParallelTaskRecord> record = future->parallel;
    bool wake = false;
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        if (record->state == ParallelTaskState::Pending &&
            !record->completion_started && !record->cancelled) {
            record->cancelled = true;
            wake = true;
        }
    }
    if (wake) {
        record->cv.notify_all();
        notify_parallel_wait_states(record);
        Registry& r = registry();
        r.cv.notify_all();
    }
    return true;
}

}  // namespace nift::rt
