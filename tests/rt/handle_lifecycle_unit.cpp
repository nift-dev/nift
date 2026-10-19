// CP17 / D-035: authoritative lifecycle, asynchronous completion, counted
// references, detached processes, transfer, handle_closed, cleanup failure and
// the NIFT_HANDLE_LIMIT setting. Deterministic: a fake process kind whose hook
// behaves like CP16's (schedule termination, hand the ticket to a reaper,
// never block) and a reaper the test drives explicitly.
#include "rt/handles/handle_ref.h"
#include "rt/handles/limit_setting.h"
#include "rt/handles/native_access.h"
#include "rt/handles/table.h"
#include "rt_test.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace nift::rt;
using namespace nift::rt::handles;

namespace {

const HandleKind P = HandleKind::Process;

struct FakeProcess {
    std::atomic<int> terminate_calls{0};
    std::atomic<bool> alive{true};
};

// Holds the tickets of terminations in progress (the reaper's own ownership:
// it keeps the process after the script's last reference is gone).
struct Reaper {
    std::mutex mu;
    std::vector<std::pair<std::string, bool>> seen;  // (context label, context ending on error) per hook call
    std::vector<std::pair<std::shared_ptr<FakeProcess>, CompletionTicket>> queue;
    void schedule(std::shared_ptr<FakeProcess> p, CompletionTicket t) {
        p->terminate_calls++;  // "SIGTERM sent"
        std::lock_guard<std::mutex> lock(mu);
        queue.emplace_back(std::move(p), std::move(t));
    }
    std::size_t size() { std::lock_guard<std::mutex> lock(mu); return queue.size(); }
    // Settles every queued termination with `outcome` (the bounded procedure ended).
    int settle_all(const CleanupOutcome& outcome) {
        std::vector<std::pair<std::shared_ptr<FakeProcess>, CompletionTicket>> work;
        { std::lock_guard<std::mutex> lock(mu); work.swap(queue); }
        int settled = 0;
        for (auto& [p, t] : work) {
            if (outcome.result == CleanupOutcome::Result::Completed) p->alive = false;
            settled += t.complete(outcome) ? 1 : 0;
        }
        return settled;
    }
};

KindSpec process_kind(Reaper& reaper) {
    KindSpec spec;
    spec.name = "process";
    spec.detach_forbidden_code = "process.detach_in_build";
    spec.cleanup = [&reaper](CleanupEvent& e) {
        auto p = std::static_pointer_cast<FakeProcess>(e.resource);
        { std::lock_guard<std::mutex> lock(reaper.mu); reaper.seen.emplace_back(e.context_label, e.context_error); }
        if (!e.ticket) return CleanupOutcome::pending();           // termination already started: bookkeeping only
        if (e.owner == kDetached) return CleanupOutcome::completed();  // detached: survives, bookkeeping released
        if (!p->alive) return CleanupOutcome::completed();          // exited and reaped: nothing to terminate
        reaper.schedule(p, std::move(*e.ticket));                   // schedule; never block
        return CleanupOutcome::pending();
    };
    return spec;
}

// CP16's deadline / cancel path on an open handle.
bool deadline(HandleTable& t, const Value& h, Reaper& reaper, TerminationCause cause) {
    std::shared_ptr<FakeProcess> p;
    if (t.resolve_as<FakeProcess>(h, P, kAnyAccessor, p) != HandleStatus::Ok) return false;
    CompletionTicket ticket;
    TerminationStart info;
    if (t.begin_termination(h, P, cause, ticket, &info) != HandleStatus::Ok || !info.started) return false;
    reaper.schedule(p, std::move(ticket));
    return true;
}

struct Fixture {
    Reaper reaper;  // declared before the table: hooks use it until the table is gone
    HandleTable t;
    ContextId script = 0;
    Fixture() {
        t.register_typed_kind<FakeProcess>(P, process_kind(reaper));
        t.open_context(ContextKind::Script, kDetached, script);
    }
    // Nothing may hang at teardown: settle whatever shutdown schedules.
    ~Fixture() {
        std::atomic<bool> done{false};
        std::thread settler([&] { while (!done) { reaper.settle_all(CleanupOutcome::completed()); std::this_thread::yield(); } });
        t.shutdown();
        done = true;
        settler.join();
    }
    std::shared_ptr<FakeProcess> spawn(Value& h, ContextId owner = 0, bool counted = true) {
        auto p = std::make_shared<FakeProcess>();
        t.create(owner ? owner : script, P, p, h, CreateOptions{counted});
        return p;
    }
};

void aliasing_and_last_reference() {
    Fixture f;
    Value h;
    auto p = f.spawn(h);
    HandleRef a = HandleRef::adopt(f.t, h);
    HandleRef b = a, c = a;
    b.reset();  // one alias dropped: nothing happens
    LifeState state;
    TerminationCause cause;
    CHECK(f.t.lifecycle(h, P, state, cause) == HandleStatus::Ok && state == LifeState::Active);
    CHECK(p->terminate_calls == 0 && f.reaper.size() == 0);
    a.reset();
    c.reset();  // last reference: cleanup is scheduled, the drop does not wait
    CHECK(p->terminate_calls == 1 && f.reaper.size() == 1 && p->alive);
    std::shared_ptr<void> out;
    CHECK(f.t.resolve(h, P, f.script, out) == HandleStatus::Closed);  // count zero: closed at once ...
    TableStats st = f.t.stats();
    CHECK(st.pending_obligations == 1 && st.records == 1);           // ... but not yet reclaimed
    CHECK(f.reaper.settle_all(CleanupOutcome::completed()) == 1);
    st = f.t.stats();
    CHECK(st.pending_obligations == 0 && st.records == 0 && st.settled == 1);
    CloseReport r = f.t.close_context(f.script);
    CHECK(r.failures.empty() && r.final_exit_status(0) == 0);
}

// close_context waits for obligations (bounded by CP16); a drop never waits.
void close_waits_for_obligations() {
    Fixture f;
    Value h;
    f.spawn(h);
    CHECK(f.t.drop(h) == HandleStatus::Ok);
    std::atomic<bool> closed{false};
    CloseReport report;
    std::thread closer([&] { report = f.t.close_context(f.script); closed = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(!closed.load());  // the resource is still terminating (not a failure, not done)
    f.reaper.settle_all(CleanupOutcome::completed());
    closer.join();
    CHECK(closed.load() && report.failures.empty() && report.cleaned == 0 && report.contexts == 1);
}

void deadline_and_cancel_vs_release() {
    for (const TerminationCause cause : {TerminationCause::Deadline, TerminationCause::Cancel}) {
        Fixture f;
        Value h;
        auto p = f.spawn(h);
        // Deadline first: the handle stays open, the outcome stays on the resource.
        CHECK(deadline(f.t, h, f.reaper, cause));
        CHECK(!deadline(f.t, h, f.reaper, cause));  // a second request does not terminate again
        LifeState state;
        TerminationCause seen;
        CHECK(f.t.lifecycle(h, P, state, seen) == HandleStatus::Ok && state == LifeState::Terminating && seen == cause);
        std::shared_ptr<void> out;
        CHECK(f.t.resolve(h, P, f.script, out) == HandleStatus::Ok);  // timeout/cancel is not handle_closed
        f.t.drop(h);  // last reference while terminating: no second termination
        CHECK(p->terminate_calls == 1 && f.t.stats().pending_obligations == 1);
        f.reaper.settle_all(CleanupOutcome::completed());
        CHECK(f.t.stats().pending_obligations == 0 && f.t.stats().records == 0);
        // Release first: a later deadline finds a closed handle.
        Value h2;
        auto p2 = f.spawn(h2);
        f.t.drop(h2);
        CompletionTicket ticket;
        CHECK(f.t.begin_termination(h2, P, cause, ticket) == HandleStatus::Closed && !ticket);
        CHECK(p2->terminate_calls == 1);
        f.reaper.settle_all(CleanupOutcome::completed());
        // Deadline settled while the handle is open: close later finds nothing to terminate.
        Value h3;
        auto p3 = f.spawn(h3);
        deadline(f.t, h3, f.reaper, cause);
        f.reaper.settle_all(CleanupOutcome::completed());
        CHECK(f.t.lifecycle(h3, P, state, seen) == HandleStatus::Ok && state == LifeState::Completed);
        CHECK(f.t.close(h3, P, f.script) == HandleStatus::Ok && f.t.stats().records == 0 && p3->terminate_calls == 1);
        CHECK(f.t.close_context(f.script).failures.empty());
    }
}

// Threads race a deadline/cancel against the last release and close():
// exactly one termination, exactly one settle, no obligation left.
void races(int rounds) {
    int bad = 0;
    for (int i = 0; i < rounds; ++i) {
        Fixture f;
        Value h;
        auto p = f.spawn(h);
        const TerminationCause cause = i % 2 ? TerminationCause::Deadline : TerminationCause::Cancel;
        std::atomic<int> go{0};
        auto wait = [&] { ++go; while (go.load() < 3) std::this_thread::yield(); };
        std::thread a([&] { wait(); deadline(f.t, h, f.reaper, cause); });
        std::thread b([&] { wait(); f.t.drop(h); });
        std::thread c([&] { wait(); f.t.close(h, P, f.script); });
        a.join(); b.join(); c.join();
        bad += p->terminate_calls != 1;
        bad += f.reaper.settle_all(CleanupOutcome::completed()) != 1;
        TableStats st = f.t.stats();
        bad += st.pending_obligations != 0 || st.records != 0 || st.settled != 1;
        bad += !f.t.close_context(f.script).failures.empty();
    }
    CHECK(bad == 0);
}

void detached_process_survives() {
    Fixture f;
    Value h;
    auto p = f.spawn(h);
    CHECK(f.t.detach_as(h, P, f.script) == HandleStatus::Ok);
    CHECK(f.t.detach_as(h, P, f.script) == HandleStatus::Ok);  // already detached
    CHECK(f.t.drop(h) == HandleStatus::Ok);                    // last reference
    CHECK(p->terminate_calls == 0 && p->alive && f.reaper.size() == 0);  // survives
    std::shared_ptr<void> out;
    CHECK(f.t.resolve(h, P, kAnyAccessor, out) == HandleStatus::Closed);  // bookkeeping released
    TableStats st = f.t.stats();
    CHECK(st.detached == 0 && st.records == 0 && st.pending_obligations == 0);
    CHECK(f.t.shutdown().failures.empty() && p->alive && p->terminate_calls == 0);
    // Detach inside a build is refused up front and at detach time.
    Fixture g;
    ContextId build, page;
    g.t.open_context(ContextKind::Build, kDetached, build);
    g.t.open_context(ContextKind::PageRender, build, page);
    Value bh;
    g.spawn(bh, page, false);
    std::string error;
    CHECK(native_handle_detach(HandleAccess{&g.t, page}, "detach", bh, P, error) == HandleStatus::DetachForbidden);
    CHECK(error.find("process.detach_in_build") != std::string::npos);
    std::thread settle([&] {
        while (g.reaper.size() == 0) std::this_thread::yield();
        g.reaper.settle_all(CleanupOutcome::completed());
    });
    CHECK(g.t.close_context(build).failures.empty());  // managed: terminated at context end
    settle.join();
}

void context_end_fallback() {
    Fixture f;
    Value h;
    auto p = f.spawn(h);
    HandleRef keep = HandleRef::adopt(f.t, h);
    HandleRef keep2 = keep;  // still referenced at context end
    std::thread settle([&] {
        while (f.reaper.size() == 0) std::this_thread::yield();
        f.reaper.settle_all(CleanupOutcome::completed());
    });
    CloseReport r = f.t.close_context(f.script);
    settle.join();
    CHECK(r.cleaned == 1 && r.failures.empty() && p->terminate_calls == 1 && !p->alive);
    keep.reset();
    keep2.reset();  // dropping references to a closed handle does nothing
    CHECK(p->terminate_calls == 1 && f.t.stats().settled == 1);
}

CloseReport close_with(Fixture& f, const CleanupOutcome& outcome) {
    std::thread settle([&] {
        while (f.reaper.size() == 0) std::this_thread::yield();
        f.reaper.settle_all(outcome);
    });
    CloseReport r = f.t.close_context(f.script);
    settle.join();
    return r;
}

void cleanup_failures() {
    {   // success
        Fixture f;
        Value h;
        f.spawn(h, 0, false);
        CloseReport r = close_with(f, CleanupOutcome::completed());
        CHECK(!r.cleanup_failed() && r.final_exit_status(0) == 0);
    }
    {   // Failed (alive after forced termination): nonzero; an earlier error and a signal status stay primary
        Fixture f;
        Value h;
        f.spawn(h, 0, false);
        CloseReport r = close_with(f, CleanupOutcome::alive_after_kill("process.kill_failed", "pid 9 alive after SIGKILL"));
        CHECK(r.failures.size() == 1 && r.failures[0].state == LifeState::Failed &&
              r.failures[0].code == "process.kill_failed" && r.failures[0].reason == ReleaseReason::ContextEnd);
        CHECK(r.final_exit_status(0) == 1 && r.final_exit_status(1) == 1 && r.final_exit_status(143) == 143 &&
              r.final_exit_status(130) == 130);
    }
    {   // Lost: termination could not be confirmed (never "still running")
        Fixture f;
        Value h;
        f.spawn(h, 0, false);
        CloseReport r = close_with(f, CleanupOutcome::lost("", ""));
        CHECK(r.failures.size() == 1 && r.failures[0].state == LifeState::Lost);
        CHECK(r.failures[0].code == "internal.handle_cleanup_lost" &&
              r.failures[0].message == "termination could not be confirmed");
        CHECK(r.failures[0].message.find("running") == std::string::npos && r.final_exit_status(0) == 1);
    }
    {   // an asynchronous failure after the last drop shows up in the context's final report
        Fixture f;
        Value h;
        f.spawn(h);
        f.t.drop(h);
        CHECK(f.t.stats().pending_obligations == 1);
        f.reaper.settle_all(CleanupOutcome::failure("process.kill_failed", "EPERM"));
        CloseReport r = f.t.close_context(f.script);
        CHECK(r.failures.size() == 1 && r.failures[0].reason == ReleaseReason::Abandoned &&
              r.failures[0].cause == TerminationCause::Abandoned);
    }
    {   // a deadline termination that fails while the handle is open is the context's failure too
        Fixture f;
        Value h;
        f.spawn(h, 0, false);
        deadline(f.t, h, f.reaper, TerminationCause::Deadline);
        f.reaper.settle_all(CleanupOutcome::lost("process.lost", "process did not exit after SIGKILL"));
        CloseReport r = f.t.close_context(f.script);
        CHECK(r.failures.size() == 1 && r.failures[0].cause == TerminationCause::Deadline &&
              r.failures[0].state == LifeState::Lost && r.cleaned == 1);
    }
    {   // reports nobody completes: a hook returning pending() without taking
        // the ticket, and a reaper dropping its ticket, settle as unresolved
        Fixture f;
        KindSpec forgetful;
        forgetful.name = "forgetful";
        forgetful.cleanup = [](CleanupEvent&) { return CleanupOutcome::pending(); };
        f.t.register_typed_kind<FakeProcess>(HandleKind::Timer, forgetful);
        Value a, b;
        f.t.create(f.script, HandleKind::Timer, std::make_shared<FakeProcess>(), a);
        f.spawn(b, 0, false);
        f.t.close(b, P, f.script);
        { std::lock_guard<std::mutex> lock(f.reaper.mu); f.reaper.queue.clear(); }  // the ticket is dropped
        CloseReport r = f.t.close_context(f.script);
        CHECK(r.failures.size() == 2);
        for (const CleanupFailure& x : r.failures) {
            CHECK(x.code == "internal.handle_cleanup_unresolved" && x.state == LifeState::Lost);
        }
    }
    {   // a ticket settles exactly once
        Fixture f;
        Value h;
        f.spawn(h, 0, false);
        CompletionTicket ticket;
        f.t.begin_termination(h, P, TerminationCause::Exited, ticket);
        CompletionTicket moved = std::move(ticket);
        CHECK(!ticket && !ticket.complete(CleanupOutcome::completed()));
        CHECK(moved.complete(CleanupOutcome::completed()) && !moved.complete(CleanupOutcome::completed()));
        CHECK(f.t.close_context(f.script).failures.empty());
    }
}

// Settles racing the context close: the report is complete either way.
void settle_close_races(int rounds) {
    int bad = 0;
    for (int i = 0; i < rounds; ++i) {
        Fixture f;
        Value h1, h2;
        f.spawn(h1);
        f.spawn(h2, 0, false);
        f.t.drop(h1);
        std::thread settler([&] {
            for (int k = 0; k < 2;) {
                k += f.reaper.settle_all(i % 3 ? CleanupOutcome::completed() : CleanupOutcome::failure("x.fail", "f"));
                std::this_thread::yield();
            }
        });
        CloseReport r = f.t.close_context(f.script);
        settler.join();
        bad += r.failures.size() != (i % 3 ? 0u : 2u) || f.t.stats().pending_obligations != 0;
    }
    CHECK(bad == 0);
}

#include "handle_lifecycle_unit_more.inc"

} // namespace

int main() {
    aliasing_and_last_reference();
    close_waits_for_obligations();
    deadline_and_cancel_vs_release();
    races(300);
    detached_process_survives();
    context_end_fallback();
    cleanup_failures();
    settle_close_races(200);
    handle_closed_identity();
    transfers(200);
    limit_setting();
    context_label_and_error();
    settled_states();
    return rt_test::finish("rt handle lifecycle unit");
}
