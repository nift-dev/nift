// CP16 Host layer, POSIX internals: state transitions of one group.
#ifndef _WIN32
#include "rt/host/process_posix_loop.h"

#include "rt/host/process_posix_lifecycle.h"
#include "rt/host/process_posix_sys.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

namespace nift::rt::host::posix {

bool kill_suppressed_for_test() {
#ifdef NIFT_HOST_TEST_HOOKS
    return runtime::test_hooks().suppress_sigkill.load();
#else
    return false;
#endif
}
bool group_has_members(const State& s) {
    if (s.pgid <= 0) return false;
#ifdef __linux__
    // Fast path (every legacy run() asks): no process at all in the group.
    if (::kill(-s.pgid, 0) != 0 && errno == ESRCH) return false;
    const long session = s.detached ? static_cast<long>(s.pgid) : static_cast<long>(::getsid(0));
    return !runtime::proc_group_members(s.pgid, session).empty();
#else
    return ::kill(-s.pgid, 0) == 0;
#endif
}

void send_signal(State& s, int sig) {
    if (s.scope == CancelScope::Child) {
        if (s.child_stage < s.st.size()) {
            const auto& stage = s.st[s.child_stage];
            if (stage.pid > 0 && !stage.reaped && !(sig == SIGKILL && kill_suppressed_for_test()))
                runtime::host_kill(stage.pid, sig, "child", &s);  // unreaped: no pid reuse
        }
        return;
    }
    // Pipeline: every stage at once (never stage by stage, §11.4). While any
    // stage is unreaped the pgid cannot be reused; afterwards only with live
    // members (Linux: same session).
    std::vector<long> tree;
    // Stages outside their own process group (own_group = false: legacy run()'s
    // historical semantics) have no group to signal their descendants through;
    // like the legacy launcher's kill_stage_tree (feature-host-deps D-028),
    // cancellation reaches the stages' descendants by parentage (Linux /proc),
    // collected before the stages are signalled (they would be reparented).
    if (s.scope == CancelScope::Tree || s.pgid <= 0) tree = runtime::proc_descendants(s.unreaped_pids());
    if (sig == SIGKILL && kill_suppressed_for_test()) return;
    if (s.pgid > 0 && (!s.all_reaped() || group_has_members(s))) runtime::host_kill(-s.pgid, sig, "pipeline", &s);
    if (s.pgid <= 0)  // stages in Nift's own group (own_group = false): by pid, unreaped only (no reuse)
        for (const auto& stage : s.st)
            if (stage.pid > 0 && !stage.reaped) runtime::host_kill(stage.pid, sig, "pipeline-pid", &s);
    for (long pid : tree) runtime::host_kill(pid, sig, "tree", &s);
}

void close_stdin_end(State& s) {
    if (s.in_pos < s.in_pending.size()) s.stdin_incomplete = true;  // undelivered input: not an error by itself
    close_fd(s.in_fd);
    s.in_pending.clear();
    s.in_pos = 0;
    s.stdin_closed = true;
}

void begin_cancel(State& s, CancelScope scope, CancelReason reason, Millis kill_after, std::size_t child_stage) {
    if (s.phase != State::Phase::None) return;  // idempotent: one sequence per group
    s.scope = scope;
    s.reason = reason;
    s.child_stage = child_stage;
    s.kill_after = kill_after;
    if (reason == CancelReason::Explicit || reason == CancelReason::Cleanup || reason == CancelReason::Abandoned)
        s.cancelled = true;
    if (reason == CancelReason::Deadline) s.timed_out = true;
    if (reason == CancelReason::Cleanup || reason == CancelReason::Abandoned)
        s.waiting = true;                                 // unread output is discarded
    s.phase_at = Clock::now();
    runtime::test_event(&s, "cancel", s.pgid, static_cast<long>(reason), static_cast<long>(scope));
    if (kill_after.count() <= 0) {
        send_signal(s, SIGKILL);
        s.phase = State::Phase::Kill;
    } else {
        send_signal(s, SIGTERM);
        s.phase = State::Phase::Term;
    }
    // Then stdin: a child blocked reading it sees EOF. Signal first -- closing
    // it first let a child act on the EOF (run its next command, write output)
    // before the signal arrived (r2: `deadlines` under TSan captured "never").
#ifdef NIFT_HOST_TEST_HOOKS
    if (const int gap = runtime::test_hooks().cancel_gap_us.load()) ::usleep(static_cast<useconds_t>(gap));
#endif
    if (scope != CancelScope::Child) close_stdin_end(s);
}

void record_exit(StageRt& stage, int status) {
    runtime::test_event(nullptr, "reap", stage.pid, status, 0,
                        WIFSIGNALED(status) ? "signaled" : "exited");  // a = pid, b = wait status
    stage.reaped = true;
    if (stage.pidfd >= 0) { ::close(stage.pidfd); stage.pidfd = -1; }
    auto& r = stage.result;
    if (WIFEXITED(status)) {
        r.exit_code = WEXITSTATUS(status);
        r.state = StageState::Exited;
    } else if (WIFSIGNALED(status)) {
        r.signal = WTERMSIG(status);
        r.exit_code = 128 + r.signal;
        r.state = StageState::Signaled;
    } else {
        r.exit_code = 1;
        r.state = StageState::Exited;
    }
    if (stage.exec_failed) { r.state = StageState::LaunchFailed; r.launched = false; r.signal = 0; }
}

void reap_some(State& s, const void* key) {
    bool changed = false;
    for (auto& stage : s.st) {
        if (stage.pid <= 0 || stage.reaped) continue;
        int status = 0;
        pid_t got;
        do { got = ::waitpid(stage.pid, &status, WNOHANG); } while (got < 0 && errno == EINTR);
        if (got == stage.pid) { record_exit(stage, status); changed = true; }
        else if (got < 0 && errno == ECHILD) {
            // Reaped by someone else (SIGCHLD ignored, or a foreign waitpid(-1)).
            stage.reaped = true;
            if (stage.pidfd >= 0) { ::close(stage.pidfd); stage.pidfd = -1; }
            if (!stage.exec_failed) {
                stage.result.state = StageState::Lost;
                stage.result.exit_code = -1;
                stage.result.error = "exit status unavailable (reaped elsewhere)";
            } else stage.result.state = StageState::LaunchFailed;
            changed = true;
        }
    }
    if (changed) runtime::index_update_pids(key, s.unreaped_pids());
}

// Reads what is available now. Returns false when the fd reached EOF (closed).
void drain(State& s, int& fd, Buffer& b, bool is_out) {
    char chunk[kChunk];
    while (fd >= 0) {
        std::size_t room = kChunk;
        const bool soft = s.req.mode == OutputMode::Interactive && !s.waiting && !s.limit_hit;
        if (soft) {
            if (b.retained() >= s.req.buffer_limit) return;  // backpressure: the child blocks
            room = std::min<std::size_t>(room, s.req.buffer_limit - b.retained());
        }
        ssize_t n;
        do { n = ::read(fd, chunk, room); } while (n < 0 && errno == EINTR);
        if (n == 0) {
            runtime::test_event(&s, "eof", s.pgid, is_out ? 1 : 2);
            close_fd(fd);
            return;
        }  // b: 1 stdout, 2 stderr
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            close_fd(fd);  // EIO etc.: treat as EOF
            return;
        }
        if (s.limit_hit) continue;  // discarding after the limit: keep the child unblocked
        const std::uint64_t after = static_cast<std::uint64_t>(b.retained()) + static_cast<std::uint64_t>(n);
        if (after > s.req.capture_limit) {
            s.limit_hit = true;
            s.limit_stream = is_out ? "stdout" : "stderr";
            s.out = Buffer();
            s.err = Buffer();
            begin_cancel(s, CancelScope::Pipeline, CancelReason::Limit, s.req.kill_after);
            continue;
        }
        b.compact();
        b.data.append(chunk, static_cast<std::size_t>(n));
    }
}

void write_stdin(State& s) {
    while (s.in_fd >= 0 && s.in_pos < s.in_pending.size()) {
        const std::size_t left = s.in_pending.size() - s.in_pos;
        const ssize_t n = write_no_sigpipe(s.in_fd, s.in_pending.data() + s.in_pos, std::min(left, kChunk));
        if (n < 0) {
            const int error = errno;
            if (error == EAGAIN || error == EWOULDBLOCK) return;
            // EPIPE: the reader exited or closed stdin early -- the result is
            // kept, delivery is just incomplete. Anything else is a genuine
            // delivery failure and is reported.
            if (error != EPIPE)
                s.stdin_error = s.req.builtin + ": stdin: write failed after " + std::to_string(s.stdin_written) +
                                " of " + std::to_string(s.stdin_total) + " bytes: " + std::strerror(error);
            close_stdin_end(s);
            return;
        }
        s.in_pos += static_cast<std::size_t>(n);
        s.stdin_written += static_cast<std::uint64_t>(n);
    }
    if (s.in_pos >= s.in_pending.size()) {
        s.in_pending.clear();
        s.in_pos = 0;
        if (s.close_stdin_when_drained && s.in_fd >= 0) close_fd(s.in_fd);
    }
}

void check_file_limits(State& s) {
    if (s.limit_hit || s.req.mode != OutputMode::FileCapture) return;
    struct stat info;
    for (int i = 0; i < 2; ++i) {
        const int fd = i == 0 ? s.out_file : s.err_file;
        if (fd >= 0 && ::fstat(fd, &info) == 0 && static_cast<std::uint64_t>(info.st_size) > s.req.capture_limit) {
            s.limit_hit = true;
            s.limit_stream = i == 0 ? "stdout" : "stderr";
            begin_cancel(s, CancelScope::Pipeline, CancelReason::Limit, s.req.kill_after);
            return;
        }
    }
}

bool read_file_bounded(int fd, std::uint64_t limit, std::string& out) {
    out.clear();
    char chunk[kChunk];
    off_t offset = 0;
    for (;;) {
        ssize_t n;
        do { n = ::pread(fd, chunk, sizeof chunk, offset); } while (n < 0 && errno == EINTR);
        if (n <= 0) return true;
        if (static_cast<std::uint64_t>(out.size()) + static_cast<std::uint64_t>(n) > limit) {
            out.clear();
            return false;
        }
        out.append(chunk, static_cast<std::size_t>(n));
        offset += n;
    }
}

void restore_terminal(State& s) {
    if (!s.handoff) return;
    s.handoff = false;
    ::tcsetpgrp(STDIN_FILENO, ::getpgrp());  // SIGTTOU is still ignored here
    ::sigaction(SIGTTOU, &s.old_ttou, nullptr);
}

void finish(State& s, const void* key) {
    if (s.finished) return;
    if (s.req.mode == OutputMode::FileCapture && !s.limit_hit && !s.launch_failed) {
        std::string data;
        if (s.out_file >= 0 && !read_file_bounded(s.out_file, s.req.capture_limit, data)) {
            s.limit_hit = true;
            s.limit_stream = "stdout";
        } else
            s.out.data = std::move(data);
        if (!s.limit_hit && s.err_file >= 0 && !read_file_bounded(s.err_file, s.req.capture_limit, data)) {
            s.limit_hit = true;
            s.limit_stream = "stderr";
        } else if (!s.limit_hit)
            s.err.data = std::move(data);
    }
    close_fd(s.out_fd);
    close_fd(s.err_fd);
    close_fd(s.in_fd);
    close_fd(s.out_file);
    close_fd(s.err_file);
    for (auto& stage : s.st) if (stage.pidfd >= 0) { ::close(stage.pidfd); stage.pidfd = -1; }
    restore_terminal(s);

    ProcessResult& r = s.result;
    r.stages.clear();
    for (auto& stage : s.st) {
        if (stage.result.state == StageState::Lost) s.lost = true;
        r.stages.push_back(stage.result);
    }
    r.pid = s.st.empty() ? -1 : static_cast<long>(s.st.front().pid);
    r.cancelled = s.cancelled;
    r.timed_out = s.timed_out;
    r.lost = s.lost;
    if (s.launch_failed) {
        r.launched = false;
        r.error = s.parent_error;
        r.exit_code = -1;
        r.state = GroupState::LaunchFailed;
    } else {
        r.launched = true;
        const auto& last = s.st.back().result;
        r.exit_code = last.exit_code;
        r.signal = last.signal;
        if (s.st.size() == 1 && last.state == StageState::LaunchFailed) {
            r.launched = false;
            r.error = last.error;
            r.state = GroupState::LaunchFailed;
        } else if (s.lost) r.state = GroupState::Lost;
        else {
            // The first cause wins: a deadline that expired stays recorded
            // even if the process then exits by itself during the grace period.
            switch (s.reason) {
                case CancelReason::Deadline: r.state = GroupState::TimedOut; break;
                case CancelReason::Limit: r.state = GroupState::LimitExceeded; break;
                case CancelReason::Explicit:
                case CancelReason::Cleanup:
                case CancelReason::Abandoned:
                    r.state = GroupState::Cancelled;
                    break;
                default: r.state = s.limit_hit ? GroupState::LimitExceeded : GroupState::Exited; break;
            }
        }
    }
    r.stdin_total = s.stdin_total;
    r.stdin_written = s.stdin_written;
    r.stdin_complete = !s.stdin_incomplete && s.stdin_written == s.stdin_total;
    r.stdin_error = s.stdin_error;
    if (s.limit_hit) {
        s.out = Buffer();
        s.err = Buffer();
        r.capture_limit_exceeded = s.limit_stream;
        r.limit_message = s.req.builtin + ": captured " + s.limit_stream + " of " + s.req.stages.back().program +
                          " exceeds the read limit of " + std::to_string(s.req.capture_limit) +
                          " bytes (NIFT_READ_LIMIT)";
    }
    r.out = s.out.data.substr(std::min(s.out.pos, s.out.data.size()));
    r.err = s.err.data.substr(std::min(s.err.pos, s.err.data.size()));
    s.out = Buffer();
    s.err = Buffer();
    s.finished = true;
    runtime::test_event(&s, "finish", s.pgid, static_cast<long>(r.state), 0, group_state_name(r.state));
    runtime::forget_managed_pgid(s.pgid);
    runtime::index_remove(key);
}

// One pass of state transitions (no blocking). Returns finished.
bool step(State& s, const void* key) {
    if (s.finished) return true;
    reap_some(s, key);
    const auto now = Clock::now();
    if (s.deadline && now >= *s.deadline && s.phase == State::Phase::None && !s.all_reaped())
        begin_cancel(s, CancelScope::Pipeline, CancelReason::Deadline, s.req.kill_after);
    check_file_limits(s);
    if (s.phase == State::Phase::Term && now >= s.phase_at + s.kill_after) {
        send_signal(s, SIGKILL);
        s.phase = State::Phase::Kill;
        s.phase_at = now;
    } else if (s.phase == State::Phase::Kill && now >= s.phase_at + s.req.post_kill_wait) {
        for (auto& stage : s.st) {
            if (stage.pid <= 0 || stage.reaped) continue;
            stage.reaped = true;  // no longer ours to wait for: the reaper takes it
            if (stage.pidfd >= 0) { ::close(stage.pidfd); stage.pidfd = -1; }
            if (!stage.exec_failed) {
                stage.result.state = StageState::Lost;
                stage.result.exit_code = -1;
                stage.result.error = "process did not exit after SIGKILL";
            }
            runtime::reaper_add_pid(stage.pid);
        }
        close_fd(s.out_fd);  // a holder that survived SIGKILL (another session) cannot block us
        close_fd(s.err_fd);
        s.phase = State::Phase::Done;
    }
    if (!s.all_reaped()) return false;
    if (s.in_fd >= 0) { write_stdin(s); close_stdin_end(s); }  // nobody left to read it
    // Every stage reaped. Pipe modes: read what is there now; if a pipe is
    // still open, a leftover member (or an escapee) holds it.
    if (s.out_fd >= 0) drain(s, s.out_fd, s.out, true);
    if (s.err_fd >= 0) drain(s, s.err_fd, s.err, false);
    if (s.out_fd >= 0 || s.err_fd >= 0) {
        // An idle interactive handle whose pipes a descendant still holds may
        // still produce output the script reads later: keep it open. Once a
        // cancellation (deadline, cancel, cleanup) is under way, finish:
        // unread output moves to result.out and stays readable.
        const bool interactive_unread =
            s.req.mode == OutputMode::Interactive && !s.waiting && s.phase == State::Phase::None;
        if (interactive_unread) return false;
        const bool members = group_has_members(s);
        if (!s.leftover_started) {
            s.leftover_started = true;
            runtime::test_event(&s, "pipe-leftover", s.pgid, members ? 1 : 0);
            if (members && s.phase == State::Phase::None)
                begin_cancel(s, CancelScope::Pipeline, CancelReason::Leftover, s.req.kill_after);
        }
        if (!members) {  // nobody in the group: an escapee (other session) holds the pipe
            close_fd(s.out_fd);
            close_fd(s.err_fd);
        }
        if (s.out_fd >= 0 || s.err_fd >= 0) return false;
    }
    finish(s, key);
    return true;
}

} // namespace nift::rt::host::posix
#endif  // !_WIN32
