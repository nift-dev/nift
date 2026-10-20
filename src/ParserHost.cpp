// CP16: legacy Host operations through the Host layer (src/rt/host).
// See ParserHost.h and docs/runtime-2/checkpoints/cp16-host-ops-impl.md §9.
#include "ParserHost.h"

#include "FileSystem.h"
#include "HostTrace.h"
#include "Parser.h"
#include "ReadObservation.h"
#include "rt/handles/table.h"
#include "rt/host/process.h"
#include "rt/host/process_call.h"
#include "rt/host/process_cleanup.h"
#include "rt/host/process_handles.h"
#include "rt/host/process_runtime.h"

#include <atomic>
#include <limits>

#ifndef _WIN32
#include <signal.h>
#endif

namespace fs = std::filesystem;
namespace host = nift::rt::host;
namespace handles = nift::rt::handles;

// ---------------------------------------------------------------- recording

namespace {
PageObserver::Mode page_mode(bool observing, bool standalone) {
    PageObserver::Mode mode;
    mode.observing = observing;
    mode.standalone = standalone;
    return mode;
}
} // namespace

// Recording an observation is not a change to the evaluation state that the
// const of inspect_path() protects (same reasoning as observe_presence).
ParserHostObserver::ParserHostObserver(const Parser& parser)
    : PageObserver(parser.host_, const_cast<Parser&>(parser).result_,
                   page_mode(parser.observing(), parser.standalone_script_host_)) {}

host::TraceChannel* parser_host_trace() {
    if (!nift::trace::enabled()) return nullptr;
    return &host::TraceChannel::process_default();
}

host::HostError parser_host_error(const char* code, std::string message) {
    host::HostError error;
    host::make_error(error, code, std::move(message));
    return error;
}

// ---------------------------------------------------------------- processes

namespace {

std::atomic<bool> g_interactive_shell{false};

// One table for the legacy interpreter's process residues. Never destroyed:
// the reaper and host-loop threads may still settle a residue while static
// destructors run.
handles::HandleTable& legacy_process_table() {
    static handles::HandleTable* table = [] {
        auto* created = new handles::HandleTable();
        host::register_process_kind(*created);
        return created;
    }();
    return *table;
}

// C2: SIGINT/SIGTERM terminate the managed children before Nift ends with the
// signal. Installed at the first managed launch, and only while nobody else
// handles those signals (the interactive shell, --auto's stop handler).
[[maybe_unused]] void ensure_signal_cleanup() {
#ifndef _WIN32
    static std::atomic<bool> installed{false};
    if (installed.load() || g_interactive_shell.load()) return;
    struct sigaction current_int {}, current_term {};
    if (::sigaction(SIGINT, nullptr, &current_int) != 0 || ::sigaction(SIGTERM, nullptr, &current_term) != 0) return;
    auto is = [](const struct sigaction& sa, void (*handler)(int)) { return !(sa.sa_flags & SA_SIGINFO) && sa.sa_handler == handler; };
    // SIGINT is ignored in a shell's background job: it stays ignored.
    const bool int_ignored = is(current_int, SIG_IGN);
    if (!is(current_term, SIG_DFL) || !(int_ignored || is(current_int, SIG_DFL))) return;
    if (!host::runtime::install_signal_cleanup()) return;
    installed = true;
    // C2 for background members: legacy run() stages stay in Nift's process
    // group, so a member a stage leaves behind is found by parentage instead
    // (subreaper; reaped after each run() and at script end, terminated by C2).
    host::runtime::enable_subreaper_for_legacy();
    if (int_ignored) {
        struct sigaction ignore {};
        ignore.sa_handler = SIG_IGN;
        sigemptyset(&ignore.sa_mask);
        ::sigaction(SIGINT, &ignore, nullptr);
    }
#endif
}

#ifndef _WIN32
ProcessResult legacy_result(const host::ProcessResult& r) {
    ProcessResult out;
    out.exit_code = r.exit_code;
    out.out = r.out;
    out.err = r.err;
    out.launched = r.launched;
    out.error = r.error;
    out.capture_limit_exceeded = r.capture_limit_exceeded;
    for (const auto& stage : r.stages) {
        ProcessStageResult s;
        s.exit_code = stage.exit_code;
        switch (stage.state) {
            case host::StageState::NotStarted:
                s.launched = false;
                s.error = stage.error.empty() ? (r.error.empty() ? std::string("not started") : r.error) : stage.error;
                break;
            case host::StageState::LaunchFailed:
                s.launched = false;
                s.error = stage.error;
                break;
            default:
                // Started (exited, signaled, or lost after SIGKILL + bound).
                // Legacy: launched with its own status; a parent-side failure
                // of the pipeline marks every stage not launched.
                s.launched = r.state != host::GroupState::LaunchFailed;
                s.error = stage.error;
                break;
        }
        out.stages.push_back(std::move(s));
    }
    if (r.stages.size() == 1 && !r.launched && r.state == host::GroupState::LaunchFailed && !r.stages.empty() &&
        r.stages.front().state == host::StageState::LaunchFailed)
        out.exec_failed = true;
    return out;
}
#endif

} // namespace

void parser_host_set_interactive_shell(bool interactive) { g_interactive_shell = interactive; }

bool Parser::run_host_pipeline(const std::vector<ProcessSpec>& specs, std::uint64_t capture_limit, ProcessResult& out,
                               std::string& error) {
#ifdef _WIN32
    // The Host process layer is POSIX-only so far (process_win.cpp reports
    // process.unsupported): Windows keeps the legacy launcher, unchanged.
    (void)error;
    out = nift_run_pipeline(specs, true, false, capture_limit);
    return true;
#else
    host::SpawnRequest request;
    request.mode = host::OutputMode::FileCapture;  // legacy: seekable regular files, descendants not waited for
    request.stdin_mode = host::StdinMode::Inherit;
    request.capture_limit = capture_limit ? capture_limit : std::numeric_limits<std::uint64_t>::max();
    request.builtin = "run";
    // Legacy contract (test-consumer-snapshots, interrupt barrier): when Nift
    // itself is SIGKILLed, a hook's child keeps running and keeps the build
    // lock, so the next build waits for it. No PR_SET_PDEATHSIG here; SIGINT/
    // SIGTERM still terminate the children (C2).
    request.die_with_parent = false;
    // Legacy contract (rt-diff-selftest, supervisors): the stages stay in
    // Nift's process group, so a signal to that group (killpg by a harness,
    // Ctrl-C) reaches them. Hence no C3 group and no C1 leftover tracking for
    // legacy run(); C2 still terminates them (by pid) on SIGINT/SIGTERM.
    request.own_group = false;
    request.exec_report_optional = true;  // CP07: capture works under `ulimit -n 6`
    for (const auto& spec : specs) {
        host::SpawnStage stage;
        stage.program = spec.program;
        stage.args = spec.args;
        stage.cwd = spec.cwd.empty() ? std::string() : spec.cwd.generic_string();
        for (const auto& kv : spec.env) stage.env.emplace_back(kv.first, kv.second);
        stage.stdin_path = spec.stdin_path;
        stage.stdout_path = spec.stdout_path;
        stage.stdout_append = spec.append_stdout;
        stage.stderr_path = spec.stderr_path;
        stage.stderr_append = spec.append_stderr;
        // merge_stderr and foreground_terminal are never set by run()/cmd().
        request.stages.push_back(std::move(stage));
    }
    ensure_signal_cleanup();
    host::HostStatus status = host::HostStatus::Ok;
    host::HostError host_error;
    auto group = host::start_traced(parser_host_env(nullptr), std::move(request), status, host_error, /*wait_at_once=*/true);
    if (!group) {
        // Refused before launching anything (Nift is shutting down after
        // SIGINT/SIGTERM; an empty pipeline cannot be built by cmd()).
        error = host_error.message;
        return false;
    }
    const host::ProcessResult& result = group->wait();
    host::runtime::reap_adopted_children();  // background members that already exited (no zombies)
    if (host::runtime::shutting_down()) {
        // C2: SIGINT/SIGTERM arrived while the call ran; the watcher is ending
        // Nift with the signal. The script stops here instead of running on
        // (its writes do not all go through the Host layer's refusal yet).
        error = "run: Nift is shutting down (signal received)";
        return false;
    }
    // C1: background members still in the call's process group belong to
    // this execution context; they are terminated (silently) when it ends.
    // (Only for a call in its own group; legacy run() is not, see above.)
    if (!group->detached() && group->has_live_members()) {
        bool registered = false;
        handles::HandleTable& table = legacy_process_table();
        if (!host_process_context_) {
            handles::ContextId id = 0;
            const std::string label = standalone_script_host_ ? std::string("script") : "page '" + tracked_info_.name + "'";
            if (table.open_context(standalone_script_host_ ? handles::ContextKind::Script : handles::ContextKind::PageRender,
                                   handles::kDetached, id, label) == nift::rt::HandleStatus::Ok)
                host_process_context_ = id;
        }
        if (host_process_context_) host::register_run_residue(table, host_process_context_, group, registered);
        if (!registered) host::process_cleanup(group, host::ProcessRelease::ContextEnd, "run()", false);  // never leak
    }
    out = legacy_result(result);
    return true;
#endif
}

bool Parser::end_host_process_context(std::string& error, bool on_error) {
#ifndef _WIN32
    host::runtime::reap_adopted_children();
#endif
    if (!host_process_context_) return true;
    const handles::ContextId id = host_process_context_;
    host_process_context_ = 0;
    const auto report = legacy_process_table().close_context(id, on_error);
    if (report.failures.empty()) return true;
    // D-035: a genuine cleanup failure (a process still alive after SIGKILL
    // and the bounded wait) is an error of the execution, never only a warning.
    const auto& first = report.failures.front();
    error = "process cleanup failed: " + first.message;
    if (report.failures.size() > 1) error += " (and " + std::to_string(report.failures.size() - 1) + " more)";
    return false;
}
