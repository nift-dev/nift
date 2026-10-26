// CP21 job control for the POSIX Host process layer: stop/continue waits and
// explicit terminal ownership. The state machine itself stays in
// process_posix_loop/lifecycle; this file only exposes those operations.
#ifndef _WIN32
#include "rt/host/process_posix_job.h"

#include "rt/host/process_posix.h"

#include "rt/host/process_posix_lifecycle.h"
#include "rt/host/process_posix_loop.h"
#include "rt/host/process_posix_state.h"

#include <csignal>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace nift::rt::host {

using namespace posix;

namespace {

// Every live stage is stopped. waitid(WNOWAIT) only observes the state; the
// stop status stays available to the shell's later wait/exit path.
bool all_live_stopped(const State& s) {
    bool any = false;
    for (const auto& stage : s.st) {
        if (stage.pid <= 0 || stage.reaped) continue;
        any = true;
        siginfo_t info{};
        if (::waitid(P_PID, static_cast<id_t>(stage.pid), &info,
                     WSTOPPED | WCONTINUED | WNOHANG | WNOWAIT) != 0 ||
            info.si_pid != stage.pid || info.si_code != CLD_STOPPED)
            return false;
    }
    return any;
}

} // namespace

bool ProcessGroup::has_stopped_members() const {
    std::lock_guard<std::mutex> lock(s_->mu);
    for (const auto& stage : s_->st) {
        if (stage.pid <= 0 || stage.reaped) continue;
        siginfo_t info{};
        if (::waitid(P_PID, static_cast<id_t>(stage.pid), &info, WSTOPPED | WCONTINUED | WNOHANG | WNOWAIT) == 0 &&
            info.si_pid == stage.pid && info.si_code == CLD_STOPPED)
            return true;
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
    return ::kill(-static_cast<pid_t>(s.pgid), SIGCONT) == 0;
}

bool ProcessGroup::give_terminal() {
    State& s = *s_;
    OpLock op(s);
    if (s.detached || s.finished || s.pgid <= 0 || !::isatty(STDIN_FILENO)) return false;
    const bool already_handed = s.handoff;
    if (!already_handed) {
        struct sigaction ign {};
        ign.sa_handler = SIG_IGN;
        sigemptyset(&ign.sa_mask);
        // Keep SIGTTOU ignored until restore_terminal(): the shell is in the
        // background while the job runs, so tcsetpgrp() back to the shell
        // must not stop it.
        ::sigaction(SIGTTOU, &ign, &s.old_ttou);
        s.handoff = true;
    }
    if (::tcsetpgrp(STDIN_FILENO, static_cast<pid_t>(s.pgid)) != 0) {
        if (!already_handed) {
            ::sigaction(SIGTTOU, &s.old_ttou, nullptr);
            s.handoff = false;
        }
        return false;
    }
    return true;
}

bool ProcessGroup::reclaim_terminal() {
    State& s = *s_;
    OpLock op(s);
    if (!s.handoff) return false;
    restore_terminal(s);
    return true;
}

} // namespace nift::rt::host
#endif  // !_WIN32
