// Runtime 2.0 CP22: the coroutine scheduler's wait point (rt/coro_wake.h)
// across threads, for TSan: posts from plain threads and from the CP16 host
// loop thread (loop_at), racing a waiter that uses deadlines; a source
// destroyed while its host-loop timer is pending is never touched.
#include "rt/coro_wake.h"
#include "rt_test.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using nift::rt::WakeSource;
using Clock = std::chrono::steady_clock;

int main() {
    auto source = std::make_shared<WakeSource>();
    // Deadlines are never early.
    for (int k = 0; k < 20; ++k) {
        const auto t0 = Clock::now();
        source->wait_until(t0 + std::chrono::milliseconds(2));
        CHECK(Clock::now() - t0 >= std::chrono::milliseconds(2));
    }
    // Many posters, one waiter: every post is observed (none lost), the waiter
    // never blocks past a post.
    constexpr int kPosts = 2000;
    std::atomic<int> sent{0};
    std::vector<std::thread> posters;
    for (int t = 0; t < 4; ++t)
        posters.emplace_back([&] {
            for (int k = 0; k < kPosts / 4; ++k) {
                source->post();
                ++sent;
            }
        });
    int woken = 0;
    const auto limit = Clock::now() + std::chrono::seconds(60);
    while ((sent.load() < kPosts || woken == 0) && Clock::now() < limit)
        if (source->wait_until(Clock::now() + std::chrono::milliseconds(5))) ++woken;
    for (auto& t : posters) t.join();
    CHECK(sent.load() == kPosts);
    CHECK(source->posts() == static_cast<std::uint64_t>(kPosts));
    CHECK(woken >= 1);
    // The host loop thread posts at deadlines.
    auto looped = std::make_shared<WakeSource>();
    for (int k = 0; k < 50; ++k) {
        const auto t0 = Clock::now();
        looped->post_at(t0 + std::chrono::milliseconds(1 + k % 3));
        CHECK(looped->wait_until(t0 + std::chrono::seconds(10)));
        CHECK(Clock::now() - t0 >= std::chrono::milliseconds(1 + k % 3));
    }
    CHECK(looped->posts() == 50);
    // Sources destroyed with timers pending (weak reference: never touched).
    for (int k = 0; k < 100; ++k) {
        auto gone = std::make_shared<WakeSource>();
        gone->post_at(Clock::now() + std::chrono::milliseconds(k % 5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return rt_test::finish("coro-wake-unit");
}
