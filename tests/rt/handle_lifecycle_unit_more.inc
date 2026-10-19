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
