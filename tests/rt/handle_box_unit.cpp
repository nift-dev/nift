// CP17/D-035 VM hook: counted handles boxed in refcounted heap values.
#include "rt/compare.h"
#include "rt/heap_account.h"
#include "rt/handles/handle_box.h"
#include "rt/handles/table.h"
#include "rt_test.h"

#include <vector>

using namespace nift::rt;
using namespace nift::rt::handles;

int main() {
    int cleaned = 0;
    std::vector<CompletionTicket> reaper;
    HandleTable t;
    KindSpec spec;
    spec.name = "process";
    spec.cleanup = [&](CleanupEvent& e) {
        ++cleaned;
        reaper.push_back(std::move(*e.ticket));
        return CleanupOutcome::pending();
    };
    t.register_kind(HandleKind::Process, spec);
    ContextId s;
    t.open_context(ContextKind::Script, kDetached, s);
    Value plain;
    t.create(s, HandleKind::Process, std::make_shared<int>(1), plain, CreateOptions{true});
    const std::size_t baseline = live_heap_bytes();
    {
        Value boxed = box_counted_handle(t, plain);
        CHECK(live_heap_bytes() == baseline + heap_footprint(boxed.heap()));
        CHECK(boxed.boxed_handle() && boxed.is_heap() && boxed.refcount() == 1);
        CHECK(boxed.handle_id() == plain.handle_id() && boxed.handle_generation() == plain.handle_generation());
        CHECK(equal(boxed, plain) && hash(boxed) == hash(plain));
        std::shared_ptr<void> out;
        CHECK(t.resolve(boxed.handle_identity(), HandleKind::Process, s, out) == HandleStatus::Ok);
        Value alias = boxed;           // a VM copy: refcount, no table traffic
        CHECK(boxed.refcount() == 2);
        CHECK(live_heap_bytes() == baseline + heap_footprint(boxed.heap()));
        Value other_isolate = share_into_isolate(t, boxed);
        CHECK(other_isolate.boxed_handle() && other_isolate.heap() != boxed.heap());
        CHECK(live_heap_bytes() == baseline + heap_footprint(boxed.heap()) + heap_footprint(other_isolate.heap()));
        boxed = Value();
        CHECK(cleaned == 0);           // alias and the other isolate's box still refer
        alias = Value();
        CHECK(cleaned == 0);
    }                                  // the last box dies: cleanup is scheduled, never awaited
    CHECK(live_heap_bytes() == baseline);
    CHECK(cleaned == 1 && reaper.size() == 1);
    std::shared_ptr<void> out;
    CHECK(t.resolve(plain, HandleKind::Process, s, out) == HandleStatus::Closed);
    CHECK(t.stats().pending_obligations == 1);
    reaper.back().complete(CleanupOutcome::completed());
    CHECK(t.stats().pending_obligations == 0 && t.close_context(s).failures.empty());
    return rt_test::finish("rt handle box unit");
}
