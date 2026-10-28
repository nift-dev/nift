// CP17 unwind release (owner decision, 2026-10-28, an approved Runtime 2.0
// semantic correction; it replaces decision B and the statement rollback):
// ownership rule of natives::abandon_file_on_unwind, linked against the
// production natives and handle table. The VM calls it for each register a
// caught Error unwinds (every register of an unwound frame, and the try body's
// registers in the handler's frame), immediately before clearing it. Only the
// LAST reference to an OPEN file OWNED by the run's context is discarded
// (silently, unsaved changes never written); every other reference is left
// exactly as it was, and the slot's cleanup still runs exactly once, from the drop.
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
    {  // B6 unsaved writes are discarded, never written, and not reported.
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
    // Creation sequence provenance (CleanupFailure::sequence: the VM's
    // left-open summary names the most recently created file, CP17 rework F3).
    {
        HandleTable table;
        KindSpec fails;
        fails.name = "custom";
        fails.cleanup = [](CleanupEvent&) { return CleanupOutcome::failure("test.left_open", "left open"); };
        table.register_kind(HandleKind::Custom, fails);
        ContextId ctx = 0;
        CHECK(table.open_context(ContextKind::Script, kDetached, ctx, "script") == HandleStatus::Ok);
        auto make = [&]() {
            Value v;
            CHECK(table.create(ctx, HandleKind::Custom, std::make_shared<int>(1), v, CreateOptions{true}) ==
                  HandleStatus::Ok);
            return v;
        };
        Value first = make(), second = make(), third = make();
        CHECK(table.drop(second) == HandleStatus::Ok);  // dies first
        CHECK(table.drop(first) == HandleStatus::Ok);
        CHECK(table.drop(third) == HandleStatus::Ok);
        const CloseReport report = table.close_context(ctx);
        CHECK(report.failures.size() == 3);
        if (report.failures.size() == 3) {  // in order of death, each with its creation sequence
            CHECK(report.failures[0].sequence == 2);
            CHECK(report.failures[1].sequence == 1);
            CHECK(report.failures[2].sequence == 3);
        }
    }
    std::remove(read_path.c_str());
    return rt_test::finish("rt unwind file release (CP17 unwind release)");
}
