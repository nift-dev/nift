#pragma once
// Per-await neutral registration identities and allocation-free rollback.
#include <cstdint>
#include <memory>
namespace nift::rt {
struct ParallelTaskRecord;
struct ParallelWait;
struct ParallelWaitRegistration;
struct ParallelTaskWaiter {
    std::uint32_t task = 0;
    const ParallelWaitRegistration* registration = nullptr; // identity only, never dereferenced
};
struct ParallelTaskObserver {
    const ParallelWaitRegistration* registration = nullptr;
    std::shared_ptr<ParallelWait> state;
};
// The suspended task owns this lease. The record holds only its identity and
// neutral notification data, so there is no cycle. Destruction also rolls back
// registrations on allocation failure or unexpected VM exception teardown.
struct ParallelWaitRegistration {
    explicit ParallelWaitRegistration(std::shared_ptr<ParallelTaskRecord> owner);
    ~ParallelWaitRegistration() noexcept;
    ParallelWaitRegistration(const ParallelWaitRegistration&) = delete;
    ParallelWaitRegistration& operator=(const ParallelWaitRegistration&) = delete;
    std::shared_ptr<ParallelTaskRecord> record;
};
} // namespace nift::rt
