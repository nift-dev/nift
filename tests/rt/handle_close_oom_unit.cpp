// CP17 x CP18 integration defect (found 2026-10-27 by CP18's allocation
// campaign, tests/runtime2/cp18/cow_allocation.cpp, on the combined tree): an
// allocation failure inside HandleTable::close_context left the context
// marked Closing with no live closer, so the table's destructor
// (shutdown()) waited for it forever (nift_program_run never returned).
//
// Every allocation inside close_context is failed in turn (operator new
// countdown). After each injected failure:
//  - the exception reaches the caller (std::bad_alloc), and
//  - no context is left Closing without a closer: either it is still open
//    (create() in it succeeds; the close can be retried) or it is gone;
//  - a later close_context / shutdown() completes (no wait for an abandoned
//    close); no failure is reported twice.
#include "rt/handles/completion.h"
#include "rt/handles/table.h"
#include "rt_test.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {
long countdown = -1;  // >= 0: the allocation at which operator new fails
bool injected = false;
} // namespace

void* operator new(std::size_t n) {
    if (countdown >= 0 && countdown-- == 0) {
        injected = true;
        throw std::bad_alloc();
    }
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace nift::rt;
using namespace nift::rt::handles;

namespace {

struct Tree {
    HandleTable table;
    ContextId root = 0, child = 0, grandchild = 0, sibling = 0;
    Tree() {
        KindSpec spec;
        spec.name = "custom";
        spec.cleanup = [](CleanupEvent&) { return CleanupOutcome::failure("test.left_open", "left open"); };
        table.register_kind(HandleKind::Custom, spec);
        CHECK(table.open_context(ContextKind::Script, kDetached, root, "root") == HandleStatus::Ok);
        CHECK(table.open_context(ContextKind::Script, root, child, "child") == HandleStatus::Ok);
        CHECK(table.open_context(ContextKind::Script, child, grandchild, "grandchild") == HandleStatus::Ok);
        CHECK(table.open_context(ContextKind::Script, root, sibling, "sibling") == HandleStatus::Ok);
        // Each context holds a reported failure (closed before the campaign):
        // ending it appends that report (an allocation the fix must survive).
        // No handle is open while close_context runs: an allocation failure
        // inside a cleanup's own bookkeeping (run_cleanup/settle) is a
        // separate, reported qualification, not covered here.
        for (ContextId c : {root, child, grandchild, sibling}) {
            Value v;
            CHECK(table.create(c, HandleKind::Custom, std::make_shared<int>(1), v) == HandleStatus::Ok);
            CHECK(table.close(v, HandleKind::Custom, c) == HandleStatus::CleanupFailed);
        }
        // A cancelled context goes back to Cancelling (not Open) if a close of
        // it is abandoned.
        CHECK(table.cancel_context(sibling) == HandleStatus::Ok);
    }
    bool open(ContextId c) {
        Value v;
        if (table.create(c, HandleKind::Custom, std::make_shared<int>(2), v) != HandleStatus::Ok) return false;
        // Released again at once, so later closes still run no cleanup.
        return table.close(v, HandleKind::Custom, c) == HandleStatus::CleanupFailed;
    }
};

// Runs `body` (on this thread) under a watchdog: a body that does not return
// within 20 s is the witnessed defect (an in-flight count or obligation never
// released), reported with its section and site; the process then exits 3.
template <class F> void bounded(const char* section, long site, F body) {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    std::thread watchdog([&] {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(20), [&] { return done; })) {
            std::fprintf(stderr, "HANG section=%s site=%ld\n", section, site);
            std::fflush(stderr);
            _exit(3);
        }
    });
    body();
    {
        std::lock_guard<std::mutex> lock(mu);
        done = true;
    }
    cv.notify_all();
    watchdog.join();
}

// A table with one context holding `open` counted handles whose hook reports
// a failure (a file left open).
struct Open {
    HandleTable table;
    ContextId ctx = 0;
    std::vector<Value> handles;
    explicit Open(int open) {
        KindSpec spec;
        spec.name = "custom";
        spec.cleanup = [](CleanupEvent&) { return CleanupOutcome::failure("test.left_open", "left open"); };
        table.register_kind(HandleKind::Custom, spec);
        CHECK(table.open_context(ContextKind::Script, kDetached, ctx, "script") == HandleStatus::Ok);
        for (int k = 0; k < open; ++k) {
            Value v;
            CHECK(table.create(ctx, HandleKind::Custom, std::make_shared<int>(k), v, CreateOptions{true}) ==
                  HandleStatus::Ok);
            handles.push_back(v);
        }
    }
};

// Cleanup bookkeeping (run_cleanup / settle) under allocation failure: the
// last-reference drop path (a VM box dying) and the context-close path.
int cleanup_bookkeeping() {
    int sites = 0;
    for (const char* section : {"drop", "close"}) {
        bool completed = false;
        for (long i = 0; i < 10000 && !completed; ++i) {
            Open t(3);
            bool threw = false;
            injected = false;
            bounded(section, i, [&] {
                countdown = i;
                try {
                    if (section[0] == 'd') {
                        for (const Value& v : t.handles) CHECK(t.table.drop(v) == HandleStatus::Ok);
                    } else {
                        t.table.close_context(t.ctx);
                    }
                } catch (const std::bad_alloc&) {
                    threw = true;
                }
                countdown = -1;
            });
            if (!injected) {
                CHECK(!threw);
                completed = true;
            } else {
                ++sites;
                CHECK(threw);
            }
            // Whatever happened, ending the context and the table returns, no
            // obligation is left behind, and every slot is reclaimed.
            bounded(section, i, [&] {
                t.table.close_context(t.ctx);
                t.table.shutdown();
            });
            const TableStats st = t.table.stats();
            CHECK(st.live == 0 && st.contexts == 0 && st.pending_obligations == 0);
        }
        CHECK(completed);
    }
    return sites;
}

// create() under allocation failure: no live slot without its lifecycle record.
int create_sites() {
    int sites = 0;
    bool completed = false;
    for (long i = 0; i < 10000 && !completed; ++i) {
        Open t(1);
        const TableStats before = t.table.stats();
        Value v;
        bool threw = false;
        injected = false;
        countdown = i;
        try {
            t.table.create(t.ctx, HandleKind::Custom, std::shared_ptr<int>(std::make_shared<int>(9)), v,
                           CreateOptions{true});
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        countdown = -1;
        if (!injected) {
            completed = true;
            CHECK(!threw && t.table.stats().live == before.live + 1);
        } else {
            ++sites;
            CHECK(threw);
            CHECK(t.table.stats().live == before.live);  // nothing half-created
        }
        bounded("create", i, [&] { t.table.shutdown(); });
        CHECK(t.table.stats().live == 0 && t.table.stats().contexts == 0);
    }
    CHECK(completed);
    return sites;
}

// A ticket completed later (the CP16 reaper path) under allocation failure:
// the record must not keep its obligation (close_context would wait forever).
CompletionTicket* g_pending = nullptr;
int ticket_sites() {
    int sites = 0;
    bool completed = false;
    for (long i = 0; i < 10000 && !completed; ++i) {
        HandleTable table;
        CompletionTicket held;
        g_pending = &held;
        KindSpec spec;
        spec.name = "process";
        spec.cleanup = [](CleanupEvent& e) {
            *g_pending = std::move(*e.ticket);
            return CleanupOutcome::pending();
        };
        table.register_kind(HandleKind::Process, spec);
        ContextId ctx = 0;
        // A label beyond the small-string buffer, and an outcome without its own
        // code/message (the table fills in defaults): building the report
        // allocates before anything is recorded, as well as when it is stored.
        CHECK(table.open_context(ContextKind::Script, kDetached, ctx, "script context with a long label") ==
              HandleStatus::Ok);
        Value v;
        CHECK(table.create(ctx, HandleKind::Process, std::make_shared<int>(1), v) == HandleStatus::Ok);
        CHECK(table.close(v, HandleKind::Process, ctx) == HandleStatus::Ok);  // pending: an obligation
        CHECK(table.stats().pending_obligations == 1);
        CleanupOutcome outcome = CleanupOutcome::failure({}, {});
        bool threw = false;
        injected = false;
        countdown = i;
        try {
            held.complete(std::move(outcome));
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        countdown = -1;
        if (!injected) completed = true;
        else { ++sites; CHECK(threw); }
        CHECK(table.stats().pending_obligations == 0);
        bounded("ticket", i, [&] { table.close_context(ctx); table.shutdown(); });
    }
    CHECK(completed);
    return sites;
}

// Releasing a slot (SlotStore::take, the first step of every cleanup, reached
// from a dying VM box) must not allocate at all: with every allocation failing,
// dropping handles whose hook allocates nothing succeeds.
void release_is_allocation_free() {
    HandleTable table;
    KindSpec spec;
    spec.name = "quiet";
    spec.cleanup = [](CleanupEvent&) { return CleanupOutcome::completed(); };
    table.register_kind(HandleKind::Custom, spec);
    ContextId ctx = 0;
    CHECK(table.open_context(ContextKind::Script, kDetached, ctx, "script") == HandleStatus::Ok);
    std::vector<Value> handles;
    for (int k = 0; k < 40; ++k) {  // past every small-capacity boundary of the slot store
        Value v;
        CHECK(table.create(ctx, HandleKind::Custom, std::make_shared<int>(k), v, CreateOptions{true}) == HandleStatus::Ok);
        handles.push_back(v);
    }
    bool threw = false;
    injected = false;
    countdown = 0;  // the next allocation fails
    try {
        for (const Value& v : handles) table.drop(v);
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    countdown = -1;
    CHECK(!threw && !injected);
    CHECK(table.stats().live == 0);
    table.close_context(ctx);
}

} // namespace

int main() {
    const int creates = create_sites();
    CHECK(creates > 0);
    std::printf("create allocation failures: %d injected sites\n", creates);
    const int tickets = ticket_sites();
    CHECK(tickets > 0);
    std::printf("ticket completion allocation failures: %d injected sites\n", tickets);
    const int bookkeeping_sites = cleanup_bookkeeping();
    release_is_allocation_free();  // after the others: thread-local hook bookkeeping is warm
    CHECK(bookkeeping_sites > 0);
    std::printf("cleanup bookkeeping allocation failures: %d injected sites\n", bookkeeping_sites);
    int sites = 0;
    bool completed = false;
    for (long i = 0; i < 10000 && !completed; ++i) {
        Tree t;
        const std::size_t contexts_before = t.table.stats().contexts;
        CloseReport report;
        bool threw = false;
        injected = false;
        countdown = i;
        try {
            report = t.table.close_context(t.root);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        countdown = -1;
        if (!injected) {
            CHECK(!threw);
            CHECK(report.status == HandleStatus::Ok && report.contexts == 4 && report.failures.size() == 4);
            CHECK(t.table.stats().contexts == 0);
            completed = true;
            break;
        }
        ++sites;
        CHECK(threw);
        // No orphaned Closing context: each remaining context is still open
        // (accepts handles) or the close finished it.
        const std::size_t remaining = t.table.stats().contexts;
        CHECK(remaining <= contexts_before);
        std::size_t open = 0;
        for (ContextId c : {t.root, t.child, t.grandchild, t.sibling}) {
            HandleStatus status = HandleStatus::Ok;
            bool in_build = false;
            status = t.table.context_in_build(c, in_build);
            if (status == HandleStatus::Ok) {
                // A context left Closing would refuse this (ContextClosed); the
                // cancelled sibling must still refuse it (Cancelling, not Open).
                if (c == t.sibling) CHECK(!t.open(c) && t.table.cancel_context(c) == HandleStatus::Ok);
                else CHECK(t.open(c));
                ++open;
            }
        }
        CHECK(open == remaining);
        // The close can be completed; shutdown() (the destructor) must not wait.
        const CloseReport retry = t.table.close_context(t.root);
        CHECK(retry.status == HandleStatus::Ok || retry.status == HandleStatus::UnknownContext);
        const CloseReport final_report = t.table.shutdown();
        CHECK(t.table.stats().contexts == 0);
        // Reports that do arrive are intact and never duplicated (a failure
        // moved out by the aborted close is lost with its unreturned report,
        // never re-reported as an empty shell).
        std::uint64_t seen = 0;
        for (const CloseReport* r : {&retry, &final_report}) {
            for (const CleanupFailure& f : r->failures) {
                CHECK(f.code == "test.left_open" && f.message == "left open");
                CHECK(f.sequence > 0 && f.sequence < 64 && !(seen & (std::uint64_t(1) << f.sequence)));
                seen |= std::uint64_t(1) << f.sequence;
            }
        }
    }
    CHECK(completed);
    CHECK(sites > 0);
    std::printf("handle close allocation failures: %d injected sites, completed=%d\n", sites, int(completed));
    return rt_test::finish("rt handle close under allocation failure (CP17 x CP18)");
}
