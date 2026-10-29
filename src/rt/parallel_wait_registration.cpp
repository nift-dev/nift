#include "rt/parallel_wait_registration.h"
#include "rt/parallel_task.h"
#include <algorithm>
#include <utility>
namespace nift::rt {
ParallelWaitRegistration::ParallelWaitRegistration(std::shared_ptr<ParallelTaskRecord> owner)
    : record(std::move(owner)) {}
ParallelWaitRegistration::~ParallelWaitRegistration() noexcept {
    std::lock_guard<std::mutex> lock(record->mutex);
    record->waiters.erase(std::remove_if(record->waiters.begin(), record->waiters.end(),
        [&](const ParallelTaskWaiter& waiter) { return waiter.registration == this; }), record->waiters.end());
    record->wait_observers.erase(std::remove_if(record->wait_observers.begin(), record->wait_observers.end(),
        [&](const ParallelTaskObserver& observer) { return observer.registration == this; }),
        record->wait_observers.end());
}
} // namespace nift::rt
