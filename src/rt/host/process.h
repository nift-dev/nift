#pragma once
// CP16 Host layer: the process model (design §11; D-029, D-030).
//
// One ProcessGroup is one launch of a pipeline (1..n stages). It provides:
//  - launch failure kept distinct from "launched, then exited nonzero", from
//    timeout and from cancellation (per stage and per group);
//  - one process group per managed pipeline (C3), signalled as a unit;
//  - deadlock-free draining of stdout and stderr (and writing stdin) in one
//    poll() event loop; completion = every stage reaped, never pipe EOF;
//  - captures bounded while running (D-028: NIFT_READ_LIMIT value supplied
//    by the caller as `capture_limit`; exceeding it cancels the group and is
//    reported as limit.exceeded, never truncated);
//  - deadlines and cancellation with scope Child / Pipeline / Tree:
//    SIGTERM, grace (`kill_after`, default 2,000 ms), SIGKILL, bounded wait
//    (`post_kill_wait`, 5,000 ms), then `Lost` (handed to the reaper);
//  - explicit detachment (own session, double fork, pid record only).
//
// Ownership (which execution context owns a group, when it is cleaned up) is
// NOT decided here: it belongs to CP17's handle registry. The host layer only
// supplies the per-kind cleanup hook (process_cleanup.h).
//
// POSIX implementation: process_posix.cpp. Windows: process_win.cpp
// (job-object helpers only, UNTESTED; ProcessGroup reports
// process.unsupported there until the Windows slice is done).
#include "rt/host/host_error.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nift::rt::host {

enum class OutputMode : std::uint8_t {
    FileCapture,  // legacy default for run()/cmd().run(): anonymous regular files, read back after reaping
    PipeCapture,  // pipes drained by the event loop (D-011 opt-in; leftover holders are terminated)
    Stream,       // child inherits Nift's stdout/stderr after `flush_output()` (order with print kept)
    Inherit,      // as Stream without the flush (interactive shell)
    Interactive,  // non-blocking start; stdin pipe; stdout/stderr drained into bounded buffers
    Detached,     // own session, stdio to the null device or a file, never waited
};
const char* output_mode_name(OutputMode mode);

enum class StdinMode : std::uint8_t { Inherit, Null, Text };

struct SpawnStage {
    std::string program;
    std::vector<std::string> args;
    std::string cwd;  // as spelled; empty = Nift's cwd
    std::vector<std::pair<std::string, std::string>> env;  // overrides on top of Nift's environment
    std::string stdin_path;   // first stage only
    std::string stdout_path;  // last stage only
    bool stdout_append = false;
    std::string stderr_path;
    bool stderr_append = false;
    std::vector<std::string> argv() const;
};

using Millis = std::chrono::milliseconds;
using Clock = std::chrono::steady_clock;

struct SpawnRequest {
    std::vector<SpawnStage> stages;
    OutputMode mode = OutputMode::FileCapture;
    StdinMode stdin_mode = StdinMode::Inherit;  // Interactive always uses a pipe
    std::string stdin_text;                      // StdinMode::Text
    std::uint64_t capture_limit = std::uint64_t(1) << 30;  // NIFT_READ_LIMIT value (bytes, per stream)
    std::uint64_t buffer_limit = std::uint64_t(1) << 20;   // Interactive: per-stream buffer before backpressure
    std::optional<Millis> timeout;               // deadline relative to launch; none = wait forever (today)
    Millis kill_after{2000};                     // grace between SIGTERM and SIGKILL (D-030 default)
    Millis post_kill_wait{5000};                 // bound after SIGKILL before a stage is Lost
    bool in_build = false;                       // detaching is refused (process.detach_in_build)
    bool die_with_parent = true;                 // Linux PR_SET_PDEATHSIG(SIGKILL) on managed stages
    // C3: the pipeline is its own process group (signalled as a unit, leftover
    // members found by group, terminal handed off). false: the stages stay in
    // Nift's process group, as legacy run() launched them -- a signal to
    // Nift's group (a supervisor's killpg, Ctrl-C) reaches them; Nift
    // signals them by pid; no leftover tracking (C1) and no terminal handoff.
    bool own_group = true;
    std::function<void()> flush_output;          // Stream: flush Nift's buffered stdout/stderr first
    std::string builtin = "run";                 // message prefix
};

enum class StageState : std::uint8_t { NotStarted, LaunchFailed, Running, Exited, Signaled, Lost };
const char* stage_state_name(StageState state);

struct StageResult {
    StageState state = StageState::NotStarted;
    bool launched = false;
    int exit_code = -1;   // exit status, 128+n when signaled, 127/126 for exec failures, -1 lost/not started
    int signal = 0;       // terminating signal (Signaled)
    std::string error;    // launch failure / "not started" / lost text
    long pid = -1;
};

enum class GroupState : std::uint8_t { Running, Exited, LaunchFailed, Cancelled, TimedOut, LimitExceeded, Lost, Detached };
const char* group_state_name(GroupState state);

struct ProcessResult {
    // Legacy-compatible fields (run()/cmd().run() result objects).
    bool launched = false;
    int exit_code = -1;
    std::string out, err;
    std::string error;
    std::vector<StageResult> stages;
    // New-option fields (only shown when the call used the new options, §11.10).
    GroupState state = GroupState::Running;
    bool cancelled = false;
    bool timed_out = false;
    bool lost = false;
    int signal = 0;                       // the last stage's terminating signal, if any
    std::string capture_limit_exceeded;   // "stdout" / "stderr" when the capture limit was reached
    std::string limit_message;            // "<builtin>: captured stdout of <prog> exceeds the read limit of N bytes (NIFT_READ_LIMIT)"
    // stdin delivery (StdinMode::Text and Interactive writes). An early exit
    // or a closed stdin is not an error: `stdin_complete` is false and the
    // process result is kept. `stdin_error` is set only for a genuine
    // delivery failure (a write error other than EPIPE).
    std::uint64_t stdin_total = 0;
    std::uint64_t stdin_written = 0;
    bool stdin_complete = true;
    std::string stdin_error;
    long pid = -1;                        // first stage
};

std::string signal_name(int signo);  // "SIGTERM", ..., "SIG<n>"

enum class CancelScope : std::uint8_t { Child, Pipeline, Tree };

// ---- lifecycle seam (CP17 integration, cp16-host-ops-impl.md §8) ----
// The process layer reports OS facts; the handle table (CP17) alone decides
// and records the lifecycle (Completed / Failed / Lost). No CP17 type here:
// the binding (process_handles.h) implements the sink.
enum class OsTermination : std::uint8_t { Exited, Deadline, Cancel };  // terminations CP16 starts itself
struct OsOutcome {
    bool stage_unconfirmed = false;  // a stage still unreaped after SIGKILL + bound (handed to the reaper)
    bool members_survived = false;   // leftover group members still present after SIGKILL + bound
    std::string detail;              // "process 'x' (pid N) ..."
};
class LifecycleSink {
public:
    virtual ~LifecycleSink() = default;
    // Once, the first time the process layer itself starts termination:
    // every stage exited (Exited), the deadline expired (Deadline), or
    // cancel() / the capture limit (Cancel). Close, abandonment and context
    // end arrive the other way round, through the cleanup hook.
    virtual void terminating(OsTermination cause) = 0;
    // Each time the group becomes settled (finished, leftover cleanup done):
    // the OS facts. Never called with the group's lock held.
    virtual void settled(const OsOutcome& facts) = 0;
};
enum class CancelReason : std::uint8_t { None, Explicit, Deadline, Limit, Cleanup, Abandoned, Leftover };

// Thread safety: every public member locks the group; the event loop drops
// the lock while it sleeps in poll(), so cancel() from another thread, the
// host loop's deadline timers and context cleanup never wait for a blocked
// read or wait() to finish. Deadlines are enforced even when nobody uses the
// handle: a group that is not finished when a call returns arms a host-loop
// timer for its next deadline or escalation step (and pidfd watches so exits
// are reaped promptly).
class ProcessGroup {
public:
    struct State;
    ~ProcessGroup();
    ProcessGroup(const ProcessGroup&) = delete;
    ProcessGroup& operator=(const ProcessGroup&) = delete;

    // Launches the request. Never blocks on the children. Option/context
    // errors (detach in a build, a deadline on a detached process, an empty
    // pipeline) are returned as a status with `error` set and no group;
    // OS-level launch failures are a group in state LaunchFailed (a value).
    static std::shared_ptr<ProcessGroup> start(SpawnRequest request, HostStatus& status, HostError& error);
    // As start(), for a caller that calls wait() at once (legacy run()): no
    // host-loop timer or watch is armed in between, so a plain blocking call
    // uses no descriptor beyond its own (CP07: capture under `ulimit -n 6`).
    static std::shared_ptr<ProcessGroup> start_for_wait(SpawnRequest request, HostStatus& status, HostError& error);
    // The checks start() makes before launching anything (also used by the
    // traced launch so a refused request emits no v1 spawn line).
    static HostStatus validate(const SpawnRequest& request, HostError& error);
    // Blocking run: start + wait. `status` reports option/context errors as start().
    static ProcessResult run(SpawnRequest request, HostStatus& status, HostError& error);

    // Runs the event loop until the group is settled (finished, and any
    // requested leftover cleanup done) or `until` passes; returns settled.
    // `until` = now: one non-blocking step.
    bool pump(std::optional<Clock::time_point> until);
    // Blocks until finished (deadline and escalation still apply) and returns
    // the result. For a detached group: returns at once (state Detached);
    // natives must refuse wait() on detached handles (process.not_owned).
    const ProcessResult& wait();
    // Starts cancellation (idempotent). Returns whether anything was still
    // running. The sequence completes during pump()/wait().
    bool cancel(CancelScope scope = CancelScope::Pipeline, CancelReason reason = CancelReason::Explicit,
                std::optional<Millis> kill_after = std::nullopt);

    // Interactive I/O. write() queues bytes and pumps without blocking past
    // what the child accepts; after close_stdin() or exit it is
    // process.stdin_closed. read_line() returns the next line without its
    // '\n' (blocking), nullopt at EOF; read() returns up to `max` bytes.
    HostStatus write(std::string_view bytes, HostError& error);
    HostStatus close_stdin(HostError& error);
    std::optional<std::string> read_line();
    std::optional<std::string> read(std::size_t max);

    bool finished() const;
    bool running();             // some stage not yet reaped (detached: best-effort liveness)
    bool detached() const;
    GroupState state() const;
    long pid() const;           // first stage
    long pgid() const;
    std::string argv0() const;
    const SpawnRequest& request() const;
    const ProcessResult& result() const;  // valid once finished()
    std::vector<StageResult> stages() const;  // current per-stage view (launch outcome known right after start)
    // Host-loop entry point (timers, pidfd watches, reaper adoption): one
    // non-blocking step if the group is not busy; re-arms itself.
    void timer_tick();

    // Cleanup support (process_cleanup.h). begin_terminate() starts the §11.4
    // sequence for live stages (scope Tree under the Linux subreaper,
    // Pipeline otherwise) and for leftover group members of an already
    // reaped group (C1), without blocking; pump() then drives it and returns
    // true once the group is settled. terminate() = begin + pump to the end
    // (bounded by kill_after + post_kill_wait).
    struct TerminateOutcome { bool live_stage_terminated = false; bool leftovers_terminated = false; };
    TerminateOutcome begin_terminate(CancelReason reason);
    TerminateOutcome terminate(CancelReason reason);
    // A genuine cleanup failure: a stage or a leftover member still alive
    // after SIGKILL and the bounded wait (handed to the reaper).
    bool cleanup_failed() const;
    // Finished, and any requested leftover cleanup done (detached: no cancel in flight).
    bool settled() const;
    // The OS facts of the settled group (meaningful once settled()).
    OsOutcome os_outcome() const;
    // Attaches the lifecycle sink (the CP17 binding). With `replay`, events
    // that already happened (exit, settle) are delivered at once, in order;
    // without, the current state counts as reported and only later changes
    // are (a run() residue: its lifecycle starts when its context ends).
    void set_lifecycle_sink(std::shared_ptr<LifecycleSink> sink, bool replay = true);
    // True when the group (or, for a detached group, its session) still has a
    // live (non-zombie) member. Linux checks /proc and requires Nift's session
    // for managed groups (guards against pgid reuse); elsewhere kill(-pgid, 0).
    bool has_live_members() const;

private:
    explicit ProcessGroup(std::unique_ptr<State> state);
    static std::shared_ptr<ProcessGroup> start_impl(SpawnRequest request, HostStatus& status, HostError& error, bool arm);
    std::unique_ptr<State> s_;
};

// The background reaper (process_runtime.cpp) adopts abandoned groups.
using ProcessGroupPtr = std::shared_ptr<ProcessGroup>;

} // namespace nift::rt::host
