#pragma once
// CP22 event-loop wait point of a VM run: the VM thread blocks here when no
// task is runnable, until the earliest timer deadline or until another thread
// (the CP16 host loop, through post_at) posts a wake-up. The host loop stays the
// only thread that polls OS events; this is not a second event loop.
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

namespace nift::rt {

class WakeSource : public std::enable_shared_from_this<WakeSource> {
public:
    using Clock = std::chrono::steady_clock;

    // Blocks until `deadline` (never returns earlier unless woken by a post).
    // True when a post arrived since the previous wait (it is consumed).
    bool wait_until(Clock::time_point deadline);
    // Any thread: wakes the waiter (or the next wait returns at once).
    void post() noexcept;
    // Posts from the CP16 host loop thread at `when` (loop_at). The callback
    // holds a weak reference: a source destroyed first is never touched.
    // Returns the host loop's timer id.
    std::uint64_t post_at(Clock::time_point when);
    std::uint64_t posts() const;  // total posts (tests)

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::uint64_t posted_ = 0;
    std::uint64_t consumed_ = 0;
};

}  // namespace nift::rt
