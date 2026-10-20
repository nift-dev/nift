// CP16: legacy Host operations through the Host layer (src/rt/host).
// See ParserHost.h and docs/runtime-2/checkpoints/cp16-host-ops-impl.md §9.
#include "ParserHost.h"

#include "FileSystem.h"
#include "HostTrace.h"
#include "Parser.h"
#include "ReadObservation.h"
#include "rt/host/process.h"
#include "rt/host/process_call.h"

#include <limits>


namespace fs = std::filesystem;
namespace host = nift::rt::host;

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
    // Historical legacy run() behaviour, kept as a temporary compatibility
    // exception (cp16-host-ops-impl.md §9.6; CP25 removes it with the legacy
    // launcher): the stages stay in Nift's process group (a signal to that
    // group reaches them; no dedicated group, no terminal handoff), they do
    // not die with Nift (no PDEATHSIG: a hook's child outlives a SIGKILL of
    // Nift and keeps the build lock), no SIGINT/SIGTERM cleanup is installed,
    // background descendants are not tracked, and a launch with no descriptor
    // left for the exec-failure report goes ahead (CP07, `ulimit -n 6`). The
    // Host layer's own run() keeps all of its guarantees.
    request.die_with_parent = false;
    request.own_group = false;
    request.exec_report_optional = true;
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
    host::HostStatus status = host::HostStatus::Ok;
    host::HostError host_error;
    auto group = host::start_traced(parser_host_env(nullptr), std::move(request), status, host_error, /*wait_at_once=*/true);
    if (!group) {
        // Refused before launching anything (an empty pipeline cannot be built
        // by cmd(); a shutdown refusal needs C2, which legacy run() does not install).
        error = host_error.message;
        return false;
    }
    const host::ProcessResult& result = group->wait();
    out = legacy_result(result);
    return true;
#endif
}
