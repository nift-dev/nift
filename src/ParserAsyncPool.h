#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#ifdef __APPLE__
#include <pthread.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NIFT_PARSER_ASYNC_POOL_HIDDEN __attribute__((visibility("hidden")))
#else
#define NIFT_PARSER_ASYNC_POOL_HIDDEN
#endif

namespace nift::detail {

class NIFT_PARSER_ASYNC_POOL_HIDDEN NiftAsyncPool {
public:
    static NiftAsyncPool& instance();
    static bool is_worker_thread();

    void submit(std::function<void()> task);
    bool run_one();

    NiftAsyncPool(const NiftAsyncPool&) = delete;
    NiftAsyncPool& operator=(const NiftAsyncPool&) = delete;

private:
    NiftAsyncPool();
    ~NiftAsyncPool();
    void run_worker();
    void shutdown();

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::function<void()>> queue_;
#ifdef __APPLE__
    std::vector<pthread_t> workers_;
#else
    std::vector<std::thread> workers_;
#endif
    bool stop_ = false;
};

}  // namespace nift::detail

#undef NIFT_PARSER_ASYNC_POOL_HIDDEN
