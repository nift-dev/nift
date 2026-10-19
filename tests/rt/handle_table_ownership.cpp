// CP17 typed handles: ownership, identity vs ownership, contexts, cleanup
// ordering and failing hooks, counted references and the native interface.
#include "rt/handles/handle_ref.h"
#include "rt/handles/native_access.h"
#include "rt/handles/table.h"
#include "rt_test.h"
#include "handle_test_support.h"

#include <string>
#include <vector>

using namespace handle_test;

void ownership_and_access() {
    Log log;  // declared first: hooks run until the table is destroyed
    HandleTable t;
    KindSpec process = logging_kind("process", log);
    process.detach_forbidden_code = "process.detach_in_build";
    t.register_typed_kind<Res>(HandleKind::Process, process);
    ContextId script, child, sibling, build, page, page_child;
    t.open_context(ContextKind::Script, kDetached, script);
    t.open_context(ContextKind::Script, script, child);
    t.open_context(ContextKind::Script, script, sibling);
    t.open_context(ContextKind::Build, kDetached, build);
    t.open_context(ContextKind::PageRender, build, page);
    t.open_context(ContextKind::Script, page, page_child);  // a script nested in a build is build-family
    Value h, hc, hb, hp, hpc;
    t.create(script, HandleKind::Process, res(1), h);
    t.create(child, HandleKind::Process, res(2), hc);
    t.create(build, HandleKind::Process, res(3), hb);
    t.create(page, HandleKind::Process, res(4), hp);
    t.create(page_child, HandleKind::Process, res(5), hpc);
    const HandleKind P = HandleKind::Process;
    // Build-family query (refuse a detached launch before starting anything).
    bool in_build = true;
    CHECK(t.context_in_build(script, in_build) == HandleStatus::Ok && !in_build);
    CHECK(t.context_in_build(child, in_build) == HandleStatus::Ok && !in_build);
    for (const ContextId c : {build, page, page_child}) {
        in_build = false;
        CHECK(t.context_in_build(c, in_build) == HandleStatus::Ok && in_build);
    }
    ContextId hook_ctx, rebuild;
    t.open_context(ContextKind::BuildHook, kDetached, hook_ctx);
    t.open_context(ContextKind::AutoRebuild, kDetached, rebuild);
    CHECK(t.context_in_build(hook_ctx, in_build) == HandleStatus::Ok && in_build);
    CHECK(t.context_in_build(rebuild, in_build) == HandleStatus::Ok && in_build);
    t.close_context(hook_ctx);
    in_build = false;
    CHECK(t.context_in_build(hook_ctx, in_build) == HandleStatus::ContextClosed && !in_build);
    CHECK(t.context_in_build(424242, in_build) == HandleStatus::UnknownContext);
    CHECK(t.context_in_build(kDetached, in_build) == HandleStatus::UnknownContext);
    t.close_context(rebuild);
    // Access: the owner and contexts nested in it; never siblings or parents.
    CHECK(serial_of(t, h, P, script) == 1 && serial_of(t, h, P, child) == 1);
    CHECK(serial_of(t, hc, P, script) == -1 && serial_of(t, hc, P, sibling) == -1);
    CHECK(serial_of(t, hb, P, page) == 3 && serial_of(t, hp, P, build) == -1);
    CHECK(serial_of(t, h, P, build) == -1 && serial_of(t, h, P, kAnyAccessor) == 1);
    std::shared_ptr<void> out;
    CHECK(t.resolve(hc, P, sibling, out) == HandleStatus::NotOwned && !out);
    // Identity is not ownership: copies of the value never own. Only the owner
    // releases; non-owners (even a nested context) are refused.
    Value copy1 = h, copy2 = copy1;
    CHECK(t.release(copy1, P, child) == HandleStatus::NotOwned);
    CHECK(t.release(copy2, P, sibling) == HandleStatus::NotOwned);
    CHECK(t.release(copy2, P, kDetached) == HandleStatus::NotOwned);
    CHECK(log.cleaned[1] == 0 && serial_of(t, h, P, script) == 1);
    // Detach: explicit, single step, names the current owner.
    CHECK(t.detach(h, P, child) == HandleStatus::NotOwned);
    CHECK(t.detach(h, P, script) == HandleStatus::Ok);
    CHECK(t.detach(h, P, script) == HandleStatus::NotOwned);  // already detached: no second step
    CHECK(t.detach(h, P, kDetached) == HandleStatus::NotOwned);
    ContextId owner = 7;
    CHECK(t.owner_of(copy1, P, owner) == HandleStatus::Ok && owner == kDetached);
    CHECK(serial_of(t, h, P, sibling) == 1 && serial_of(t, h, P, build) == 1);  // detached: no owner to check
    CHECK(t.transfer(h, P, kDetached, script) == HandleStatus::TransferForbidden);  // no adoption
    // Detach is forbidden in every build-family context (D-030).
    CHECK(t.detach(hb, P, build) == HandleStatus::DetachForbidden);
    CHECK(t.detach(hp, P, page) == HandleStatus::DetachForbidden);
    CHECK(t.detach(hpc, P, page_child) == HandleStatus::DetachForbidden);
    std::string error;
    describe_handle_failure(HandleStatus::DetachForbidden, "detach", P, hb, &t, error);
    CHECK(error.find("process.detach_in_build") != std::string::npos);
    // Transfer: explicit, single step; never out of the build family.
    CHECK(t.transfer(hc, P, script, sibling) == HandleStatus::NotOwned);  // wrong expected owner
    CHECK(t.transfer(hc, P, child, sibling) == HandleStatus::Ok);
    CHECK(t.transfer(hc, P, child, script) == HandleStatus::NotOwned);    // the first move already happened
    CHECK(t.owner_of(hc, P, owner) == HandleStatus::Ok && owner == sibling);
    CHECK(serial_of(t, hc, P, child) == -1 && serial_of(t, hc, P, sibling) == 2);
    CHECK(t.transfer(hp, P, page, build) == HandleStatus::Ok);           // page -> its build: stays in the build
    CHECK(t.transfer(hb, P, build, script) == HandleStatus::TransferForbidden);
    CHECK(t.transfer(hb, P, build, kDetached) == HandleStatus::TransferForbidden);
    CHECK(t.transfer(hb, P, build, build) == HandleStatus::Ok);
    // Closing the child cleans nothing (it no longer owns hc); closing the
    // sibling cleans hc exactly once; the detached handle survives both.
    CloseReport r = t.close_context(child);
    CHECK(r.cleaned == 0 && log.cleaned[2] == 0);
    CHECK(t.transfer(hb, P, build, child) == HandleStatus::ContextClosed);
    r = t.close_context(sibling);
    CHECK(r.cleaned == 1 && log.cleaned[2] == 1);
    r = t.close_context(page);  // page_child goes with it; hp was moved to the build
    CHECK(r.contexts == 2 && r.cleaned == 1 && log.cleaned[5] == 1 && log.cleaned[4] == 0);
    r = t.close_context(script);
    CHECK(r.cleaned == 0 && log.cleaned[1] == 0 && serial_of(t, h, P, kAnyAccessor) == 1);
    r = t.close_context(build);
    CHECK(r.cleaned == 2 && log.cleaned[3] == 1 && log.cleaned[4] == 1);
    r = t.shutdown();
    CHECK(r.cleaned == 1 && log.cleaned[1] == 1 && log.events.back() == "1:shutdown");
    CHECK(t.shutdown().cleaned == 0);  // idempotent
    for (const auto& [serial, n] : log.cleaned) CHECK(n == 1);
}

void contexts_and_cleanup() {
    Log log;  // declared first: hooks run until the table is destroyed
    HandleTable t;
    t.register_typed_kind<Res>(HandleKind::File, logging_kind("file", log));
    // A kind whose hook resolves an older handle of the same context: LIFO
    // order keeps the older handle live while the newer one is cleaned.
    Value first;
    ContextId build = 0;
    int resolved_in_hook = -2, created_in_hook = -2;
    KindSpec stream;
    stream.name = "stream";
    stream.cleanup = [&](const CleanupEvent& e) {
        resolved_in_hook = serial_of(t, first, HandleKind::File, e.owner);
        Value h;
        created_in_hook = static_cast<int>(t.create(e.owner, HandleKind::File, res(77), h));
        log.events.push_back("stream:" + std::string(release_reason_name(e.reason)));
        return CleanupOutcome::success();
    };
    t.register_typed_kind<Res>(HandleKind::Custom, stream);
    ContextId page_a, page_b, hook;
    CHECK(t.open_context(ContextKind::Build, kDetached, build) == HandleStatus::Ok);
    t.open_context(ContextKind::PageRender, build, page_a);
    t.open_context(ContextKind::PageRender, build, page_b);
    t.open_context(ContextKind::BuildHook, page_a, hook);
    Value v;
    t.create(build, HandleKind::File, res(1), first);
    t.create(page_a, HandleKind::File, res(2), v);
    t.create(page_a, HandleKind::File, res(9001), v);   // hook reports failure
    t.create(page_b, HandleKind::File, res(3), v);
    t.create(hook, HandleKind::File, res(9101), v);     // hook throws std::exception
    t.create(hook, HandleKind::File, res(4), v);
    t.create(build, HandleKind::File, res(9201), v);    // hook throws a non-exception
    t.create(build, HandleKind::Custom, res(5), v);
    t.create(build, HandleKind::File, res(6), v);
    // A failing hook on an explicit release is reported to the caller and in
    // the owner's close report (D-035); the slot is reclaimed anyway.
    Value doomed;
    t.create(page_b, HandleKind::File, res(9002), doomed);
    CleanupFailure failure;
    CHECK(t.release(doomed, HandleKind::File, page_b, &failure) == HandleStatus::CleanupFailed);
    CHECK(failure.code == "process.kill_failed" && failure.reason == ReleaseReason::Explicit &&
          failure.owner == page_b && failure.message == "kill refused");
    CHECK(t.release(doomed, HandleKind::File, page_b) == HandleStatus::Closed);
    CHECK(t.close(doomed, HandleKind::File, page_b) == HandleStatus::Ok);  // close() is idempotent
    log.events.clear();
    // Opening under a closing or closed context fails.
    CloseReport r = t.close_context(build);
    CHECK(r.status == HandleStatus::Ok && r.contexts == 4 && r.cleaned == 9);
    const std::vector<std::string> expected = {
        "3:context-end",                                   // page_b (opened last) first
        "4:context-end", "9101:context-end",               // hook (nested in page_a), newest first
        "9001:context-end", "2:context-end",               // page_a
        "6:context-end", "stream:context-end", "9201:context-end", "1:context-end"};  // build itself
    CHECK(log.events == expected);
    CHECK(resolved_in_hook == 1);  // the older handle was still live during the newer one's hook
    CHECK(created_in_hook == static_cast<int>(HandleStatus::ContextClosed));
    CHECK(r.failures.size() == 4 && r.cleanup_failed() && r.final_exit_status(0) == 1);
    if (r.failures.size() == 4) {  // per context in close order, each in settle order
        CHECK(r.failures[0].code == "process.kill_failed" && r.failures[0].reason == ReleaseReason::Explicit &&
              r.failures[0].owner == page_b);
        CHECK(r.failures[1].code == "internal.handle_cleanup_threw" && r.failures[1].message == "hook threw" &&
              r.failures[1].state == LifeState::Lost);  // std::exception: outcome unknown
        CHECK(r.failures[2].code == "process.kill_failed" && r.failures[2].reason == ReleaseReason::ContextEnd);
        CHECK(r.failures[3].code == "internal.handle_cleanup_threw" && r.failures[3].message == "unknown exception");
        CHECK(r.failures[3].owner == build && r.failures[3].kind == HandleKind::File);
        CHECK(r.failures[3].state == LifeState::Lost);              // a throwing hook: outcome unknown
        CHECK(r.failures[0].state == LifeState::CleanupIncomplete);  // failure(): never "alive"
    }
    // Everything about the ended contexts now fails closed.
    for (const ContextId c : {build, page_a, page_b, hook}) {
        CHECK(t.create(c, HandleKind::File, res(8), v) == HandleStatus::ContextClosed);
        CHECK(t.close_context(c).status == HandleStatus::ContextClosed);
        ContextId x;
        CHECK(t.open_context(ContextKind::Script, c, x) == HandleStatus::ContextClosed);
    }
    CHECK(t.close_context(999999).status == HandleStatus::UnknownContext);
    std::shared_ptr<void> out;
    CHECK(t.resolve(first, HandleKind::File, kAnyAccessor, out) == HandleStatus::Closed);
    TableStats st = t.stats();
    CHECK(st.live == 0 && st.contexts == 0);
    // Child closed explicitly before its parent; the parent then has only its own.
    ContextId s, c1;
    t.open_context(ContextKind::Script, kDetached, s);
    t.open_context(ContextKind::Script, s, c1);
    t.create(c1, HandleKind::File, res(20), v);
    t.create(s, HandleKind::File, res(21), v);
    CHECK(t.close_context(c1).cleaned == 1 && t.close_context(s).cleaned == 1 && t.close_context(s).cleaned == 0);
    // The destructor ends whatever is left.
    Log tail_log;
    {
        HandleTable d;
        d.register_typed_kind<Res>(HandleKind::File, logging_kind("file", tail_log));
        ContextId ds;
        d.open_context(ContextKind::Script, kDetached, ds);
        d.create(ds, HandleKind::File, res(30), v);
        d.create(ds, HandleKind::File, res(31), v);
        d.detach(v, HandleKind::File, ds);
    }
    CHECK(tail_log.events.size() == 2 && tail_log.events[0] == "30:context-end" && tail_log.events[1] == "31:shutdown");
}

void counted_references() {
    Log log;  // declared first: hooks run until the table is destroyed
    HandleTable t;
    t.register_typed_kind<Res>(HandleKind::Process, logging_kind("process", log));
    const HandleKind P = HandleKind::Process;
    ContextId s;
    t.open_context(ContextKind::Script, kDetached, s);
    Value plain, counted;
    t.create(s, P, res(1), plain);
    CHECK(t.create(s, P, res(2), counted, CreateOptions{true}) == HandleStatus::Ok);
    CHECK(t.retain(plain) == HandleStatus::NotCounted && t.drop(plain) == HandleStatus::NotCounted);
    CHECK(t.retain(Value::integer(3)) == HandleStatus::NotAHandle);
    {
        HandleRef a = HandleRef::adopt(t, counted);   // the creator's reference
        HandleRef b = a;                              // a second reference to the same resource
        HandleRef c = HandleRef::share(t, counted);
        CHECK(a && b && c);
        // Dropping references never cleans up while another reference lives.
        a.reset();
        b.reset();
        CHECK(log.cleaned[2] == 0 && serial_of(t, counted, P, s) == 2);
        HandleRef moved = std::move(c);
        CHECK(!c && moved);
        CHECK(log.cleaned[2] == 0);
    }  // last reference gone: abandoned, cleaned exactly once
    CHECK(log.cleaned[2] == 1 && log.events.back() == "2:abandoned");
    CHECK(t.drop(counted) == HandleStatus::Closed && !HandleRef::share(t, counted));
    // Owner release with exactly the creator's reference left consumes it.
    Value solo;
    t.create(s, P, res(3), solo, CreateOptions{true});
    CHECK(t.release(solo, P, s) == HandleStatus::Ok && log.cleaned[3] == 1 && t.drop(solo) == HandleStatus::Closed);
    // Context end cleans counted handles regardless of references; the
    // references then hold a stale identity, and dropping them is harmless.
    Value held;
    t.create(s, P, res(4), held, CreateOptions{true});
    HandleRef keep = HandleRef::adopt(t, held);
    HandleRef keep2 = keep;
    // An abandoned handle whose hook fails is reported in the owner's close report.
    Value failing;
    t.create(s, P, res(9003), failing, CreateOptions{true});
    CHECK(t.drop(failing) == HandleStatus::Ok && log.cleaned[9003] == 1);
    CloseReport r = t.close_context(s);
    CHECK(r.cleaned == 2 && log.cleaned[4] == 1 && log.cleaned[1] == 1);
    CHECK(r.failures.size() == 1 && r.failures[0].reason == ReleaseReason::Abandoned &&
          r.failures[0].code == "process.kill_failed");
    keep.reset();
    keep2.reset();
    CHECK(log.cleaned[4] == 1);
    // D-039: a detached handle's hook only releases bookkeeping; its reported
    // result is ignored (Nift does not supervise the resource). Only what Nift
    // actually knows (here: a hook that threw) reaches shutdown(), separately,
    // and never changes the exit status.
    ContextId s2;
    t.open_context(ContextKind::Script, kDetached, s2);
    t.create(s2, P, res(9004), failing, CreateOptions{true});   // its hook reports a failure: ignored
    t.detach(failing, P, s2);
    t.drop(failing);
    Value throwing;
    t.create(s2, P, res(9102), throwing, CreateOptions{true});  // its hook throws: known, reported
    t.detach(throwing, P, s2);
    t.drop(throwing);
    TableStats st = t.stats();
    CHECK(st.records == 0 && st.pending_obligations == 0 && st.detached == 0);  // nothing kept for a report
    r = t.shutdown();
    CHECK(r.failures.empty() && r.detached_failures.size() == 1 && r.final_exit_status(0) == 0);
    if (r.detached_failures.size() == 1) {
        CHECK(r.detached_failures[0].owner == kDetached && r.detached_failures[0].reason == ReleaseReason::Abandoned &&
              r.detached_failures[0].code == "internal.handle_cleanup_threw");
    }
    for (const auto& [serial, n] : log.cleaned) CHECK(n == 1);
}

void native_interface() {
    Log log;  // declared first: hooks run until the table is destroyed
    HandleTable t;
    t.register_typed_kind<Res>(HandleKind::File, logging_kind("file", log));
    ContextId s, other;
    t.open_context(ContextKind::Script, kDetached, s);
    t.open_context(ContextKind::Script, kDetached, other);
    HandleAccess access{&t, s};
    Value h;
    std::string error;
    CHECK(native_handle_result(access, "file", HandleKind::File, res(1), h, error) == HandleStatus::Ok);
    std::shared_ptr<Res> r;
    CHECK(native_handle_arg<Res>(access, "read", h, HandleKind::File, r, error) == HandleStatus::Ok && r->serial == 1);
    r.reset();
    HandleAccess foreign{&t, other};
    CHECK(native_handle_arg<Res>(foreign, "read", h, HandleKind::File, r, error) == HandleStatus::NotOwned && !r);
    CHECK(error.find("read: handle is not owned") == 0 && error.find("internal.handle_not_owned") != std::string::npos);
    CHECK(native_handle_arg<Res>(HandleAccess{}, "read", h, HandleKind::File, r, error) ==
          HandleStatus::UnknownContext);
    std::shared_ptr<Other> o;
    CHECK(native_handle_arg<Other>(access, "read", h, HandleKind::File, o, error) == HandleStatus::TypeMismatch);
    t.close_context(s);
    CHECK(native_handle_arg<Res>(access, "read", h, HandleKind::File, r, error) == HandleStatus::Closed);
    CHECK(native_handle_result(access, "file", HandleKind::File, res(2), h, error) == HandleStatus::ContextClosed);
    CHECK(error.find("internal.handle_context_closed") != std::string::npos);
}
