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
#include "rt/handles/table.h"
#include "rt_test.h"

#include <cstdlib>
#include <memory>
#include <new>
#include <stdexcept>

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

} // namespace

int main() {
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
