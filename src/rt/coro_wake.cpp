#include "rt/coro_wake.h"

#include "rt/host/process_loop.h"

namespace nift::rt {

bool WakeSource::wait_until(Clock::time_point deadline) {
    std::unique_lock<std::mutex> lock(mutex_);
    // wait_until may return spuriously or early on some platforms' clocks:
    // loop until the deadline has really passed (a timer never fires early).
    while (posted_ == consumed_ && Clock::now() < deadline) cv_.wait_until(lock, deadline);
    const bool woken = posted_ != consumed_;
    consumed_ = posted_;
    return woken;
}

void WakeSource::post() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++posted_;
    }
    cv_.notify_one();
}

std::uint64_t WakeSource::post_at(Clock::time_point when) {
    std::weak_ptr<WakeSource> target = weak_from_this();
    return host::runtime::loop_at(when, [target] {
        if (const auto source = target.lock()) source->post();
    });
}

std::uint64_t WakeSource::posts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return posted_;
}

}  // namespace nift::rt
