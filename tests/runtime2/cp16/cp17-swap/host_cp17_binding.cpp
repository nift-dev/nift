// CP16 x CP17 trial: the process hook registered on CP17's real HandleTable.
// Scratch-only evidence that the swap from the temporary stand-in is mechanical.
#include "rt/handles/handle_ref.h"
#include "rt/handles/table.h"
#include "rt/host/process.h"
#include "rt/host/process_cleanup.h"
#include "rt/host/process_runtime.h"
#include "rt_test.h"

#include <chrono>
#include <csignal>
#include <thread>

using namespace nift::rt;
using namespace nift::rt::host;
using std::chrono::milliseconds;

static handles::CleanupOutcome process_hook(const handles::CleanupEvent& ev) {
    const ProcessRelease r = ev.reason == handles::ReleaseReason::Explicit ? ProcessRelease::Explicit
                           : ev.reason == handles::ReleaseReason::Abandoned ? ProcessRelease::Abandoned
                           : ev.reason == handles::ReleaseReason::ContextEnd ? ProcessRelease::ContextEnd : ProcessRelease::Shutdown;
    process_cleanup(std::static_pointer_cast<ProcessGroup>(ev.resource), r);
    return handles::CleanupOutcome::success();
}

static SpawnRequest req(std::vector<std::string> argv, OutputMode mode) {
    SpawnRequest r;
    SpawnStage s;
    s.program = argv.front();
    s.args.assign(argv.begin() + 1, argv.end());
    r.stages.push_back(s);
    r.mode = mode;
    r.stdin_mode = StdinMode::Null;
    r.kill_after = milliseconds(300);
    r.post_kill_wait = milliseconds(2000);
    return r;
}

static ProcessGroupPtr start(SpawnRequest r) {
    HostStatus st; HostError e;
    auto g = ProcessGroup::start(std::move(r), st, e);
    CHECK(g != nullptr);
    return g;
}

int main() {
    std::vector<std::string> warnings;
    runtime::set_warning_sink([&](const std::string& w) { warnings.push_back(w); });
    handles::HandleTable table;
    handles::KindSpec spec{"process", process_hook, "process.detach_in_build"};
    CHECK(table.register_typed_kind<ProcessGroup>(HandleKind::Process, spec) == HandleStatus::Ok);
    handles::ContextId script = 0;
    CHECK(table.open_context(handles::ContextKind::Script, handles::kDetached, script) == HandleStatus::Ok);

    // abandoned (counted reference dropped) while running: async + warning
    {
        auto g = start(req({"sleep", "30"}, OutputMode::Interactive));
        Value h;
        CHECK(table.create(script, HandleKind::Process, g, h, handles::CreateOptions{true}) == HandleStatus::Ok);
        std::shared_ptr<ProcessGroup> resolved;
        CHECK(table.resolve_as(h, HandleKind::Process, script, resolved) == HandleStatus::Ok && resolved == g);
        g.reset(); resolved.reset();
        auto ref = handles::HandleRef::adopt(table, h);
        const auto t0 = std::chrono::steady_clock::now();
        ref.reset();
        CHECK(std::chrono::steady_clock::now() - t0 < milliseconds(200));
        CHECK(warnings.size() == 1 && warnings[0].find("was abandoned while running; terminated") != std::string::npos);
        CHECK(runtime::reaper_wait_idle(milliseconds(5000)));
    }
    // context end: hooks start, ContextCleanup waits once
    warnings.clear();
    std::vector<ProcessGroupPtr> groups;
    for (int i = 0; i < 3; ++i) {
        auto r = req({"sh", "-c", "trap '' TERM; sleep 30"}, OutputMode::Interactive);
        r.kill_after = milliseconds(500);
        groups.push_back(start(r));
        Value h;
        CHECK(table.create(script, HandleKind::Process, groups.back(), h) == HandleStatus::Ok);
    }
    std::this_thread::sleep_for(milliseconds(100));
    const auto t0 = std::chrono::steady_clock::now();
    {
        ContextCleanup end("script", false);
        auto report = table.close_context(script);
        CHECK(report.status == HandleStatus::Ok && report.failures.empty() && report.cleaned == 3);
        CHECK(end.wait().empty());
    }
    const auto ms = std::chrono::duration_cast<milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms >= 450 && ms < 1500);
    CHECK(warnings.size() == 3);
    for (auto& g : groups) CHECK(g->finished() && g->result().stages[0].signal == SIGKILL);

    // detach refused in a build (CP17 code), allowed in a script; shutdown leaves it alone
    handles::ContextId build = 0, script2 = 0;
    table.open_context(handles::ContextKind::Build, handles::kDetached, build);
    table.open_context(handles::ContextKind::Script, handles::kDetached, script2);
    auto det = start(req({"sleep", "30"}, OutputMode::Detached));
    Value hd, hb;
    table.create(script2, HandleKind::Process, det, hd);
    CHECK(table.detach(hd, HandleKind::Process, script2) == HandleStatus::Ok);
    auto inbuild = start(req({"sleep", "30"}, OutputMode::Interactive));
    table.create(build, HandleKind::Process, inbuild, hb);
    CHECK(table.detach(hb, HandleKind::Process, build) != HandleStatus::Ok);
    {
        ContextCleanup end("build", false);
        table.close_context(build);
        table.close_context(script2);
    }
    table.shutdown();
    CHECK(det->running());
    det->cancel();
    runtime::reaper_adopt(det);
    CHECK(runtime::reaper_wait_idle(milliseconds(5000)));
    runtime::set_warning_sink(nullptr);
    return rt_test::finish("host-cp17-binding");
}
