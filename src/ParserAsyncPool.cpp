#include "ParserAsyncPool.h"

#include <algorithm>

namespace nift::detail {
namespace {

thread_local bool nift_async_pool_worker = false;

}  // namespace

NiftAsyncPool& NiftAsyncPool::instance() {
    static NiftAsyncPool pool;
    return pool;
}

bool NiftAsyncPool::is_worker_thread() {
    return nift_async_pool_worker;
}

NiftAsyncPool::NiftAsyncPool() {
    const unsigned hint = std::thread::hardware_concurrency();
    const unsigned count = std::max(2u, hint == 0 ? 2u : hint);
    workers_.reserve(count);
    for (unsigned i = 0; i < count; ++i) workers_.emplace_back([this] {
        nift_async_pool_worker = true;
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) break;
                task = std::move(queue_.front());
                queue_.pop_front();
            }
            task();
        }
        nift_async_pool_worker = false;
    });
}

NiftAsyncPool::~NiftAsyncPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_)
        if (worker.joinable()) worker.join();
}

void NiftAsyncPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(std::move(task));
    }
    cv_.notify_one();
}

bool NiftAsyncPool::run_one() {
    std::function<void()> task;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        task = std::move(queue_.front());
        queue_.pop_front();
    }
    task();
    return true;
}

}  // namespace nift::detail
