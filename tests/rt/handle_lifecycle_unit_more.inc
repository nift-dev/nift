// Included by handle_lifecycle_unit.cpp (same anonymous namespace):
// runtime.handle_closed identity, host transfer, NIFT_HANDLE_LIMIT.

void handle_closed_identity() {
    Fixture f;
    Value h;
    f.spawn(h, 0, false);
    std::shared_ptr<void> out;
    // explicit close
    CHECK(f.t.close(h, P, f.script) == HandleStatus::Ok);
    CHECK(f.t.close(h, P, f.script) == HandleStatus::Ok);  // idempotent
    CHECK(f.t.release(h, P, f.script) == HandleStatus::Closed);
    CHECK(f.t.resolve(h, P, f.script, out) == HandleStatus::Closed && !out);
    CHECK(std::string(handle_status_code(HandleStatus::Closed)) == "runtime.handle_closed");
    CHECK(handle_error_class(HandleStatus::Closed) == HandleErrorClass::Recoverable);
    std::string error;
    std::shared_ptr<FakeProcess> fp;
    CHECK(native_handle_arg<FakeProcess>(HandleAccess{&f.t, f.script}, "wait", h, P, fp, error) ==
          HandleStatus::Closed);
    CHECK(error.find("wait: handle is closed") == 0 && error.find("runtime.handle_closed") != std::string::npos);
    CHECK(native_handle_close(HandleAccess{&f.t, f.script}, "close", h, P, error) == HandleStatus::Ok);
    // wrong kind and forgery stay fatal internal errors, even for a closed handle
    CHECK(f.t.resolve(h, HandleKind::File, f.script, out) == HandleStatus::KindMismatch);
    CHECK(f.t.resolve(Value::handle(P, h.handle_id(), h.handle_generation() + 1), P, f.script, out) ==
          HandleStatus::Forged);
    // registry reuse: the slot holds a new process, the old handle stays closed
    f.reaper.settle_all(CleanupOutcome::completed());
    Value h2;
    f.spawn(h2, 0, false);
    CHECK(std::uint32_t(h2.handle_id()) == std::uint32_t(h.handle_id()) && h2.handle_generation() > h.handle_generation());
    CHECK(f.t.resolve(h, P, f.script, out) == HandleStatus::Closed && f.t.resolve(h2, P, f.script, out) == HandleStatus::Ok);
    // automatic cleanup (last reference) -> closed
    Value h3;
    f.spawn(h3);
    f.t.drop(h3);
    CHECK(f.t.resolve(h3, P, f.script, out) == HandleStatus::Closed);
    // a cancellation racing close(): one termination, the handle ends closed
    Value h4;
    auto p4 = f.spawn(h4, 0, false);
    std::thread canceller([&] { deadline(f.t, h4, f.reaper, TerminationCause::Cancel); });
    f.t.close(h4, P, f.script);
    canceller.join();
    CHECK(p4->terminate_calls == 1 && f.t.resolve(h4, P, f.script, out) == HandleStatus::Closed);
    f.reaper.settle_all(CleanupOutcome::completed());
    std::thread settle([&] {  // h2 is still open: the context end schedules it
        while (f.reaper.size() == 0) std::this_thread::yield();
        f.reaper.settle_all(CleanupOutcome::completed());
    });
    CHECK(f.t.close_context(f.script).failures.empty());
    settle.join();
}

void transfers(int rounds) {
    {   // an async worker returns a handle: the host transfers it before the worker ends
        Fixture f;
        ContextId worker;
        f.t.open_context(ContextKind::Script, f.script, worker);
        Value h;
        auto p = f.spawn(h, worker);
        HandleRef result = HandleRef::adopt(f.t, h);
        CHECK(f.t.transfer(h, P, worker, f.script) == HandleStatus::Ok);
        CloseReport r = f.t.close_context(worker);
        CHECK(r.cleaned == 0 && p->terminate_calls == 0);
        ContextId owner = 0;
        CHECK(f.t.owner_of(h, P, owner) == HandleStatus::Ok && owner == f.script);
        CHECK(f.t.transfer(h, P, worker, f.script) == HandleStatus::NotOwned);  // single step: done once
        result.reset();
        CHECK(p->terminate_calls == 1);  // the parent's last reference
        f.reaper.settle_all(CleanupOutcome::completed());
        CHECK(f.t.transfer(h, P, f.script, f.script) == HandleStatus::Closed);  // stale handle
    }
    {   // cancelled or ended destination: refused; kept, or reclaimed safely
        Fixture f;
        ContextId src, dst;
        f.t.open_context(ContextKind::Script, f.script, src);
        f.t.open_context(ContextKind::Script, f.script, dst);
        CHECK(f.t.cancel_context(dst) == HandleStatus::Ok);
        Value h, g;
        auto p = f.spawn(h, src, false);
        CHECK(f.t.create(dst, P, std::make_shared<FakeProcess>(), g) == HandleStatus::ContextClosed);
        ContextId child;
        CHECK(f.t.open_context(ContextKind::Script, dst, child) == HandleStatus::ContextClosed);
        CHECK(f.t.transfer(h, P, src, dst) == HandleStatus::ContextClosed);
        ContextId owner = 0;
        CHECK(f.t.owner_of(h, P, owner) == HandleStatus::Ok && owner == src && p->terminate_calls == 0);
        CHECK(f.t.transfer(h, P, src, dst, TransferPolicy::ReclaimOnRefusal) == HandleStatus::ContextClosed);
        std::shared_ptr<void> out;
        CHECK(p->terminate_calls == 1 && f.t.resolve(h, P, src, out) == HandleStatus::Closed);
        f.reaper.settle_all(CleanupOutcome::completed());
        CHECK(f.t.close_context(dst).contexts == 1);
        Value k;
        auto pk = f.spawn(k, src, false);
        CHECK(f.t.transfer(k, P, src, dst, TransferPolicy::ReclaimOnRefusal) == HandleStatus::ContextClosed);
        CHECK(pk->terminate_calls == 1);  // ended destination: reclaimed
        f.reaper.settle_all(CleanupOutcome::completed());
        CHECK(f.t.close_context(f.script).failures.empty());
    }
    // transfer racing the source's context end: transferred or cleaned, never both
    int bad = 0, moved = 0;
    for (int i = 0; i < rounds; ++i) {
        Fixture f;
        ContextId src, dst;
        f.t.open_context(ContextKind::Script, f.script, src);
        f.t.open_context(ContextKind::Script, f.script, dst);
        Value h;
        auto p = f.spawn(h, src, false);
        std::atomic<int> go{0};
        std::atomic<bool> stop{false};
        HandleStatus ts = HandleStatus::Ok;
        std::thread reaper([&] { while (!stop) { f.reaper.settle_all(CleanupOutcome::completed()); std::this_thread::yield(); } });
        std::thread a([&] { ++go; while (go < 2) {} ts = f.t.transfer(h, P, src, dst); });
        std::thread b([&] { ++go; while (go < 2) {} f.t.close_context(src); });
        a.join(); b.join();
        ContextId owner = 0;
        const bool open = f.t.owner_of(h, P, owner) == HandleStatus::Ok;
        if (ts == HandleStatus::Ok) { ++moved; bad += !open || owner != dst || p->terminate_calls != 0; }
        else bad += open || p->terminate_calls != 1;
        f.t.close_context(f.script);
        stop = true;
        reaper.join();
        bad += p->terminate_calls != 1 || f.t.stats().records != 0;
    }
    CHECK(bad == 0);
    std::printf("transfer race: %d of %d transferred before the source ended\n", moved, rounds);
}

void limit_setting() {
    CHECK(parse_handle_limit(nullptr).valid && parse_handle_limit("").capacity == (1u << 20));
    CHECK(parse_handle_limit("64").capacity == 64 && parse_handle_limit("2K").capacity == 2048);
    CHECK(parse_handle_limit("1m").capacity == (1u << 20) && parse_handle_limit("4294967295").capacity == 0xFFFFFFFFu);
    for (const char* bad : {"0", "-1", "abc", "4294967296", "4G", "12 ", "1.5", "K", "4194304K"}) {
        const HandleLimitSetting s = parse_handle_limit(bad);
        CHECK(!s.valid && s.raw == bad);
    }
    setenv(kHandleLimitSetting, "3", 1);
    {
        Reaper reaper;
        HandleTable t(table_options_from_environment());
        t.register_typed_kind<FakeProcess>(P, process_kind(reaper));
        ContextId s;
        t.open_context(ContextKind::Script, kDetached, s);
        Value h;
        std::string error;
        for (int i = 0; i < 3; ++i) CHECK(t.create(s, P, std::make_shared<FakeProcess>(), h) == HandleStatus::Ok);
        CHECK(native_handle_result(HandleAccess{&t, s}, "run", P, std::make_shared<FakeProcess>(), h, error) ==
              HandleStatus::LimitExceeded);
        CHECK(error == "run: handle limit exceeded (3 handles, NIFT_HANDLE_LIMIT) (process, limit.exceeded)");
        std::thread settle([&] { while (reaper.size() < 3) std::this_thread::yield(); reaper.settle_all({}); });
        t.close_context(s);
        settle.join();
    }
    setenv(kHandleLimitSetting, "lots", 1);
    {
        Reaper reaper;
        HandleTable t(table_options_from_environment());
        t.register_typed_kind<FakeProcess>(P, process_kind(reaper));
        ContextId s;
        t.open_context(ContextKind::Script, kDetached, s);
        Value h;
        std::string error;
        CHECK(native_handle_result(HandleAccess{&t, s}, "run", P, std::make_shared<FakeProcess>(), h, error) ==
              HandleStatus::InvalidSetting);
        CHECK(error.find("NIFT_HANDLE_LIMIT='lots'") != std::string::npos &&
              error.find("runtime.invalid_argument") != std::string::npos);
    }
    unsetenv(kHandleLimitSetting);
}

// Cleanup events and close reports carry the context's label and whether it
// is ending on an error (one source of truth for CP16's warnings). Lost
// (unconfirmed) and Failed (known alive after SIGKILL) both force a nonzero
// exit; the original error stays primary.
void context_label_and_error() {
    Fixture f;
    ContextId build, page;
    f.t.open_context(ContextKind::Build, kDetached, build, "build");
    f.t.open_context(ContextKind::PageRender, build, page, "page 'index'");
    Value a, b, c;
    f.spawn(a, page, false);
    f.spawn(b, build, false);
    f.spawn(c, page, false);
    CHECK(f.t.close(c, P, page) == HandleStatus::Ok);  // explicit close: label, no error
    f.reaper.settle_all(CleanupOutcome::completed());
    // The page ends first and waits for its process before the build's is scheduled.
    std::thread settle([&] {
        const CleanupOutcome outcomes[] = {
            CleanupOutcome::lost("process.lost", "termination of pid 41 could not be confirmed"),
            CleanupOutcome::alive_after_kill("process.kill_failed", "pid 42 still alive after SIGKILL")};
        for (const CleanupOutcome& o : outcomes) {
            while (f.reaper.size() == 0) std::this_thread::yield();
            f.reaper.settle_all(o);
        }
    });
    CloseReport r = f.t.close_context(build, /*on_error=*/true);
    settle.join();
    CHECK(r.label == "build" && r.on_error && r.contexts == 2 && r.cleaned == 2);
    const std::vector<std::pair<std::string, bool>> expected = {
        {"page 'index'", false}, {"page 'index'", true}, {"build", true}};
    CHECK(f.reaper.seen == expected);
    CHECK(r.failures.size() == 2);
    if (r.failures.size() == 2) {
        CHECK(r.failures[0].state == LifeState::Lost && r.failures[0].context_label == "page 'index'" &&
              r.failures[0].code == "process.lost" && r.failures[0].detail == FailureDetail::Unconfirmed);
        CHECK(r.failures[1].state == LifeState::Failed && r.failures[1].context_label == "build" &&
              r.failures[1].message == "pid 42 still alive after SIGKILL" &&
              r.failures[1].detail == FailureDetail::AliveAfterKill);
    }
    CHECK(r.final_exit_status(0) == 1 && r.final_exit_status(1) == 1 && r.final_exit_status(143) == 143);
    // A normal end carries no error flag.
    f.reaper.seen.clear();
    ContextId s2;
    f.t.open_context(ContextKind::Script, kDetached, s2, "script a.f");
    Value d;
    f.spawn(d, s2, false);
    std::thread settle2([&] { while (f.reaper.size() < 1) std::this_thread::yield(); f.reaper.settle_all({}); });
    r = f.t.close_context(s2);
    settle2.join();
    CHECK(r.label == "script a.f" && !r.on_error && r.failures.empty() && r.final_exit_status(0) == 0);
    CHECK(f.reaper.seen.size() == 1 && f.reaper.seen[0] == std::make_pair(std::string("script a.f"), false));
}

// Known to have exited, but reaping / handle cleanup could not be completed:
// a cleanup failure (nonzero exit), never "alive" and never Lost.
void exited_but_cleanup_incomplete() {
    Fixture f;
    Value a, b, c;
    f.spawn(a, 0, false);
    f.spawn(b, 0, false);
    f.spawn(c, 0, false);
    deadline(f.t, a, f.reaper, TerminationCause::Exited);  // CP16 saw the exit
    f.reaper.settle_all(CleanupOutcome::exited_cleanup_incomplete("process.reap_failed", ""));
    // the other two cases side by side
    deadline(f.t, b, f.reaper, TerminationCause::Deadline);
    f.reaper.settle_all(CleanupOutcome::alive_after_kill("", ""));
    deadline(f.t, c, f.reaper, TerminationCause::Cancel);
    // A Lost outcome is always "unconfirmed", whatever detail the reporter passed.
    CleanupOutcome odd = CleanupOutcome::lost("", "");
    odd.detail = FailureDetail::ExitedCleanupIncomplete;
    f.reaper.settle_all(odd);
    CloseReport r = f.t.close_context(f.script);
    CHECK(r.failures.size() == 3 && r.final_exit_status(0) == 1 && r.final_exit_status(2) == 2);
    if (r.failures.size() == 3) {
        const CleanupFailure& x = r.failures[0];
        CHECK(x.state == LifeState::Failed && x.detail == FailureDetail::ExitedCleanupIncomplete &&
              x.cause == TerminationCause::Exited && x.code == "process.reap_failed" &&
              x.message == "exited, but cleanup could not be completed");
        CHECK(x.message.find("alive") == std::string::npos && x.message.find("confirm") == std::string::npos);
        CHECK(r.failures[1].detail == FailureDetail::AliveAfterKill && r.failures[1].state == LifeState::Failed &&
              r.failures[1].message == "still alive after forced termination" &&
              r.failures[1].code == "internal.handle_cleanup_failed");
        CHECK(r.failures[2].detail == FailureDetail::Unconfirmed && r.failures[2].state == LifeState::Lost &&
              r.failures[2].code == "internal.handle_cleanup_lost");
    }
    CHECK(std::string(failure_detail_name(FailureDetail::ExitedCleanupIncomplete)) == "exited-cleanup-incomplete");
    // A default code for an incomplete cleanup.
    Fixture g;
    Value d;
    g.spawn(d, 0, false);
    deadline(g.t, d, g.reaper, TerminationCause::Exited);
    g.reaper.settle_all(CleanupOutcome::exited_cleanup_incomplete("", "pid 7 exited; waitpid failed: ECHILD"));
    r = g.t.close_context(g.script);
    CHECK(r.failures.size() == 1 && r.failures[0].code == "internal.handle_cleanup_incomplete" &&
          r.failures[0].message == "pid 7 exited; waitpid failed: ECHILD");
}
