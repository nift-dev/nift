// CP17 typed handles: concurrent create / resolve / release / retain / drop /
// detach / transfer / close_context / shutdown. Run under TSan
// (make test-rt-handles-tsan) and ASan (make test-rt-handles-sanitize).
// Invariants checked:
//   * every resource whose handle was created is cleaned exactly once;
//   * a refused create destroys its resource and runs no hook;
//   * a successful resolve always yields the resource the handle identified
//     (never a newer resource in a reused slot);
//   * no resource outlives shutdown (no leak), no hook runs twice.
#include "rt/handles/handle_ref.h"
#include "rt/handles/table.h"
#include "rt_test.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

using namespace nift::rt;
using namespace nift::rt::handles;

namespace {

std::atomic<long> g_alive{0}, g_twice{0}, g_wrong{0}, g_cleaned{0}, g_settle_twice{0};

struct Res {
    long serial;
    std::atomic<int> cleaned{0};
    std::atomic<int> settled{0};   // final outcomes reported for this resource
    explicit Res(long s) : serial(s) { ++g_alive; }
    ~Res() { --g_alive; }
};

// Asynchronous completion (CP16's reaper): tickets are completed later by
// another thread; it keeps its own reference to the resource meanwhile.
struct Reaper {
    std::mutex mu;
    std::vector<std::pair<std::shared_ptr<Res>, CompletionTicket>> queue;
    void push(std::shared_ptr<Res> r, CompletionTicket t) {
        std::lock_guard<std::mutex> lock(mu);
        queue.emplace_back(std::move(r), std::move(t));
    }
    void drain() {
        std::vector<std::pair<std::shared_ptr<Res>, CompletionTicket>> work;
        { std::lock_guard<std::mutex> lock(mu); work.swap(queue); }
        for (auto& [r, t] : work) {
            if (r->settled.fetch_add(1) != 0) ++g_settle_twice;
            t.complete(r->serial % 13 == 0 ? CleanupOutcome::lost("test.lost", "unconfirmed")
                                           : CleanupOutcome::completed());
        }
    }
};
Reaper g_reaper;

KindSpec counting_kind() {
    KindSpec spec;
    spec.name = "stress";
    spec.cleanup = [](CleanupEvent& e) {
        auto r = std::static_pointer_cast<Res>(e.resource);
        if (r->cleaned.fetch_add(1) != 0) ++g_twice;
        ++g_cleaned;
        if (!e.ticket) return CleanupOutcome::pending();  // a deadline already started termination
        if (r->serial % 5 == 0) {                         // schedule; complete later on the reaper thread
            g_reaper.push(r, std::move(*e.ticket));
            return CleanupOutcome::pending();
        }
        if (r->settled.fetch_add(1) != 0) ++g_settle_twice;
        if (r->serial % 17 == 0) return CleanupOutcome::failure("test.fail", "failing hook");
        return CleanupOutcome::success();
    };
    return spec;
}

struct Entry { Value handle; long serial; ContextId owner; bool counted; };

// Shared board of handles every worker may act on concurrently. The board's
// own mutex only protects the vector; table calls are made outside it.
struct Board {
    std::mutex mu;
    std::vector<Entry> entries;
    std::vector<ContextId> contexts;
    bool pick(std::mt19937& rng, Entry& out) {
        std::lock_guard<std::mutex> lock(mu);
        if (entries.empty()) return false;
        out = entries[rng() % entries.size()];
        return true;
    }
    ContextId context(std::mt19937& rng) {
        std::lock_guard<std::mutex> lock(mu);
        return contexts.empty() ? kDetached : contexts[rng() % contexts.size()];
    }
    // Returns false and the evicted entry when the board is full.
    bool add(const Entry& e, Entry& evicted) {
        std::lock_guard<std::mutex> lock(mu);
        if (entries.size() < 384) { entries.push_back(e); return true; }
        Entry& slot = entries[static_cast<std::size_t>(e.serial) % entries.size()];
        evicted = slot;
        slot = e;
        return false;
    }
};

void worker(HandleTable& t, Board& board, std::atomic<long>& serials, std::atomic<bool>& stop, unsigned seed) {
    std::mt19937 rng(seed);
    const HandleKind K = HandleKind::File;
    std::vector<HandleRef> refs;
    while (!stop.load(std::memory_order_relaxed)) {
        Entry e;
        switch (rng() % 9) {
            case 0: {
                const ContextId c = board.context(rng);
                const long serial = ++serials;
                const bool counted = rng() % 3 == 0;
                Value h;
                if (t.create(c, K, std::make_shared<Res>(serial), h, CreateOptions{counted}) == HandleStatus::Ok) {
                    Entry evicted;
                    if (!board.add(Entry{h, serial, c, counted}, evicted)) {  // keep the table from filling up
                        ContextId owner = kDetached;
                        if (t.owner_of(evicted.handle, K, owner) == HandleStatus::Ok) t.release(evicted.handle, K, owner);
                    }
                    if (counted) refs.push_back(HandleRef::adopt(t, h));
                }
                break;
            }
            case 1: case 2:
                if (board.pick(rng, e)) {
                    std::shared_ptr<Res> r;
                    if (t.resolve_as<Res>(e.handle, K, kAnyAccessor, r) == HandleStatus::Ok && r->serial != e.serial)
                        ++g_wrong;
                }
                break;
            case 3: case 4:
                if (board.pick(rng, e)) {
                    ContextId owner = kDetached;
                    if (t.owner_of(e.handle, K, owner) == HandleStatus::Ok) t.release(e.handle, K, owner);
                }
                break;
            case 5:
                if (board.pick(rng, e) && e.counted) {
                    HandleRef extra = HandleRef::share(t, e.handle);
                    if (extra && rng() % 2) refs.push_back(std::move(extra));
                }
                break;
            case 6:
                if (!refs.empty()) {
                    const std::size_t i = rng() % refs.size();
                    refs[i] = std::move(refs.back());
                    refs.pop_back();  // drops one reference, maybe the last
                }
                break;
            case 7:
                if (board.pick(rng, e)) {
                    if (rng() % 2) {
                        t.detach(e.handle, K, e.owner);
                    } else {  // a deadline: termination starts while the handle stays open
                        std::shared_ptr<Res> r;
                        CompletionTicket ticket;
                        TerminationStart info;
                        if (t.resolve_as<Res>(e.handle, K, kAnyAccessor, r) == HandleStatus::Ok &&
                            t.begin_termination(e.handle, K, TerminationCause::Deadline, ticket, &info) ==
                                HandleStatus::Ok && ticket) {  // detached: a request, no ticket
                            g_reaper.push(r, std::move(ticket));
                        }
                    }
                }
                break;
            default:
                if (board.pick(rng, e)) t.transfer(e.handle, K, e.owner, board.context(rng));
                break;
        }
    }
}

void context_manager(HandleTable& t, Board& board, std::atomic<bool>& stop, long& closed_failures) {
    std::mt19937 rng(7);
    while (!stop.load(std::memory_order_relaxed)) {
        ContextId c = kDetached, parent = board.context(rng);
        const ContextKind kind = rng() % 2 ? ContextKind::Script : ContextKind::Build;
        if (rng() % 3 == 0 || t.open_context(kind, parent, c) != HandleStatus::Ok) {
            t.open_context(kind, kDetached, c);
        }
        {
            std::lock_guard<std::mutex> lock(board.mu);
            board.contexts.push_back(c);
            if (board.contexts.size() > 6) {
                const std::size_t i = rng() % board.contexts.size();
                c = board.contexts[i];
                board.contexts.erase(board.contexts.begin() + static_cast<long>(i));
            } else {
                c = kDetached;
            }
        }
        if (c != kDetached) closed_failures += static_cast<long>(t.close_context(c).failures.size());
        std::this_thread::yield();
    }
}

void random_stress(int seconds_ms, std::uint32_t capacity) {
    long refused_alive = 0, closed_failures = 0;
    {
        TableOptions options;
        options.capacity = capacity;
        HandleTable t(options);
        t.register_typed_kind<Res>(HandleKind::File, counting_kind());
        Board board;
        std::atomic<long> serials{0};
        std::atomic<bool> stop{false};
        std::vector<std::thread> threads;
        for (unsigned i = 0; i < 4; ++i)
            threads.emplace_back(worker, std::ref(t), std::ref(board), std::ref(serials), std::ref(stop), 100 + i);
        std::thread manager(context_manager, std::ref(t), std::ref(board), std::ref(stop), std::ref(closed_failures));
        std::atomic<bool> reaping{true};
        std::thread reaper([&] { while (reaping) { g_reaper.drain(); std::this_thread::yield(); } g_reaper.drain(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(seconds_ms));
        stop = true;
        for (std::thread& th : threads) th.join();
        manager.join();
        const TableStats before = t.stats();
        const CloseReport r = t.shutdown();  // waits for the reaper to settle every obligation
        reaping = false;
        reaper.join();
        const TableStats after = t.stats();
        CHECK(after.records == 0 && after.pending_obligations == 0 && after.settled == after.created);
        CHECK(after.live == 0 && after.contexts == 0 && after.detached == 0);
        CHECK(after.cleaned == after.created && after.created > 0);
        std::printf("stress: %llu created, %llu cleaned, %zu slots (cap %u), %zu live before shutdown, "
                    "%ld close failures, %zu shutdown failures\n",
                    static_cast<unsigned long long>(after.created), static_cast<unsigned long long>(after.cleaned),
                    before.slots, capacity, before.live, closed_failures, r.failures.size());
        CHECK(before.slots <= capacity);
        refused_alive = g_alive.load();  // board entries hold only Values: no resources
    }
    CHECK(g_twice == 0 && g_wrong == 0 && g_settle_twice == 0);
    CHECK(refused_alive == 0 && g_alive == 0);
}

// Release, last drop and context close race on one counted handle: exactly
// one of them cleans it up.
void exactly_once_race(int rounds) {
    Res* seen = nullptr;
    std::atomic<bool> reaping{true};
    std::thread reaper([&] { while (reaping) { g_reaper.drain(); std::this_thread::yield(); } });
    for (int i = 0; i < rounds; ++i) {
        HandleTable t;
        t.register_typed_kind<Res>(HandleKind::Process, counting_kind());
        ContextId c;
        t.open_context(ContextKind::Script, kDetached, c);
        auto res = std::make_shared<Res>(1 + 17 * i + 1);
        seen = res.get();
        Value h;
        t.create(c, HandleKind::Process, res, h, CreateOptions{true});
        t.retain(h);
        t.retain(h);
        std::atomic<int> go{0};
        auto wait = [&] { ++go; while (go.load() < 3) {} };
        std::thread a([&] { wait(); t.release(h, HandleKind::Process, c); });
        std::thread b([&] { wait(); t.drop(h); t.drop(h); t.drop(h); });
        std::thread d([&] { wait(); t.close_context(c); });
        a.join(); b.join(); d.join();
        CHECK(seen->cleaned.load() == 1);
        res.reset();
    }
    reaping = false;
    reaper.join();
    CHECK(g_twice == 0 && g_settle_twice == 0);
}


// close_context returns only after a hook another thread started for one of
// its handles (here: an explicit release) has finished.
void close_waits_for_inflight_hook() {
    std::atomic<bool> in_hook{false}, allow{false}, hook_done{false}, closed{false};
    bool done_before_close = false;
    {
        HandleTable t;
        KindSpec spec;
        spec.name = "slow";
        spec.cleanup = [&](const CleanupEvent&) {
            in_hook = true;
            while (!allow.load()) std::this_thread::yield();
            hook_done = true;
            return CleanupOutcome::success();
        };
        t.register_typed_kind<Res>(HandleKind::Timer, spec);
        ContextId c;
        t.open_context(ContextKind::Script, kDetached, c);
        Value h;
        t.create(c, HandleKind::Timer, std::make_shared<Res>(1), h);
        std::thread releaser([&] { t.release(h, HandleKind::Timer, c); });
        while (!in_hook.load()) std::this_thread::yield();
        std::thread closer([&] { t.close_context(c); done_before_close = hook_done.load(); closed = true; });
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        CHECK(!closed.load());
        allow = true;
        releaser.join();
        closer.join();
    }
    CHECK(closed.load() && done_before_close);
}

} // namespace

int main(int argc, char** argv) {
    const int ms = argc > 1 ? std::atoi(argv[1]) : 2000;
    const int rounds = argc > 2 ? std::atoi(argv[2]) : 500;
    random_stress(ms, 512);
    random_stress(ms / 4, 48);  // small table: the capacity limit is hit under contention
    exactly_once_race(rounds);
    close_waits_for_inflight_hook();
    return rt_test::finish("rt handle table stress");
}
