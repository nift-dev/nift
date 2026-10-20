// CP16 Host layer: POSIX process model (design §11; D-029/D-030), public
// operations of ProcessGroup. See process.h for the contract; internals:
// process_posix_sys (descriptors, exec), process_posix_state (group state),
// process_posix_launch (start), process_posix_loop (state transitions),
// process_posix_lifecycle (driving over time, CP17 seam).
#ifndef _WIN32
#include "rt/host/process_posix.h"

#include "rt/host/process_posix_lifecycle.h"
#include "rt/host/process_posix_loop.h"
#include "rt/host/process_posix_state.h"
#include "rt/host/process_posix_sys.h"
#include "rt/host/process_runtime.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <unistd.h>

namespace nift::rt::host {

using namespace posix;

std::vector<std::string> SpawnStage::argv() const {
    std::vector<std::string> out;
    out.push_back(program);
    out.insert(out.end(), args.begin(), args.end());
    return out;
}

ProcessGroup::ProcessGroup(std::unique_ptr<State> state) : s_(std::move(state)) {}

ProcessGroup::~ProcessGroup() {
    if (!s_) return;
    State& s = *s_;
    for (auto id : s.loop_tasks) runtime::loop_cancel(id);
    s.loop_tasks.clear();
    if (!s.finished && !s.detached) {
        // Never leak: a group destroyed while running is killed and reaped
        // (bounded). Normal paths finish groups through wait()/cleanup hooks.
        s.phase = State::Phase::None;
        begin_cancel(s, CancelScope::Pipeline, CancelReason::Cleanup, Millis(0));
        const auto bound = Clock::now() + s.req.post_kill_wait;
        while (!step(s, this) && Clock::now() < bound) {
            if (s.out_fd >= 0) drain(s, s.out_fd, s.out, true);
            if (s.err_fd >= 0) drain(s, s.err_fd, s.err, false);
            ::usleep(5000);
        }
        if (!s.finished) {
            for (auto& stage : s.st) if (stage.pid > 0 && !stage.reaped) runtime::reaper_add_pid(stage.pid);
            for (auto& stage : s.st) stage.reaped = true;
            finish(s, this);
        }
    }
    close_fd(s.out_fd);
    close_fd(s.err_fd);
    close_fd(s.in_fd);
    close_fd(s.out_file);
    close_fd(s.err_file);
    runtime::index_remove(this);
}

const char* output_mode_name(OutputMode mode) {
    switch (mode) {
        case OutputMode::FileCapture: return "file-capture";
        case OutputMode::PipeCapture: return "pipe-capture";
        case OutputMode::Stream: return "stream";
        case OutputMode::Inherit: return "inherit";
        case OutputMode::Interactive: return "interactive";
        case OutputMode::Detached: return "detached";
    }
    return "?";
}

std::shared_ptr<ProcessGroup> ProcessGroup::start(SpawnRequest req, HostStatus& status, HostError& error) {
    return start_impl(std::move(req), status, error, true);
}

std::shared_ptr<ProcessGroup> ProcessGroup::start_for_wait(SpawnRequest req, HostStatus& status, HostError& error) {
    return start_impl(std::move(req), status, error, false);
}

ProcessResult ProcessGroup::run(SpawnRequest request, HostStatus& status, HostError& error) {
    // Blocking: wait() drives the group to the end itself; no host-loop arming.
    auto group = start_impl(std::move(request), status, error, false);
    if (!group) return ProcessResult();
    return group->wait();
}

void ProcessGroup::timer_tick() {
    State& s = *s_;
    auto keep = s.self.lock();  // the reaper may release the last reference below
    std::unique_lock<std::mutex> lk(s.mu, std::try_to_lock);
    if (!lk.owns_lock()) {
        // Busy: the holder is between poll() calls and re-arms when it lets
        // go; retry shortly in case that happened just before this fired.
        std::weak_ptr<ProcessGroup> w = s.self;
        runtime::loop_at(Clock::now() + Millis(5), [w] { if (auto g = w.lock()) g->timer_tick(); });
        return;
    }
    runtime::test_event(&s, "tick", s.pgid, s.finished ? 1 : 0, static_cast<long>(s.phase));
    if (s.detached) detached_step(s);
    else {
        if (!s.finished) run_loop(s, this, lk, Clock::now(), nullptr);
        if (s.finished) leftover_step(s);
    }
    const bool done = group_settled(s);
    rearm(s);
    auto deliver = collect_lifecycle(s);
    lk.unlock();
    if (deliver) deliver();  // the CP17 binding reports failures; nothing is printed here
    if (done) runtime::reaper_release(this);
}

bool ProcessGroup::pump(std::optional<Clock::time_point> until) {
    State& s = *s_;
    OpLock op(s);
    if (s.detached) return detached_step(s);
    if (!s.finished && !run_loop(s, this, op.lk, until, nullptr)) return false;
    leftover_step(s);
    return group_settled(s);
}

const ProcessResult& ProcessGroup::wait() {
    State& s = *s_;
    OpLock op(s);
    if (!s.finished && !s.detached) {
        s.waiting = true;
        run_loop(s, this, op.lk, std::nullopt, nullptr);
    }
    return s.result;
}

bool ProcessGroup::cancel(CancelScope scope, CancelReason reason, std::optional<Millis> kill_after) {
    State& s = *s_;
    OpLock op(s);
    const Millis grace = kill_after ? *kill_after : s.req.kill_after;
    if (s.detached) {
        if (s.result.state != GroupState::Detached || s.pgid <= 0 || !group_has_members(s)) return false;
        if (s.phase != State::Phase::None) return true;
        s.kill_after = grace;
        s.phase_at = Clock::now();
        if (grace.count() <= 0) {
            if (!kill_suppressed_for_test()) runtime::host_kill(-s.pgid, SIGKILL, "detached-cancel", &s);
            s.phase = State::Phase::Kill;
        } else {
            runtime::host_kill(-s.pgid, SIGTERM, "detached-cancel", &s);
            s.phase = State::Phase::Term;
        }
        return true;
    }
    if (s.finished) return false;
    if (reason == CancelReason::Cleanup || reason == CancelReason::Abandoned)
        s.waiting = true;  // nobody will read: finish
    reap_some(s, this);
    std::size_t child = 0;
    if (scope == CancelScope::Child) {
        child = s.st.size();
        for (std::size_t i = 0; i < s.st.size(); ++i) if (s.st[i].pid > 0 && !s.st[i].reaped) { child = i; break; }
        if (child == s.st.size()) return false;
    }
    if (s.all_reaped()) return false;
    begin_cancel(s, scope, reason, grace, child);
    return true;
}

HostStatus ProcessGroup::write(std::string_view bytes, HostError& error) {
    State& s = *s_;
    OpLock op(s);
    if (s.in_fd < 0 || s.stdin_closed || s.finished)
        return make_error(error, "process.stdin_closed", s.req.builtin + ": write: the process's stdin is closed");
    s.in_pending.append(bytes.data(), bytes.size());
    s.stdin_total += bytes.size();
    run_loop(s, this, op.lk, Clock::now(), nullptr);
    return HostStatus::Ok;
}

HostStatus ProcessGroup::close_stdin(HostError& error) {
    State& s = *s_;
    OpLock op(s);
    if (s.stdin_closed || s.in_fd < 0) {
        if (s.req.mode != OutputMode::Interactive)
            return make_error(error, "process.stdin_closed",
                              s.req.builtin + ": close_stdin: the process has no stdin pipe");
        return HostStatus::Ok;  // idempotent
    }
    // Queued bytes are delivered first, then EOF.
    s.close_stdin_when_drained = true;
    s.stdin_closed = true;
    write_stdin(s);
    if (s.in_pending.empty()) close_fd(s.in_fd);
    return HostStatus::Ok;
}

std::optional<std::string> ProcessGroup::read_line() {
    State& s = *s_;
    OpLock op(s);
    auto has_line = [&] { return s.out.data.find('\n', s.out.pos) != std::string::npos; };
    if (!s.finished && !has_line() && s.out_fd >= 0)
        run_loop(s, this, op.lk, std::nullopt, [&] { return has_line() || s.out_fd < 0; });
    if (s.finished) {
        // After finish the unread output lives in result.out.
        std::string& rest = s.result.out;
        if (rest.empty()) return std::nullopt;
        const auto nl = rest.find('\n');
        std::string line = rest.substr(0, nl);
        rest.erase(0, nl == std::string::npos ? rest.size() : nl + 1);
        return line;
    }
    Buffer& b = s.out;
    const auto nl = b.data.find('\n', b.pos);
    if (nl == std::string::npos) {
        if (b.retained() == 0) return std::nullopt;  // EOF
        std::string line = b.data.substr(b.pos);
        b.pos = b.data.size();
        return line;
    }
    std::string line = b.data.substr(b.pos, nl - b.pos);
    b.pos = nl + 1;
    b.compact();
    return line;
}

std::optional<std::string> ProcessGroup::read(std::size_t max) {
    State& s = *s_;
    OpLock op(s);
    if (!s.finished && s.out.retained() == 0 && s.out_fd >= 0)
        run_loop(s, this, op.lk, std::nullopt, [&] { return s.out.retained() > 0 || s.out_fd < 0; });
    if (s.finished) {
        std::string& rest = s.result.out;
        if (rest.empty()) return std::nullopt;
        std::string chunk = rest.substr(0, max);
        rest.erase(0, chunk.size());
        return chunk;
    }
    if (s.out.retained() == 0) return std::nullopt;
    std::string chunk = s.out.data.substr(s.out.pos, max);
    s.out.pos += chunk.size();
    s.out.compact();
    return chunk;
}

bool ProcessGroup::finished() const {
    std::lock_guard<std::mutex> lock(s_->mu);
    return s_->finished;
}

bool ProcessGroup::running() {
    State& s = *s_;
    OpLock op(s);
    if (s.detached) return s.result.state == GroupState::Detached && group_has_members(s);
    if (s.finished) return false;
    reap_some(s, this);
    return !s.all_reaped();
}

bool ProcessGroup::detached() const { return s_->detached; }  // fixed at start
GroupState ProcessGroup::state() const {
    std::lock_guard<std::mutex> lock(s_->mu);
    if (s_->finished) return s_->result.state;
    return GroupState::Running;
}
long ProcessGroup::pid() const {
    return s_->st.empty() ? -1 : static_cast<long>(s_->st.front().pid);
}                                                                        // fixed at start
long ProcessGroup::pgid() const { return static_cast<long>(s_->pgid); }  // fixed at start
std::string ProcessGroup::argv0() const {
    return s_->req.stages.empty() ? std::string() : s_->req.stages.front().program;
}
const SpawnRequest& ProcessGroup::request() const { return s_->req; }
const ProcessResult& ProcessGroup::result() const {
    std::lock_guard<std::mutex> lock(s_->mu);
    return s_->result;
}
std::vector<StageResult> ProcessGroup::stages() const {
    std::lock_guard<std::mutex> lock(s_->mu);
    if (s_->finished) return s_->result.stages;
    std::vector<StageResult> out;
    for (const auto& stage : s_->st) out.push_back(stage.result);
    return out;
}
bool ProcessGroup::has_live_members() const { return group_has_members(*s_); }

ProcessGroup::TerminateOutcome ProcessGroup::begin_terminate(CancelReason reason) {
    State& s = *s_;
    OpLock op(s);
    TerminateOutcome outcome;
    if (s.detached) return outcome;  // explicitly detached: never touched by managed cleanup
    s.cleanup_requested = true;
    s.waiting = true;  // unread interactive output is discarded with the group
    if (!s.finished) {
        reap_some(s, this);
        if (!s.all_reaped()) {
            outcome.live_stage_terminated = true;
            begin_cancel(s, runtime::subreaper_enabled() ? CancelScope::Tree : CancelScope::Pipeline, reason,
                         s.req.kill_after);
        }
        run_loop(s, this, op.lk, Clock::now(), nullptr);  // one non-blocking step
    }
    if (s.finished && s.left_phase == State::Phase::None) {
        if (s.pgid > 0 && group_has_members(s)) {
            outcome.leftovers_terminated = true;
            leftover_step(s);  // sends SIGTERM now
        } else {
            // Nothing left in the group: settled now, not at the next pump
            // (the lifecycle may already report this group settled).
            leftover_step(s);
        }
    }
    return outcome;
}

ProcessGroup::TerminateOutcome ProcessGroup::terminate(CancelReason reason) {
    TerminateOutcome outcome = begin_terminate(reason);
    while (!pump(Clock::now() + Millis(10))) {}
    return outcome;
}

bool ProcessGroup::settled() const {
    std::lock_guard<std::mutex> lock(s_->mu);
    return group_settled(*s_);
}

OsOutcome ProcessGroup::os_outcome() const {
    std::lock_guard<std::mutex> lock(s_->mu);
    return outcome_of(*s_);
}

void ProcessGroup::set_lifecycle_sink(std::shared_ptr<LifecycleSink> sink, bool replay) {
    State& s = *s_;
    OpLock op(s);
    s.sink = std::move(sink);
    s.term_notified = !replay && (s.finished || s.all_reaped());
    s.settle_notified = !replay && group_settled(s);
}

bool ProcessGroup::cleanup_failed() const {
    std::lock_guard<std::mutex> lock(s_->mu);
    return s_->lost || s_->leftover_failed;
}

std::string signal_name(int signo) {
    switch (signo) {
        case SIGHUP: return "SIGHUP";
        case SIGINT: return "SIGINT";
        case SIGQUIT: return "SIGQUIT";
        case SIGILL: return "SIGILL";
        case SIGABRT: return "SIGABRT";
        case SIGFPE: return "SIGFPE";
        case SIGKILL: return "SIGKILL";
        case SIGSEGV: return "SIGSEGV";
        case SIGPIPE: return "SIGPIPE";
        case SIGALRM: return "SIGALRM";
        case SIGTERM: return "SIGTERM";
        case SIGUSR1: return "SIGUSR1";
        case SIGUSR2: return "SIGUSR2";
        case SIGBUS: return "SIGBUS";
        default: return "SIG" + std::to_string(signo);
    }
}

} // namespace nift::rt::host
#endif  // !_WIN32
