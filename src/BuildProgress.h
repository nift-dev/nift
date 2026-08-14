#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>

class BuildProgress {
public:
    // Change this one value to control how long a build must run before
    // interactive progress is shown.
    static constexpr std::chrono::milliseconds display_delay{200};

    BuildProgress(std::size_t total, std::atomic<std::size_t>& completed);
    ~BuildProgress();

private:
    std::size_t total_;
    std::atomic<std::size_t>& completed_;
    std::atomic<bool> stop_{false};
    std::mutex wait_mutex_;
    std::condition_variable wake_;
    std::thread thread_;
};
