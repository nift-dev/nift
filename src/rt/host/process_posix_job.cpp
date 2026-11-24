// CP21 job control for the POSIX Host process layer: stop/continue waits and
// explicit terminal ownership. The state machine itself stays in
// process_posix_loop/lifecycle; this file only exposes those operations.
#ifndef _WIN32
#include "rt/host/process_posix_job.h"

#include "rt/host/process_posix.h"

#include "rt/host/process_posix_lifecycle.h"
#include "rt/host/process_posix_loop.h"
#include "rt/host/process_posix_state.h"
#include "rt/host/process_terminal.h"

#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace nift::rt::host {

using namespace posix;

namespace {

bool stage_stopped(const StageRt& stage) {
    siginfo_t info{};
    const int result = ::waitid(P_PID, static_cast<id_t>(stage.pid), &info,
                                WSTOPPED | WCONTINUED | WNOHANG | WNOWAIT);
    if (result != 0) return errno == EINTR && stage.drained_stop;
    if (info.si_pid == stage.pid) return info.si_code == CLD_STOPPED;
    // If SIGCONT failed after draining a stop notification, remember the
    // stopped member until a continuation or exit supersedes the record.
    return stage.drained_stop;
}

// Observe stop status without consuming it until we are about to resume.
bool all_live_stopped(const State& s) {
    bool any = false;
    for (const auto& stage : s.st) {
        if (stage.pid <= 0 || stage.reaped) continue;
        any = true;
        if (!stage_stopped(stage)) return false;
    }
    return any;
}

} // namespace

bool ProcessGroup::has_stopped_members() const {
    std::lock_guard<std::mutex> lock(s_->mu);
    for (const auto& stage : s_->st) {
        if (stage.pid <= 0 || stage.reaped) continue;
        if (stage_stopped(stage)) return true;
    }
    return false;
}

ProcessGroup::JobWait ProcessGroup::wait_job_control() {
    State& s = *s_;
    OpLock op(s);
    if (s.detached) return JobWait::Exited;
    s.waiting = true;
    for (;;) {
        if (s.finished) return JobWait::Exited;
        if (all_live_stopped(s)) return JobWait::Stopped;
        // A stopped child does not produce pidfd readiness, so poll the state
        // in short slices while the event loop drains output and reaps exits.
        run_loop(s, this, op.lk, Clock::now() + Millis(2), nullptr);
        if (s.finished) return JobWait::Exited;
        if (all_live_stopped(s)) return JobWait::Stopped;
    }
}

bool ProcessGroup::continue_group() {
    State& s = *s_;
    OpLock op(s);
    if (s.detached || s.finished || s.pgid <= 0) return false;
    // WNOWAIT leaves each reported stop queued. Drain the old stop before
    // resuming so the next foreground wait cannot return it as a new stop
    // before Darwin publishes a continuation event. WSTOPPED cannot reap
    // an exit. A stop that occurs after SIGCONT is still observed normally.
    for (auto& stage : s.st) {
        if (stage.pid <= 0 || stage.reaped) continue;
        siginfo_t info{};
        int result;
        do {
            result = ::waitid(P_PID, static_cast<id_t>(stage.pid), &info, WSTOPPED | WNOHANG);
        } while (result != 0 && errno == EINTR);
        if (result != 0 && errno != ECHILD) return false;
        if (result == 0 && info.si_pid == stage.pid && info.si_code == CLD_STOPPED) stage.drained_stop = true;
    }
    if (::kill(-static_cast<pid_t>(s.pgid), SIGCONT) != 0) return false;
    for (auto& stage : s.st) stage.drained_stop = false;
    return true;
}

bool ProcessGroup::give_terminal() {
    State& s = *s_;
    OpLock op(s);
    if (s.detached || s.finished || s.pgid <= 0) return false;
    if (!terminal_acquire(&s)) return false;
    if (!terminal_set_foreground(static_cast<pid_t>(s.pgid))) {
        s.handoff = false;
        terminal_release(&s);
        return false;
    }
    s.handoff = true;
    return true;
}

bool ProcessGroup::reclaim_terminal() {
    State& s = *s_;
    OpLock op(s);
    return restore_terminal(s);
}

} // namespace nift::rt::host
#endif  // !_WIN32
