#include "HostTrace.h"
#include "ParserAsyncPool.h"

#include <algorithm>
#include <system_error>

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
#ifdef __APPLE__
    // An awaiting legacy worker can execute another queued task on the same
    // stack. Darwin's default worker stack is too small for the CP22 chain.
    constexpr std::size_t worker_stack_bytes = 4 * 1024 * 1024;
    pthread_attr_t attributes;
    int error = pthread_attr_init(&attributes);
    if (error) throw std::system_error(error, std::generic_category(), "pthread_attr_init");
    error = pthread_attr_setstacksize(&attributes, worker_stack_bytes);
    if (error) {
        pthread_attr_destroy(&attributes);
        throw std::system_error(error, std::generic_category(), "pthread_attr_setstacksize");
    }
#endif
    try {
        for (unsigned i = 0; i < count; ++i) {
#ifdef __APPLE__
            pthread_t worker;
            error = pthread_create(&worker, &attributes, [](void* pool) -> void* {
                static_cast<NiftAsyncPool*>(pool)->run_worker();
                return nullptr;
            }, this);
            if (error) throw std::system_error(error, std::generic_category(), "pthread_create");
            workers_.push_back(worker);
#else
            workers_.emplace_back([this] { run_worker(); });
#endif
        }
    } catch (...) {
#ifdef __APPLE__
        pthread_attr_destroy(&attributes);
#endif
        shutdown();
        throw;
    }
#ifdef __APPLE__
    pthread_attr_destroy(&attributes);
#endif
}

void NiftAsyncPool::run_worker() {
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
}

NiftAsyncPool::~NiftAsyncPool() {
    shutdown();
}

void NiftAsyncPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_) {
#ifdef __APPLE__
        pthread_join(worker, nullptr);
#else
        worker.join();
#endif
    }
}

void NiftAsyncPool::submit(std::function<void()> task) {
    // CP08 trace: label the task in the submitting thread (creation order),
    // and run it under that label on whichever worker picks it up.
    if (nift::trace::enabled()) {
        task = [label = nift::trace::child_label("async"), inner = std::move(task)]() mutable {
            const nift::trace::ContextGuard guard(label);
            inner();
        };
    }
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
