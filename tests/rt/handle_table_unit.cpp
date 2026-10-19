// CP17 typed handles: unit tests for rt::handles::HandleTable (identity,
// forgery and staleness, generation wrap, reclamation, ownership, contexts,
// cleanup ordering and failures, counted references, native access) and the
// CP09 HandleRegistry status vocabulary.
#include "rt/handle.h"
#include "rt/handles/handle_ref.h"
#include "rt/handles/native_access.h"
#include "rt/handles/table.h"
#include "rt/string.h"
#include "rt_test.h"
#include "handle_test_support.h"

#include <cstring>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace nift::rt;
using namespace nift::rt::handles;

namespace {
using namespace handle_test;

Value with_generation(const Value& h, std::uint32_t generation) {
    return Value::handle(h.handle_kind(), h.handle_id(), generation);
}

void status_codes() {
    CHECK(std::strcmp(handle_status_code(HandleStatus::Closed), "runtime.handle_closed") == 0);
    CHECK(std::strcmp(handle_status_code(HandleStatus::Forged), "internal.handle_forged") == 0);
    CHECK(std::strcmp(handle_status_code(HandleStatus::LimitExceeded), "limit.exceeded") == 0);
    CHECK(std::strcmp(handle_status_code(HandleStatus::DetachForbidden), "runtime.detach_in_build") == 0);
    CHECK(handle_error_class(HandleStatus::LimitExceeded) == HandleErrorClass::Limit);
    CHECK(handle_error_class(HandleStatus::Closed) == HandleErrorClass::Recoverable);  // D-035: catchable
    CHECK(handle_error_class(HandleStatus::Forged) == HandleErrorClass::Internal);
    CHECK(handle_error_class(HandleStatus::Foreign) == HandleErrorClass::Internal);
    CHECK(handle_error_class(HandleStatus::KindMismatch) == HandleErrorClass::Internal);
    CHECK(handle_error_class(HandleStatus::TransferForbidden) == HandleErrorClass::Internal);
    CHECK(handle_error_class(HandleStatus::DetachForbidden) == HandleErrorClass::Error);
    CHECK(handle_error_class(HandleStatus::Ok) == HandleErrorClass::None);
    // Every status has a distinct, non-empty code.
    std::map<std::string, int> seen;
    for (int s = 1; s <= static_cast<int>(HandleStatus::CleanupFailed); ++s) {
        const std::string code = handle_status_code(static_cast<HandleStatus>(s));
        CHECK(!code.empty() && ++seen[code] == 1);
    }
}

void registry_statuses() {
    HandleRegistry reg(HandleKind::File);
    Value a = reg.add(std::make_shared<int>(1));
    CHECK(reg.check(a) == HandleStatus::Ok);
    CHECK(reg.check(Value::integer(1)) == HandleStatus::NotAHandle);
    CHECK(reg.check(make_string("\x1fnift:file:0")) == HandleStatus::NotAHandle);
    CHECK(reg.check(Value::handle(HandleKind::Timer, 0, 0)) == HandleStatus::KindMismatch);
    CHECK(reg.check(Value::handle(HandleKind::File, 7, 0)) == HandleStatus::Forged);
    CHECK(reg.check(with_generation(a, 1)) == HandleStatus::Forged);
    CHECK(reg.release(a) && reg.check(a) == HandleStatus::Closed);
    CHECK(reg.check(with_generation(a, 1)) == HandleStatus::Forged);  // the free slot's next generation
    CHECK(reg.set_free_slot_generation_for_test(a.handle_id(), HandleRegistry::kMaxGeneration));
    Value b = reg.add(std::make_shared<int>(2));
    CHECK(reg.release(b) && reg.check(b) == HandleStatus::Closed && reg.retired() == 1);  // retired, not forged
    CHECK(!reg.set_free_slot_generation_for_test(b.handle_id(), 5));
}

void kinds_and_create() {
    Log log;  // declared first: hooks run until the table is destroyed
    HandleTable t;
    CHECK(t.register_kind(HandleKind::None, {}) == HandleStatus::UnknownKind);
    CHECK(t.register_typed_kind<Res>(HandleKind::File, logging_kind("file", log)) == HandleStatus::Ok);
    CHECK(t.register_kind(HandleKind::File, {}) == HandleStatus::KindRegistered);
    const HandleKind custom = static_cast<HandleKind>(200);  // kinds are extensible beyond the named enum
    CHECK(t.register_typed_kind<Res>(custom, logging_kind("custom", log)) == HandleStatus::Ok);
    ContextId s;
    CHECK(t.open_context(ContextKind::Script, kDetached, s) == HandleStatus::Ok && s != kDetached);
    Value h, g;
    CHECK(t.create(s, HandleKind::Timer, res(1), h) == HandleStatus::UnknownKind);
    CHECK(t.create(s, HandleKind::File, nullptr, h) == HandleStatus::NullResource);
    CHECK(t.create(12345, HandleKind::File, res(1), h) == HandleStatus::UnknownContext);
    CHECK(t.create(s, HandleKind::File, res(1), h) == HandleStatus::Ok);
    CHECK(t.create(s, custom, res(2), g) == HandleStatus::Ok);
    CHECK(h.is(Kind::Handle) && h.handle_kind() == HandleKind::File && h.handle_generation() == 1);
    CHECK((h.handle_id() >> 32) == t.tag());
    CHECK(serial_of(t, h, HandleKind::File, s) == 1 && serial_of(t, g, custom, s) == 2);
    std::shared_ptr<Other> wrong;
    CHECK(t.resolve_as<Other>(h, HandleKind::File, s, wrong) == HandleStatus::TypeMismatch && !wrong);
    ContextId owner = 99;
    CHECK(t.owner_of(h, HandleKind::File, owner) == HandleStatus::Ok && owner == s);
    TableStats st = t.stats();
    CHECK(st.live == 2 && st.created == 2 && st.contexts == 1);
    CloseReport r = t.close_context(s);
    CHECK(r.status == HandleStatus::Ok && r.cleaned == 2 && r.contexts == 1 && r.failures.empty());
    CHECK(log.events.size() == 2 && log.events[0] == "2:context-end" && log.events[1] == "1:context-end");
}

// Every forged or stale handle fails closed with a specific status, and never
// reaches another resource.
void forgery_and_staleness() {
    Log log;  // declared first: hooks run until the table is destroyed
    HandleTable t, other;
    t.register_typed_kind<Res>(HandleKind::File, logging_kind("file", log));
    t.register_typed_kind<Res>(HandleKind::Process, logging_kind("process", log));
    other.register_typed_kind<Res>(HandleKind::File, logging_kind("file", log));
    ContextId s, os;
    t.open_context(ContextKind::Script, kDetached, s);
    other.open_context(ContextKind::Script, kDetached, os);
    Value h, oh, p;
    CHECK(t.create(s, HandleKind::File, res(10), h) == HandleStatus::Ok);
    CHECK(other.create(os, HandleKind::File, res(11), oh) == HandleStatus::Ok);
    CHECK(t.create(s, HandleKind::Process, res(12), p) == HandleStatus::Ok);
    std::shared_ptr<void> out;
    auto st = [&](const Value& v, HandleKind k) { out.reset(); HandleStatus r = t.resolve(v, k, s, out); return r; };
    CHECK(st(make_string("\x1fnift:file:0"), HandleKind::File) == HandleStatus::NotAHandle && !out);
    CHECK(st(Value::integer(0), HandleKind::File) == HandleStatus::NotAHandle && !out);
    CHECK(st(Value(), HandleKind::File) == HandleStatus::NotAHandle && !out);
    CHECK(st(h, HandleKind::Process) == HandleStatus::KindMismatch && !out);
    CHECK(st(oh, HandleKind::File) == HandleStatus::Foreign && !out);
    // Same slot index and generation as a live handle, but another table's tag.
    CHECK(st(Value::handle(HandleKind::File, (std::uint64_t(t.tag() + 1) << 32) | std::uint32_t(h.handle_id()), 1),
             HandleKind::File) == HandleStatus::Foreign);
    const std::uint64_t tagged = std::uint64_t(t.tag()) << 32;
    CHECK(st(Value::handle(HandleKind::File, tagged | 999, 1), HandleKind::File) == HandleStatus::Forged);
    CHECK(st(with_generation(h, 0), HandleKind::File) == HandleStatus::Forged);
    CHECK(st(with_generation(h, 2), HandleKind::File) == HandleStatus::Forged);
    CHECK(st(with_generation(h, 0xFFFFFFFFu), HandleKind::File) == HandleStatus::Forged);
    // The kind byte of a live process handle rewritten to File: id and
    // generation match a live slot, but the slot holds a process.
    CHECK(st(Value::handle(HandleKind::File, p.handle_id(), p.handle_generation()), HandleKind::File) ==
          HandleStatus::Forged && !out);
    CHECK(st(h, HandleKind::File) == HandleStatus::Ok && out);
    // Release, then reuse of the same slot by another kind.
    CHECK(t.release(h, HandleKind::File, s) == HandleStatus::Ok);
    CHECK(st(h, HandleKind::File) == HandleStatus::Closed && !out);
    CHECK(st(with_generation(h, 2), HandleKind::File) == HandleStatus::Forged);  // next generation, not yet issued
    Value reused;
    CHECK(t.create(s, HandleKind::Process, res(13), reused) == HandleStatus::Ok);
    CHECK(std::uint32_t(reused.handle_id()) == std::uint32_t(h.handle_id()) && reused.handle_generation() == 2);
    CHECK(st(h, HandleKind::File) == HandleStatus::Closed);
    CHECK(st(Value::handle(HandleKind::Process, h.handle_id(), 1), HandleKind::Process) == HandleStatus::Closed);
    CHECK(serial_of(t, reused, HandleKind::Process, s) == 13);
    // Stale handles cannot be released, retained, dropped, detached or moved.
    CHECK(t.release(h, HandleKind::File, s) == HandleStatus::Closed);
    CHECK(t.retain(h) == HandleStatus::Closed && t.drop(h) == HandleStatus::Closed);
    CHECK(t.detach(h, HandleKind::File, s) == HandleStatus::Closed);
    CHECK(t.transfer(h, HandleKind::File, s, s) == HandleStatus::Closed);
    CHECK(log.cleaned[10] == 1);  // released once, never again
    // Error text for natives names the reason and the code.
    std::string error;
    describe_handle_failure(HandleStatus::Closed, "read", HandleKind::File, h, &t, error);
    CHECK(error.find("read: handle is closed") == 0 && error.find("file handle") != std::string::npos &&
          error.find("runtime.handle_closed") != std::string::npos);
    describe_handle_failure(HandleStatus::NotAHandle, "read", HandleKind::File, make_string("x"), &t, error);
    CHECK(error.find("got string") != std::string::npos);
}

void generation_wrap() {
    Log log;  // declared first: hooks run until the table is destroyed
    HandleTable t;
    t.register_typed_kind<Res>(HandleKind::Timer, logging_kind("timer", log));
    ContextId s;
    t.open_context(ContextKind::Script, kDetached, s);
    Value first;
    t.create(s, HandleKind::Timer, res(1), first);
    t.release(first, HandleKind::Timer, s);
    const std::uint32_t slot = std::uint32_t(first.handle_id());
    CHECK(!t.set_free_generation_for_test(slot, 0));
    CHECK(!t.set_free_generation_for_test(slot + 1, 7));
    CHECK(t.set_free_generation_for_test(slot, 0xFFFFFFFEu));
    Value a, b, c;
    t.create(s, HandleKind::Timer, res(2), a);
    CHECK(std::uint32_t(a.handle_id()) == slot && a.handle_generation() == 0xFFFFFFFEu);
    t.release(a, HandleKind::Timer, s);
    t.create(s, HandleKind::Timer, res(3), b);
    CHECK(std::uint32_t(b.handle_id()) == slot && b.handle_generation() == 0xFFFFFFFFu);
    CHECK(t.release(b, HandleKind::Timer, s) == HandleStatus::Ok);
    CHECK(t.stats().retired == 1);
    t.create(s, HandleKind::Timer, res(4), c);
    CHECK(std::uint32_t(c.handle_id()) != slot);  // retired: never reused, generations never wrap to 1
    std::shared_ptr<void> out;
    for (const Value& v : {first, a, b, with_generation(b, 1)}) {
        CHECK(t.resolve(v, HandleKind::Timer, s, out) == HandleStatus::Closed && !out);
    }
    CHECK(!t.set_free_generation_for_test(slot, 5));  // retired slots stay retired
}

// Slots are reused under churn; an old handle never resolves to a new
// resource, and the slot count stays bounded by the live high-water mark.
void reclamation_churn() {
    TableOptions options;
    options.capacity = 64;
    HandleTable t(options);
    t.register_typed_kind<Res>(HandleKind::File, KindSpec{"file", nullptr, "", nullptr});
    ContextId s;
    t.open_context(ContextKind::Script, kDetached, s);
    std::mt19937 rng(20261019);
    std::vector<std::pair<Value, int>> live, dead;
    int serial = 0, wrong = 0;
    for (int step = 0; step < 200000; ++step) {
        if (live.size() < 64 && (live.empty() || rng() % 2)) {
            Value h;
            if (t.create(s, HandleKind::File, res(++serial), h) == HandleStatus::Ok) live.emplace_back(h, serial);
        } else {
            const std::size_t i = rng() % live.size();
            if (t.release(live[i].first, HandleKind::File, s) != HandleStatus::Ok) ++wrong;
            if (dead.size() < 4096) dead.push_back(live[i]);
            else dead[rng() % dead.size()] = live[i];
            live[i] = live.back();
            live.pop_back();
        }
        if (step % 97 == 0) {
            for (const auto& [h, n] : live) wrong += serial_of(t, h, HandleKind::File, s) != n;
            for (const auto& [h, n] : dead) wrong += serial_of(t, h, HandleKind::File, s) != -1;
        }
    }
    CHECK(wrong == 0);
    TableStats st = t.stats();
    CHECK(st.slots <= 64 && st.live == live.size() && st.created == std::uint64_t(serial));
    std::printf("churn: %d creates, %zu slots, %zu live\n", serial, st.slots, st.live);
}

void capacity_limit() {
    TableOptions options;
    options.capacity = 4;
    HandleTable t(options);
    t.register_typed_kind<Res>(HandleKind::File, KindSpec{"file", nullptr, "", nullptr});
    ContextId s;
    t.open_context(ContextKind::Script, kDetached, s);
    std::vector<Value> hs(4);
    for (int i = 0; i < 4; ++i) CHECK(t.create(s, HandleKind::File, res(i), hs[i]) == HandleStatus::Ok);
    auto refused = std::make_shared<Res>(99);
    std::weak_ptr<Res> watch = refused;
    Value h;
    CHECK(t.create(s, HandleKind::File, std::move(refused), h) == HandleStatus::LimitExceeded);
    CHECK(watch.expired() && h.is_null());  // loud, and the refused resource is not kept
    std::string error;
    HandleAccess access{&t, s};
    CHECK(native_handle_result(access, "open", HandleKind::File, res(5), h, error) == HandleStatus::LimitExceeded);
    CHECK(error.find("limit.exceeded") != std::string::npos && error.find("(4 handles, NIFT_HANDLE_LIMIT)") != std::string::npos);
    CHECK(t.release(hs[2], HandleKind::File, s) == HandleStatus::Ok);
    CHECK(t.create(s, HandleKind::File, res(6), h) == HandleStatus::Ok);
}

} // namespace

void ownership_and_access();      // handle_table_ownership.cpp
void contexts_and_cleanup();      // handle_table_ownership.cpp
void counted_references();        // handle_table_ownership.cpp
void native_interface();          // handle_table_ownership.cpp

int main() {
    status_codes();
    registry_statuses();
    kinds_and_create();
    forgery_and_staleness();
    generation_wrap();
    reclamation_churn();
    capacity_limit();
    ownership_and_access();
    contexts_and_cleanup();
    counted_references();
    native_interface();
    return rt_test::finish("rt handle table unit");
}
