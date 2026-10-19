// Included by handle_lifecycle_unit.cpp (same anonymous namespace): the CP17
// adversarial-review findings (R1-R14), each as a deterministic test.

struct Plain { int v = 0; };

// R1: every close_context caller waits; failures go to the close that owns
// the context; a second caller learns it was closed elsewhere.
void r1_close_ownership(int rounds) {
    {   // a second close of a context another close owns waits for its hooks
        std::atomic<bool> in_hook{false}, allow{false}, hook_done{false};
        HandleTable t;
        KindSpec spec;
        spec.name = "slow";
        spec.cleanup = [&](CleanupEvent&) {
            in_hook = true;
            while (!allow) std::this_thread::yield();
            hook_done = true;
            return CleanupOutcome::completed();
        };
        t.register_kind(HandleKind::Timer, spec);
        ContextId parent, child;
        t.open_context(ContextKind::Script, kDetached, parent, "parent");
        t.open_context(ContextKind::Script, parent, child, "child");
        Value h;
        t.create(child, HandleKind::Timer, std::make_shared<Plain>(), h);
        std::thread first([&] { t.close_context(parent); });
        while (!in_hook) std::this_thread::yield();
        std::atomic<bool> returned{false};
        bool done_at_return = false;
        CloseReport second;
        std::thread waiter([&] { second = t.close_context(child); done_at_return = hook_done; returned = true; });
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        CHECK(!returned.load());
        allow = true;
        first.join();
        waiter.join();
        CHECK(done_at_return && second.status == HandleStatus::ContextClosed && second.closed_elsewhere);
    }
    // the child's close owns the child's failures, whatever the timing of a parent close
    int child_got = 0, parent_got = 0, elsewhere = 0;
    for (int i = 0; i < rounds; ++i) {
        Fixture f;
        ContextId child;
        f.t.open_context(ContextKind::Script, f.script, child, "child");
        Value h;
        f.spawn(h, child, false);
        CloseReport rc, rp;
        std::thread tc([&] { rc = f.t.close_context(child); });
        while (f.reaper.size() == 0) std::this_thread::yield();
        std::thread tp([&] { rp = f.t.close_context(f.script); });
        if (i % 2) std::this_thread::sleep_for(std::chrono::microseconds(200 * (i % 7)));
        f.reaper.settle_all(CleanupOutcome::lost("p.lost", "x"));
        tc.join();
        tp.join();
        child_got += static_cast<int>(rc.failures.size());
        parent_got += static_cast<int>(rp.failures.size());
        elsewhere += static_cast<int>(rp.descendants_closed_elsewhere);
    }
    // `elsewhere` counts the rounds in which the parent close found the child
    // still being closed (otherwise the child was already gone): both orders occur.
    CHECK(child_got == rounds && parent_got == 0 && elsewhere > 0 && elsewhere <= rounds);
}

// R2: resolve() never destroys the caller's previous value under the lock.
struct Wrapper {
    HandleRef inner;
};
void r2_resolve_out_outside_lock() {
    HandleTable t;
    t.register_kind(HandleKind::Timer, KindSpec{"outer", {}, {}, nullptr});
    t.register_kind(HandleKind::File, KindSpec{"file", {}, {}, nullptr});
    ContextId c;
    t.open_context(ContextKind::Script, kDetached, c, "s");
    Value inner, outer, other;
    t.create(c, HandleKind::File, std::make_shared<Plain>(), inner, CreateOptions{true});
    auto w = std::make_shared<Wrapper>();
    w->inner = HandleRef::adopt(t, inner);
    t.create(c, HandleKind::Timer, w, outer);
    t.create(c, HandleKind::Timer, std::make_shared<Plain>(), other);
    std::shared_ptr<void> o;
    t.resolve(outer, HandleKind::Timer, c, o);
    w.reset();
    t.release(outer, HandleKind::Timer, c);  // `o` holds the last Wrapper reference
    CHECK(t.resolve(other, HandleKind::Timer, c, o) == HandleStatus::Ok);  // the Wrapper dies outside the lock
    std::shared_ptr<void> out;
    CHECK(t.resolve(inner, HandleKind::File, c, out) == HandleStatus::Closed);  // its inner reference was the last
}

// R3: close_context / shutdown from inside a hook: a defined error, no deadlock.
void r3_reentrant_close() {
    HandleTable* tp = nullptr;
    ContextId parent = 0;
    HandleStatus from_close = HandleStatus::Ok, from_shutdown = HandleStatus::Ok;
    KindSpec spec;
    spec.name = "reentrant";
    spec.cleanup = [&](CleanupEvent&) {
        from_close = tp->close_context(parent).status;
        from_shutdown = tp->shutdown().status;
        return CleanupOutcome::completed();
    };
    HandleTable t;
    tp = &t;
    t.register_kind(HandleKind::Timer, spec);
    ContextId child;
    t.open_context(ContextKind::Script, kDetached, parent, "parent");
    t.open_context(ContextKind::Script, parent, child, "child");
    Value h;
    t.create(child, HandleKind::Timer, std::make_shared<Plain>(), h);
    CHECK(t.release(h, HandleKind::Timer, child) == HandleStatus::Ok);
    CHECK(from_close == HandleStatus::ReentrantClose && from_shutdown == HandleStatus::ReentrantClose);
    CHECK(std::string(handle_status_code(HandleStatus::ReentrantClose)) == "internal.handle_reentrant_close");
    CHECK(t.close_context(parent).contexts == 2);  // outside the hook it works
}

// R4: a synchronous failure through close() carries its own code and class.
void r4_sync_failure_codes() {
    struct Case { CleanupOutcome outcome; LifeState state; const char* code; const char* message; };
    const Case cases[] = {
        {CleanupOutcome::alive_after_kill("process.kill_failed", "pid 3 alive after SIGKILL"), LifeState::Failed,
         "process.kill_failed", "pid 3 alive after SIGKILL"},
        {CleanupOutcome{CleanupOutcome::Result::Failed, "", ""}, LifeState::Failed, "internal.handle_cleanup_failed",
         "still alive after forced termination"},
        {CleanupOutcome::cleanup_incomplete("", ""), LifeState::CleanupIncomplete, "internal.handle_cleanup_incomplete",
         "ended, but cleanup could not be completed"},
        {CleanupOutcome::lost("", ""), LifeState::Lost, "internal.handle_cleanup_lost",
         "termination could not be confirmed"},
    };
    for (const Case& c : cases) {
        HandleTable t;
        const CleanupOutcome outcome = c.outcome;
        KindSpec spec;
        spec.name = "file";
        spec.cleanup = [outcome](CleanupEvent&) { return outcome; };
        t.register_kind(HandleKind::File, spec);
        ContextId s;
        t.open_context(ContextKind::Script, kDetached, s, "script a.f");
        Value h, h2;
        t.create(s, HandleKind::File, std::make_shared<Plain>(), h);
        t.create(s, HandleKind::File, std::make_shared<Plain>(), h2);
        CleanupFailure f;
        CHECK(t.close(h, HandleKind::File, s, &f) == HandleStatus::CleanupFailed);
        CHECK(f.state == c.state && f.code == c.code && f.message == c.message && f.context_label == "script a.f");
        CHECK(handle_error_class(HandleStatus::CleanupFailed) == HandleErrorClass::Error);  // exit 1, never 70
        std::string error;
        CHECK(native_handle_close(HandleAccess{&t, s}, "close", h2, HandleKind::File, error) ==
              HandleStatus::CleanupFailed);
        CHECK(error.find(std::string("close: ") + c.message) == 0 && error.find(c.code) != std::string::npos);
        if (c.state != LifeState::Failed) CHECK(error.find("alive") == std::string::npos);
        CloseReport r = t.close_context(s);
        CHECK(r.failures.size() == 2 && r.final_exit_status(0) == 1);
        for (const CleanupFailure& x : r.failures) CHECK(x.code == c.code && x.state == c.state);
    }
}

// R5: detach is decided by what started termination, never by timing.
void r5_detach_settled() {
    Fixture f;
    std::string error;
    Value a, b, c, d;
    f.spawn(a);
    f.spawn(b);
    f.spawn(c);
    f.spawn(d);
    deadline(f.t, a, f.reaper, TerminationCause::Exited);
    f.reaper.settle_all(CleanupOutcome::completed());               // exited and reaped
    CHECK(f.t.detach_as(a, P, f.script) == HandleStatus::Ok);       // trivially
    deadline(f.t, b, f.reaper, TerminationCause::Exited);           // exited, reaping in progress
    CHECK(f.t.detach_as(b, P, f.script) == HandleStatus::Ok);
    f.t.drop(b);                                                    // the reaping ticket is still out
    CHECK(f.t.stats().pending_obligations == 1);
    f.reaper.settle_all(CleanupOutcome::cleanup_incomplete("process.reap_failed", ""));
    deadline(f.t, c, f.reaper, TerminationCause::Deadline);
    f.reaper.settle_all(CleanupOutcome::completed());               // killed by Nift, settled
    CHECK(f.t.detach_as(c, P, f.script) == HandleStatus::DetachFailed);
    ContextId other;
    f.t.open_context(ContextKind::Script, kDetached, other, "other");
    Value e;
    f.spawn(e, other);
    f.t.cancel_context(other);
    CHECK(native_handle_detach(HandleAccess{&f.t, other}, "detach", e, P, error) == HandleStatus::DetachFailed);
    CHECK(handle_error_class(HandleStatus::DetachFailed) == HandleErrorClass::Error &&
          error.find("runtime.detach_failed") != std::string::npos);
    f.t.drop(a);
    std::atomic<bool> done{false};
    std::thread settler([&] { while (!done) { f.reaper.settle_all(CleanupOutcome::completed()); std::this_thread::yield(); } });
    CloseReport r = f.t.shutdown();
    done = true;
    settler.join();
    CHECK(r.failures.empty());
    // b's known reaping failure is a detached failure: reported, no exit effect
    bool found = false;
    for (const CleanupFailure& x : r.detached_failures) found = found || x.code == "process.reap_failed";
    CHECK(found && r.final_exit_status(0) == 0);
}

// R7: started means "a ticket was issued"; table-internal causes are refused.
void r7_begin_termination() {
    Fixture f;
    Value h;
    f.spawn(h);
    for (const TerminationCause bad : {TerminationCause::None, TerminationCause::Close, TerminationCause::Abandoned,
                                       TerminationCause::ContextEnd, TerminationCause::Shutdown}) {
        CompletionTicket ticket;
        CHECK(f.t.begin_termination(h, P, bad, ticket) == HandleStatus::InvalidCause && !ticket);
    }
    LifeState state;
    TerminationCause cause;
    CHECK(f.t.lifecycle(h, P, state, cause) == HandleStatus::Ok && state == LifeState::Active);
    CompletionTicket ticket;
    TerminationStart info;
    CHECK(f.t.begin_termination(h, P, TerminationCause::Cancel, ticket, &info) == HandleStatus::Ok);
    CHECK(info.started && !info.detached_request && ticket);
    ticket.complete(CleanupOutcome::completed());
    Value d;
    f.spawn(d);
    f.t.detach_as(d, P, f.script);
    for (int i = 0; i < 3; ++i) {
        CompletionTicket none;
        TerminationStart req;
        CHECK(f.t.begin_termination(d, P, TerminationCause::Cancel, none, &req) == HandleStatus::Ok);
        CHECK(!req.started && req.detached_request && !none);
    }
}

// R9: a hook that throws after taking the ticket is reported.
void r9_throw_after_taking_ticket() {
    std::vector<CompletionTicket> q;
    KindSpec spec;
    spec.name = "c";
    spec.cleanup = [&](CleanupEvent& e) -> CleanupOutcome {
        q.push_back(std::move(*e.ticket));
        throw std::runtime_error("bug after scheduling");
    };
    HandleTable t;
    t.register_kind(HandleKind::Timer, spec);
    ContextId c;
    t.open_context(ContextKind::Script, kDetached, c, "s");
    Value h;
    t.create(c, HandleKind::Timer, std::make_shared<Plain>(), h);
    t.release(h, HandleKind::Timer, c);
    q.back().complete(CleanupOutcome::completed());
    CloseReport r = t.close_context(c);
    CHECK(r.failures.size() == 1 && r.final_exit_status(0) == 1);
    if (r.failures.size() == 1) {
        CHECK(r.failures[0].code == "internal.handle_cleanup_threw" && r.failures[0].message == "bug after scheduling" &&
              r.failures[0].state == LifeState::Lost);
    }
}

// R14: only counted handles detach; the last reference frees all bookkeeping.
void r14_detached_bookkeeping() {
    Fixture f;
    Value plain, counted;
    f.spawn(plain, 0, false);
    CHECK(f.t.detach_as(plain, P, f.script) == HandleStatus::NotCounted);
    f.spawn(counted);
    CHECK(f.t.detach_as(counted, P, f.script) == HandleStatus::Ok);
    TableStats st = f.t.stats();
    CHECK(st.detached == 1 && st.records == 2);
    f.t.drop(counted);
    st = f.t.stats();
    CHECK(st.detached == 0 && st.live == 1 && st.records == 1 && st.pending_obligations == 0);
    CHECK(f.reaper.detached_with_ticket == 0);  // the detached hook got no ticket: nothing supervised
    // Another detached last reference: again no ticket, nothing pending.
    Value d2;
    f.spawn(d2);
    CHECK(f.t.detach_as(d2, P, f.script) == HandleStatus::Ok);
    f.t.drop(d2);
    CHECK(f.reaper.detached_with_ticket == 0 && f.t.stats().pending_obligations == 0);
}

// R10: a refused transfer reclaims by default (D-035); into a build is allowed.
void r10_transfer_policy() {
    Fixture f;
    ContextId build, dead;
    f.t.open_context(ContextKind::Build, kDetached, build, "build");
    f.t.open_context(ContextKind::Script, kDetached, dead, "dead");
    f.t.close_context(dead);
    Value h, g;
    auto p = f.spawn(h, 0, false);
    CHECK(f.t.transfer(h, P, f.script, dead) == HandleStatus::ContextClosed);
    std::shared_ptr<void> out;
    CHECK(p->terminate_calls == 1 && f.t.resolve(h, P, f.script, out) == HandleStatus::Closed);
    f.reaper.settle_all(CleanupOutcome::completed());
    f.spawn(g, 0, false);
    CHECK(f.t.transfer(g, P, f.script, build) == HandleStatus::Ok);           // into the build family
    CHECK(f.t.transfer(g, P, build, f.script) == HandleStatus::TransferForbidden);  // never out of it
    std::thread settle([&] { while (f.reaper.size() == 0) std::this_thread::yield(); f.reaper.settle_all({}); });
    CHECK(f.t.close_context(build).cleaned == 1);
    settle.join();
}

// close_context waits for a hook another thread started for its handle, even
// when that cleanup carries no obligation (the resource had already settled:
// only the in-flight count keeps the context alive).
void close_waits_for_inflight_hook_unit() {
    std::atomic<bool> in_hook{false}, allow{false}, hook_done{false}, closed{false};
    bool done_before_close = false;
    HandleTable t;
    KindSpec spec;
    spec.name = "slow";
    spec.cleanup = [&](CleanupEvent&) {
        in_hook = true;
        while (!allow.load()) std::this_thread::yield();
        hook_done = true;
        return CleanupOutcome::success();
    };
    t.register_kind(HandleKind::Timer, spec);
    ContextId c;
    t.open_context(ContextKind::Script, kDetached, c);
    Value h;
    t.create(c, HandleKind::Timer, std::make_shared<Plain>(), h);
    CompletionTicket exited;
    CHECK(t.begin_termination(h, HandleKind::Timer, TerminationCause::Exited, exited) == HandleStatus::Ok);
    CHECK(exited.complete(CleanupOutcome::completed()));  // settled: closing it creates no obligation
    std::thread releaser([&] { t.release(h, HandleKind::Timer, c); });
    while (!in_hook.load()) std::this_thread::yield();
    CHECK(t.stats().pending_obligations == 0);
    std::thread closer([&] { t.close_context(c); done_before_close = hook_done.load(); closed = true; });
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(!closed.load());
    allow = true;
    releaser.join();
    closer.join();
    CHECK(closed.load() && done_before_close);
}

void review_findings() {
    r1_close_ownership(100);
    r2_resolve_out_outside_lock();
    r3_reentrant_close();
    r4_sync_failure_codes();
    r5_detach_settled();
    r7_begin_termination();
    r9_throw_after_taking_ticket();
    r14_detached_bookkeeping();
    r10_transfer_policy();
    close_waits_for_inflight_hook_unit();
}
