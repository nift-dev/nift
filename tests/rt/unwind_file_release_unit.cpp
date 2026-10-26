// CP17 x CP13B decision B (owner, 2026-10-27): ownership rule of
// natives::abandon_file_on_unwind, linked against the production natives and
// handle table. The VM calls it for each register of a frame a caught Error
// unwinds, immediately before clearing that register. Only the LAST reference
// to an OPEN file OWNED by the run's context is discarded (silently, as
// legacy's rollback_file_operation); every other reference is left exactly as
// it was, and the slot's cleanup still runs exactly once, from the drop.
#include "rt/handles/file_resource.h"
#include "rt/natives/host_file_handles.h"
#include "rt/natives/host_stream_handles.h"
#include "rt/string.h"
#include "rt_test.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace nift::rt;
using namespace nift::rt::handles;

namespace {

std::string temp_path(const char* name, const char* content) {
    const std::string path = (std::filesystem::temp_directory_path() / name).generic_string();
    if (content) std::ofstream(path, std::ios::binary) << content;
    else std::remove(path.c_str());
    return path;
}

struct Fixture {
    HandleTable table;
    ContextId ctx = 0;
    natives::Context context;
    std::string code, error;
    Fixture() {
        KindSpec file_spec;
        file_spec.name = "file";
        file_spec.cleanup = natives::cleanup_file;
        table.register_typed_kind<FileResource>(HandleKind::File, file_spec);
        KindSpec stream_spec;
        stream_spec.name = "stream";
        stream_spec.cleanup = natives::cleanup_stream;
        table.register_typed_kind<StreamResource>(HandleKind::Stream, stream_spec);
        table.open_context(ContextKind::Script, kDetached, ctx, "script");
        context.handles = {&table, ctx};
        context.host.error_code = &code;
    }
    Value file(const std::string& path) {
        Value arg = make_string(path), out;
        CHECK(natives::file_factory(context, &arg, 1, out, error) == natives::Status::Ok);
        return out;
    }
    void open(const Value& f, const char* mode) {
        Value args[2] = {f, make_string(mode)}, out;
        CHECK(natives::file_open(context, args, 2, out, error) == natives::Status::Ok);
    }
    std::shared_ptr<FileResource> resource(const Value& f) {
        std::shared_ptr<FileResource> r;
        CHECK(table.resolve_as<FileResource>(f, HandleKind::File, ctx, r) == HandleStatus::Ok);
        return r;
    }
    std::size_t failures() {
        const CloseReport report = table.close_context(ctx);
        CHECK(report.status == HandleStatus::Ok);
        return report.failures.size();
    }
};

bool left_open_reported(Fixture& f) {
    const CloseReport report = f.table.close_context(f.ctx);
    return report.failures.size() == 1 &&
           report.failures.front().message.find("managed file left open") != std::string::npos;
}

} // namespace

int main() {
    const std::string read_path = temp_path("cp17-b-read", "one\ntwo\n");

    {  // B1 last reference to an open file: discarded silently, cleanup once, no report.
        Fixture f;
        Value v = f.file(read_path);
        f.open(v, "r");
        CHECK(v.boxed_handle() && v.refcount() == 1);
        const Value identity = v.handle_identity();
        auto r = f.resource(v);
        CHECK(natives::abandon_file_on_unwind(f.table, f.ctx, v));
        CHECK(!r->open && !r->dirty && r->working.empty() && r->cursor == 0);
        const auto before = f.table.stats();
        v = Value();  // the VM's register clear: the last reference dies
        const auto after = f.table.stats();
        CHECK(after.live == 0 && after.cleaned == before.cleaned + 1);  // exactly one cleanup, from the drop
        std::shared_ptr<FileResource> stale;
        CHECK(f.table.resolve_as<FileResource>(identity, HandleKind::File, f.ctx, stale) == HandleStatus::Closed);
        CHECK(f.failures() == 0);
    }
    {  // B2 a surviving alias (refcount 2): untouched; the survivor still reports left-open.
        Fixture f;
        Value v = f.file(read_path);
        f.open(v, "r");
        Value alias = v;
        CHECK(v.refcount() == 2);
        CHECK(!natives::abandon_file_on_unwind(f.table, f.ctx, v));
        CHECK(f.resource(alias)->open);
        v = Value();  // the unwound frame's copy dies; the alias survives
        CHECK(f.table.stats().live == 1 && f.resource(alias)->open);
        alias = Value();  // later abandoned normally: legacy and the VM both report it
        CHECK(left_open_reported(f));
    }
    {  // B3 same-frame aliases cleared in register order: the last one discards, exactly once.
        Fixture f;
        Value regs[3] = {f.file(read_path), Value(), Value()};
        f.open(regs[0], "r");
        regs[1] = regs[0];
        regs[2] = regs[0];
        int discarded = 0;
        for (Value& reg : regs) {
            if (reg.is(Kind::Handle) && natives::abandon_file_on_unwind(f.table, f.ctx, reg)) ++discarded;
            reg = Value();
        }
        CHECK(discarded == 1);
        CHECK(f.table.stats().live == 0);
        CHECK(f.failures() == 0);
    }
    {  // B4 explicitly closed before the unwind: nothing to do, no report, close stays idempotent.
        Fixture f;
        Value v = f.file(read_path);
        f.open(v, "r");
        Value out;
        CHECK(natives::file_close(f.context, &v, 1, out, f.error) == natives::Status::Ok);
        CHECK(!natives::abandon_file_on_unwind(f.table, f.ctx, v));
        v = Value();
        CHECK(f.failures() == 0);
    }
    {  // B5 never opened: nothing to discard.
        Fixture f;
        Value v = f.file(read_path);
        CHECK(!natives::abandon_file_on_unwind(f.table, f.ctx, v));
        v = Value();
        CHECK(f.failures() == 0);
    }
    {  // B6 unsaved writes are discarded, never written (legacy rollback), and not reported.
        Fixture f;
        const std::string path = temp_path("cp17-b-write", nullptr);
        Value v = f.file(path);
        f.open(v, "w");
        {
            Value args[2] = {v, make_string("data")}, out;
            CHECK(natives::file_write(f.context, args, 2, out, f.error) == natives::Status::Ok);
        }  // the call's argument copy is gone: `v` is the last reference again
        CHECK(f.resource(v)->dirty);
        CHECK(natives::abandon_file_on_unwind(f.table, f.ctx, v));
        v = Value();
        CHECK(!std::filesystem::exists(path));
        CHECK(f.failures() == 0);
        std::remove(path.c_str());
    }
    {  // B7 another owner (a second context) and a foreign table: refused, file untouched.
        Fixture f;
        ContextId other = 0;
        CHECK(f.table.open_context(ContextKind::Script, kDetached, other, "other") == HandleStatus::Ok);
        Value v = f.file(read_path);
        f.open(v, "r");
        CHECK(!natives::abandon_file_on_unwind(f.table, other, v));
        Fixture foreign;
        CHECK(!natives::abandon_file_on_unwind(foreign.table, foreign.ctx, v));
        CHECK(f.resource(v)->open);
        f.table.close_context(other);
        v = Value();
        CHECK(left_open_reported(f));
    }
    {  // B7b a nested context (one that may ACCESS the handle) is still not its owner:
       // only the run's own files are discarded, never an enclosing owner's.
        Fixture f;
        ContextId child = 0;
        CHECK(f.table.open_context(ContextKind::Script, f.ctx, child, "child") == HandleStatus::Ok);
        Value v = f.file(read_path);  // owned by f.ctx, the child's parent
        f.open(v, "r");
        CHECK(!natives::abandon_file_on_unwind(f.table, child, v));
        CHECK(f.resource(v)->open);
        f.table.close_context(child);
        v = Value();
        CHECK(left_open_reported(f));
    }
    {  // B8 not a file box: unboxed identity, stream, scalar, null.
        Fixture f;
        Value v = f.file(read_path);
        f.open(v, "r");
        CHECK(!natives::abandon_file_on_unwind(f.table, f.ctx, v.handle_identity()));
        Value sarg = make_string(read_path), stream;
        CHECK(natives::ifstream_factory(f.context, &sarg, 1, stream, f.error) == natives::Status::Ok);
        CHECK(stream.refcount() == 1);
        CHECK(!natives::abandon_file_on_unwind(f.table, f.ctx, stream));
        CHECK(!natives::abandon_file_on_unwind(f.table, f.ctx, make_string("x")));
        CHECK(!natives::abandon_file_on_unwind(f.table, f.ctx, Value()));
        CHECK(f.resource(v)->open);
        stream = Value();
        v = Value();
        CHECK(left_open_reported(f));
    }
    {  // B9 idempotent: a second call on the same reference finds the file closed.
        Fixture f;
        Value v = f.file(read_path);
        f.open(v, "r");
        CHECK(natives::abandon_file_on_unwind(f.table, f.ctx, v));
        CHECK(!natives::abandon_file_on_unwind(f.table, f.ctx, v));
        v = Value();
        CHECK(f.failures() == 0);
    }
    {  // B10 generations: a discarded slot is reused with a new generation; the old identity stays Closed.
        Fixture f;
        Value v = f.file(read_path);
        f.open(v, "r");
        const Value old_identity = v.handle_identity();
        CHECK(natives::abandon_file_on_unwind(f.table, f.ctx, v));
        v = Value();
        Value next = f.file(read_path);
        f.open(next, "r");
        CHECK(next.handle_id() == old_identity.handle_id());
        CHECK(next.handle_generation() != old_identity.handle_generation());
        std::shared_ptr<FileResource> stale;
        CHECK(f.table.resolve_as<FileResource>(old_identity, HandleKind::File, f.ctx, stale) == HandleStatus::Closed);
        CHECK(f.resource(next)->open);
        CHECK(natives::abandon_file_on_unwind(f.table, f.ctx, next));
        next = Value();
        CHECK(f.failures() == 0);
    }
    {  // B11 the context already ended (end of run): the table refuses the owner check.
        Fixture f;
        Value v = f.file(read_path);
        f.open(v, "r");
        CHECK(left_open_reported(f));  // context close reports and closes it
        CHECK(!natives::abandon_file_on_unwind(f.table, f.ctx, v));
        v = Value();
    }
    // Statement rollback (owner decision, 2026-10-27): HandleTable::withdraw_abandoned_failures.
    {
        HandleTable table;
        KindSpec fails;
        fails.name = "custom";
        fails.cleanup = [](CleanupEvent&) { return CleanupOutcome::failure("test.left_open", "left open"); };
        table.register_kind(HandleKind::Custom, fails);
        KindSpec throws;
        throws.name = "timer";
        throws.cleanup = [](CleanupEvent&) -> CleanupOutcome { throw std::runtime_error("hook threw"); };
        table.register_kind(HandleKind::Timer, throws);
        KindSpec alive;
        alive.name = "process";
        alive.cleanup = [](CleanupEvent&) { return CleanupOutcome::alive_after_kill("test.alive", "still alive"); };
        table.register_kind(HandleKind::Process, alive);
        ContextId ctx = 0, other = 0;
        CHECK(table.open_context(ContextKind::Script, kDetached, ctx, "script") == HandleStatus::Ok);
        CHECK(table.open_context(ContextKind::Script, kDetached, other, "other") == HandleStatus::Ok);
        auto make = [&](HandleKind kind, ContextId owner) {
            Value v;
            CHECK(table.create(owner, kind, std::make_shared<int>(1), v, CreateOptions{true}) == HandleStatus::Ok);
            return v;
        };
        CHECK(table.creation_count() == 0);
        Value older = make(HandleKind::Custom, ctx);        // created before the mark
        const std::uint64_t mark = table.creation_count();
        CHECK(mark == 1);
        Value abandoned = make(HandleKind::Custom, ctx);    // after the mark, last reference dropped
        Value closed = make(HandleKind::Custom, ctx);       // after the mark, explicitly closed
        Value survivor = make(HandleKind::Custom, ctx);     // after the mark, still referenced
        Value lost = make(HandleKind::Timer, ctx);          // after the mark, hook threw (Lost)
        Value foreign_owner = make(HandleKind::Custom, other);
        Value still_alive = make(HandleKind::Process, ctx);  // after the mark, Failed (alive after kill)
        CHECK(table.creation_count() == 7);
        CHECK(table.drop(older) == HandleStatus::Ok);
        CHECK(table.drop(abandoned) == HandleStatus::Ok);
        CHECK(table.close(closed, HandleKind::Custom, ctx) == HandleStatus::CleanupFailed);  // reported, Explicit
        CHECK(table.drop(lost) == HandleStatus::Ok);
        CHECK(table.drop(foreign_owner) == HandleStatus::Ok);
        CHECK(table.drop(still_alive) == HandleStatus::Ok);
        const auto cleaned = table.stats().cleaned;
        CHECK(table.withdraw_abandoned_failures(ctx, HandleKind::File, mark) == 0);  // another kind
        CHECK(table.withdraw_abandoned_failures(ctx, HandleKind::Timer, mark) == 0);  // Lost: never withdrawn
        CHECK(table.withdraw_abandoned_failures(ctx, HandleKind::Process, mark) == 0);  // Failed: never withdrawn
        CHECK(table.withdraw_abandoned_failures(ctx, HandleKind::Custom, 7) == 0);   // nothing newer
        CHECK(table.withdraw_abandoned_failures(ctx, HandleKind::Custom, mark) == 1);
        CHECK(table.withdraw_abandoned_failures(ctx, HandleKind::Custom, mark) == 0);  // exactly once
        CHECK(table.withdraw_abandoned_failures(kDetached, HandleKind::Custom, 0) == 0);
        CHECK(table.stats().cleaned == cleaned);  // no cleanup ran or re-ran
        const CloseReport report = table.close_context(ctx);
        // older (sequence 1 <= mark), explicit close, Lost hook, and the survivor
        // released at context end stay reported; only `abandoned` was withdrawn.
        CHECK(report.failures.size() == 5);
        std::uint64_t seen = 0;
        for (const CleanupFailure& f : report.failures) {
            seen |= std::uint64_t(1) << f.sequence;
            if (f.sequence == 1) CHECK(f.reason == ReleaseReason::Abandoned && f.state == LifeState::CleanupIncomplete);
            if (f.sequence == 3) CHECK(f.reason == ReleaseReason::Explicit);
            if (f.sequence == 4) CHECK(f.reason == ReleaseReason::ContextEnd);
            if (f.sequence == 5) CHECK(f.state == LifeState::Lost);
            if (f.sequence == 7) CHECK(f.state == LifeState::Failed);
        }
        CHECK(seen == ((1u << 1) | (1u << 3) | (1u << 4) | (1u << 5) | (1u << 7)));
        const CloseReport other_report = table.close_context(other);
        CHECK(other_report.failures.size() == 1 && other_report.failures.front().sequence == 6);
        survivor = Value();
    }
    std::remove(read_path.c_str());
    return rt_test::finish("rt unwind file release (CP17 x CP13B decision B)");
}
