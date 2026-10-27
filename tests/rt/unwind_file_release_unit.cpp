// CP17 x CP13B (owner decisions B and the statement-level extension,
// 2026-10-27): the statement rollback's file rules, linked against the
// production natives and handle table. A caught Error withdraws the left-open
// reports of files created during the aborted evaluation whose last reference
// died (including every frame the Error unwound); nothing else is withdrawn
// and every cleanup runs exactly once.
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

    // Managed files under the statement rollback (owner decisions B and the
    // statement extension, 2026-10-27; B's separate per-frame release was
    // removed as redundant). A file whose last reference dies while open is
    // cleaned exactly once and reports "left open" (CleanupIncomplete,
    // Abandoned); withdraw_abandoned_failures removes that report only for
    // files of the given owner created after the mark.
    {  // R1 last reference to an open file, created after the mark: report withdrawn, cleanup once.
        Fixture f;
        const std::uint64_t mark = f.table.creation_count();
        Value v = f.file(read_path);
        f.open(v, "r");
        const Value identity = v.handle_identity();
        const auto before = f.table.stats();
        v = Value();  // the last reference dies (a register cleared by the unwind)
        const auto after = f.table.stats();
        CHECK(after.live == 0 && after.cleaned == before.cleaned + 1);  // exactly one cleanup
        CHECK(f.table.withdraw_abandoned_failures(f.ctx, HandleKind::File, mark) == 1);
        CHECK(f.table.withdraw_abandoned_failures(f.ctx, HandleKind::File, mark) == 0);  // exactly once
        CHECK(f.table.stats().cleaned == after.cleaned);  // withdrawing runs no cleanup
        std::shared_ptr<FileResource> stale;
        CHECK(f.table.resolve_as<FileResource>(identity, HandleKind::File, f.ctx, stale) == HandleStatus::Closed);
        CHECK(f.failures() == 0);
    }
    {  // R2 a surviving alias keeps the file open and unreported; once it dies after a
       // later mark, its report stays (it was created before that mark).
        Fixture f;
        const std::uint64_t mark = f.table.creation_count();
        Value v = f.file(read_path);
        f.open(v, "r");
        Value alias = v;
        v = Value();  // the unwound frame's copy dies; the alias survives
        CHECK(f.table.stats().live == 1 && f.resource(alias)->open);
        CHECK(f.table.withdraw_abandoned_failures(f.ctx, HandleKind::File, mark) == 0);  // nothing reported yet
        const std::uint64_t later = f.table.creation_count();
        alias = Value();
        CHECK(f.table.withdraw_abandoned_failures(f.ctx, HandleKind::File, later) == 0);
        CHECK(left_open_reported(f));
    }
    {  // R3 explicitly closed before the Error: no report to withdraw, close stays idempotent.
        Fixture f;
        const std::uint64_t mark = f.table.creation_count();
        Value v = f.file(read_path);
        f.open(v, "r");
        Value out;
        CHECK(natives::file_close(f.context, &v, 1, out, f.error) == natives::Status::Ok);
        v = Value();
        CHECK(f.table.withdraw_abandoned_failures(f.ctx, HandleKind::File, mark) == 0);
        CHECK(f.failures() == 0);
    }
    {  // R4 unsaved writes: never written (as legacy's rollback), report withdrawn.
        Fixture f;
        const std::string path = temp_path("cp17-b-write", nullptr);
        const std::uint64_t mark = f.table.creation_count();
        Value v = f.file(path);
        f.open(v, "w");
        {
            Value args[2] = {v, make_string("data")}, out;
            CHECK(natives::file_write(f.context, args, 2, out, f.error) == natives::Status::Ok);
        }
        CHECK(f.resource(v)->dirty);
        v = Value();
        CHECK(!std::filesystem::exists(path));
        CHECK(f.table.withdraw_abandoned_failures(f.ctx, HandleKind::File, mark) == 1);
        CHECK(f.failures() == 0);
        std::remove(path.c_str());
    }
    {  // R5 a nested context may ACCESS its parent's file but does not own it: a
       // rollback for the child never withdraws the parent's report.
        Fixture f;
        ContextId child = 0;
        CHECK(f.table.open_context(ContextKind::Script, f.ctx, child, "child") == HandleStatus::Ok);
        const std::uint64_t mark = f.table.creation_count();
        Value v = f.file(read_path);  // owned by f.ctx, the child's parent
        f.open(v, "r");
        v = Value();
        CHECK(f.table.withdraw_abandoned_failures(child, HandleKind::File, mark) == 0);
        f.table.close_context(child);
        CHECK(left_open_reported(f));
    }
    {  // R6 another owner (a sibling context) and the detached pseudo-owner: never withdrawn.
        Fixture f;
        ContextId other = 0;
        CHECK(f.table.open_context(ContextKind::Script, kDetached, other, "other") == HandleStatus::Ok);
        const std::uint64_t mark = f.table.creation_count();
        Value v = f.file(read_path);
        f.open(v, "r");
        v = Value();
        CHECK(f.table.withdraw_abandoned_failures(other, HandleKind::File, mark) == 0);
        CHECK(f.table.withdraw_abandoned_failures(kDetached, HandleKind::File, 0) == 0);
        f.table.close_context(other);
        CHECK(left_open_reported(f));
    }
    {  // R7 files created before the mark (an earlier, completed evaluation) stay reported,
       // while newer ones are withdrawn: multiple files at different points.
        Fixture f;
        Value older = f.file(read_path);
        f.open(older, "r");
        const std::uint64_t mark = f.table.creation_count();
        Value newer1 = f.file(read_path), newer2 = f.file(read_path);
        f.open(newer1, "r");
        f.open(newer2, "r");
        older = Value();
        newer1 = Value();
        newer2 = Value();
        CHECK(f.table.withdraw_abandoned_failures(f.ctx, HandleKind::File, mark) == 2);
        CHECK(left_open_reported(f));  // exactly the older one
    }
    {  // R8 generations: after a withdrawn report, the slot is reused with a new
       // generation and the old identity stays Closed.
        Fixture f;
        const std::uint64_t mark = f.table.creation_count();
        Value v = f.file(read_path);
        f.open(v, "r");
        const Value old_identity = v.handle_identity();
        v = Value();
        CHECK(f.table.withdraw_abandoned_failures(f.ctx, HandleKind::File, mark) == 1);
        Value next = f.file(read_path);
        CHECK(next.handle_id() == old_identity.handle_id());
        CHECK(next.handle_generation() != old_identity.handle_generation());
        std::shared_ptr<FileResource> stale;
        CHECK(f.table.resolve_as<FileResource>(old_identity, HandleKind::File, f.ctx, stale) == HandleStatus::Closed);
        next = Value();  // never opened: nothing to report
        CHECK(f.failures() == 0);
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
