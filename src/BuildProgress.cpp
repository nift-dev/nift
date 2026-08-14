#include "BuildProgress.h"
#include "Console.h"

#include <iostream>

BuildProgress::BuildProgress(std::size_t total, std::atomic<std::size_t>& completed)
    : total_(total), completed_(completed) {
    if (total_ == 0 || !console::stdout_is_tty()) return;

    thread_ = std::thread([this] {
        {
            std::unique_lock<std::mutex> lock(wait_mutex_);
            if (wake_.wait_for(lock, display_delay, [this] { return stop_.load(); })) return;
        }

        bool displayed = false;
        while (!stop_.load() && completed_.load() < total_) {
            const std::size_t done = completed_.load();
            const std::size_t percent = (100 * done) / total_;

            {
                std::lock_guard<std::mutex> lock(console::output_mutex);
                std::cout << "\r  building " << done << '/' << total_ << "  " << percent << '%' << std::flush;
            }
            displayed = true;

            std::unique_lock<std::mutex> lock(wait_mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(100), [this] { return stop_.load(); });
        }

        if (displayed) {
            std::lock_guard<std::mutex> lock(console::output_mutex);
            std::cout << "\r\033[2K" << std::flush;
        }
    });
}

BuildProgress::~BuildProgress() {
    stop_.store(true);
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
}
