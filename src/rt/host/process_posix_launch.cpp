// CP16 Host layer, POSIX internals: launching a pipeline. Everything between
// fork() and exec in a child uses async-signal-safe calls only.
#ifndef _WIN32
#include "rt/host/process_posix_launch.h"

#include "rt/host/process_posix_lifecycle.h"
#include "rt/host/process_posix_loop.h"
#include "rt/host/process_runtime.h"

#include "ProcessEnvironment.h"  // nift_environment_mutex: consistent environ snapshots
#include "ProcessPOSIX.h"        // nift_process_launch_mutex: launches serialised with legacy's

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

extern char** environ;

namespace nift::rt::host::posix {

namespace {
std::string pipe_error() { return std::string("cannot create pipe: ") + std::strerror(errno); }

// Child side of stage i: group, terminal, parent death, cwd and redirections,
// then exec. Async-signal-safe only.
[[noreturn]] void run_child(Launch& l, std::size_t i, const int link[2], const int report[2], const int pid_pipe[2],
                            pid_t target_pgid) {
    State& s = l.s;
    const bool last = i + 1 == s.req.stages.size();
    if (report[0] >= 0) ::close(report[0]);
    if (l.detached) {
        ::close(pid_pipe[0]);
        if (::setsid() < 0) child_fail(report[1], ExecSetup, errno, 126, SetupGroup);
        const pid_t grandchild = ::fork();
        if (grandchild < 0) child_fail(report[1], ExecOther, errno, 126);
        if (grandchild > 0) {
            const long value = grandchild;
            ssize_t ignored = ::write(pid_pipe[1], &value, sizeof value);
            (void)ignored;
            ::_exit(0);
        }
        ::close(pid_pipe[1]);
    } else {
        if (s.req.own_group && ::setpgid(0, target_pgid) != 0) child_fail(report[1], ExecSetup, errno, 126, SetupGroup);
        if (l.want_handoff && i == 0) {
            struct sigaction ign {};
            ign.sa_handler = SIG_IGN;
            sigemptyset(&ign.sa_mask);
            struct sigaction prev {};
            ::sigaction(SIGTTOU, &ign, &prev);
            ::tcsetpgrp(STDIN_FILENO, ::getpid());
            struct sigaction dfl {};
            dfl.sa_handler = SIG_DFL;
            sigemptyset(&dfl.sa_mask);
            ::sigaction(SIGTTOU, &dfl, nullptr);
        }
#ifdef __linux__
        if (s.req.die_with_parent) {
            ::prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
            if (::getppid() != l.parent_pid) ::raise(SIGKILL);
        }
#endif
    }
    PreparedExec& p = l.prepared[i];
    const int rep = report[1];
    if (!p.cwd.empty() && ::chdir(p.cwd.c_str()) != 0) child_fail(rep, ExecSetup, errno, 126, SetupChdir);
    // stdin
    if (l.prev_read >= 0) {
        if (!child_dup2(l.prev_read, STDIN_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect);
    } else if (!p.stdin_path.empty()) {
        child_open_onto(rep, p.stdin_path, O_RDONLY, STDIN_FILENO, SetupStdin);
    } else if (l.in_child >= 0) {
        if (!child_dup2(l.in_child, STDIN_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect);
    } else if (l.null_stdin) {
        child_open_onto(rep, "/dev/null", O_RDONLY, STDIN_FILENO, SetupStdin);
    }
    // stdout
    if (!last) {
        if (!child_dup2(link[1], STDOUT_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect);
    } else if (!p.stdout_path.empty()) {
        const int flags = O_WRONLY | O_CREAT | (p.stdout_append ? O_APPEND : O_TRUNC);
        child_open_onto(rep, p.stdout_path, flags, STDOUT_FILENO, SetupStdout);
    } else if (l.out_child >= 0) {
        if (!child_dup2(l.out_child, STDOUT_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect);
    } else if (l.detached) {
        child_open_onto(rep, "/dev/null", O_WRONLY, STDOUT_FILENO, SetupStdout);
    }
    // stderr
    if (s.req.stages[i].stderr_to_stdout) {
        if (!child_dup2(STDOUT_FILENO, STDERR_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect);
    } else if (!p.stderr_path.empty()) {
        const int flags = O_WRONLY | O_CREAT | (p.stderr_append ? O_APPEND : O_TRUNC);
        child_open_onto(rep, p.stderr_path, flags, STDERR_FILENO, SetupStderr);
    } else if (l.err_child >= 0) {
        if (!child_dup2(l.err_child, STDERR_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect);
    } else if (l.detached) {
        child_open_onto(rep, "/dev/null", O_WRONLY, STDERR_FILENO, SetupStderr);
    }
    // Every Nift descriptor is close-on-exec; nothing else to close.
    p.exec(rep);
}

// Reads a fixed-size payload until EOF (EINTR retried); returns bytes read.
std::size_t read_payload(int fd, void* into, std::size_t size) {
    std::size_t got = 0;
    while (fd >= 0 && got < size) {
        const ssize_t n = ::read(fd, static_cast<char*>(into) + got, size - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        got += static_cast<std::size_t>(n);
    }
    return got;
}

// Parent side after fork() of stage i.
void record_stage(Launch& l, std::size_t i, pid_t pid, int link[2], int report[2], int pid_pipe[2]) {
    State& s = l.s;
    if (report[1] >= 0) ::close(report[1]);
    pid_t stage_pid = pid;
    if (l.detached) {
        ::close(pid_pipe[1]);
        long value = -1;
        const std::size_t got = read_payload(pid_pipe[0], &value, sizeof value);
        ::close(pid_pipe[0]);
        int st = 0;
        while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        s.pgid = pid;  // session and group id of the detached process
        if (got == sizeof value && value > 0) stage_pid = static_cast<pid_t>(value);
        else stage_pid = -1;
    } else {
        if (s.req.own_group) {
            if (i == 0) s.pgid = pid;
            ::setpgid(pid, s.pgid);  // both sides set it: no window where the child is outside its group
        }
        if (l.want_handoff && i == 0) ::tcsetpgrp(STDIN_FILENO, pid);
    }
    auto& stage = s.st[i];
    runtime::test_event(&s, "spawn", stage_pid, s.pgid, static_cast<long>(i), "");  // a = pid, b = pgid, c = stage
    stage.pid = stage_pid;
    stage.result.pid = stage_pid;
    stage.result.launched = stage_pid > 0;
    stage.result.state = stage_pid > 0 ? StageState::Running : StageState::LaunchFailed;
    int payload[3] = {0, 0, 0};
    const std::size_t got = read_payload(report[0], payload, sizeof payload);
    if (report[0] >= 0) ::close(report[0]);
    if (got == sizeof payload) {
        stage.exec_failed = true;
        stage.result.launched = false;
        stage.result.error = exec_failure_message(s.req.stages[i], l.prepared[i], payload[0], payload[1], payload[2]);
        stage.result.exit_code = payload[0] == ExecNotFound || payload[0] == ExecInterpreter ? 127 : 126;
        if (l.detached) stage.result.state = StageState::LaunchFailed;
    }
    if (!l.detached && stage_pid > 0 && s.pidfds_ok) {
        stage.pidfd = open_pidfd(stage_pid);
        if (stage.pidfd < 0) s.pidfds_ok = false;
    }
    if (l.prev_read >= 0) ::close(l.prev_read);
    if (link[1] >= 0) ::close(link[1]);
    l.prev_read = link[0];
    runtime::index_add(l.key, runtime::IndexedGroup{s.pgid, s.unreaped_pids(), l.detached});
    runtime::note_managed_pgid(s.pgid);
}
} // namespace

void open_endpoints(Launch& l) {
    State& s = l.s;
    const OutputMode mode = l.mode;
    const bool pipes = mode == OutputMode::PipeCapture || mode == OutputMode::Interactive;
    if (mode == OutputMode::FileCapture) {
        s.out_file = open_capture_file();
        s.err_file = open_capture_file();
        if (s.out_file < 0 || s.err_file < 0) l.fail("cannot create capture files");
        l.out_child = s.out_file;
        l.err_child = s.err_file;
    } else if (pipes) {
        int o[2], e[2];
        if (!make_pipe(o)) {
            l.fail(pipe_error());
        } else if (!make_pipe(e)) {
            l.fail(pipe_error());
            ::close(o[0]);
            ::close(o[1]);
        } else {
            s.out_fd = o[0]; l.out_child = o[1];
            s.err_fd = e[0]; l.err_child = e[1];
            set_nonblocking(s.out_fd);
            set_nonblocking(s.err_fd);
        }
    }
    l.stdin_pipe =
        !s.launch_failed && !l.detached && (mode == OutputMode::Interactive || s.req.stdin_mode == StdinMode::Text);
    if (l.stdin_pipe) {
        int i[2];
        if (!make_pipe(i)) {
            l.fail(pipe_error());
        } else {
            l.in_child = i[0];
            s.in_fd = i[1];
            set_nonblocking(s.in_fd);
            if (mode != OutputMode::Interactive) {
                s.in_pending = s.req.stdin_text;
                s.stdin_total = s.req.stdin_text.size();
                s.close_stdin_when_drained = true;
            }
        }
    }
    l.null_stdin = l.detached || s.req.stdin_mode == StdinMode::Null;
    const bool blocking = mode == OutputMode::FileCapture || mode == OutputMode::PipeCapture ||
                          mode == OutputMode::Stream || mode == OutputMode::Inherit;
    l.want_handoff = !l.detached && s.req.own_group && blocking && !l.stdin_pipe && !l.null_stdin &&
                     s.req.stages.front().stdin_path.empty() && terminal_foreground();
}

void prepare_stages(Launch& l) {
    State& s = l.s;
    // Environment snapshot once per launch.
    std::map<std::string, std::string> base_env;
    {
        std::lock_guard<std::mutex> lock(nift_environment_mutex());
        for (char** p = environ; p && *p; ++p) {
            const std::string entry = *p;
            const auto eq = entry.find('=');
            if (eq != std::string::npos) base_env[entry.substr(0, eq)] = entry.substr(eq + 1);
        }
    }
    l.prepared.reserve(s.req.stages.size());
    for (const auto& stage : s.req.stages) l.prepared.emplace_back(stage, base_env);
    l.parent_pid = ::getpid();
    runtime::index_add(l.key, runtime::IndexedGroup{-1, {}, l.detached});
    if (l.want_handoff) {
        struct sigaction ign {};
        ign.sa_handler = SIG_IGN;
        sigemptyset(&ign.sa_mask);
        ::sigaction(SIGTTOU, &ign, &s.old_ttou);
        s.handoff = true;
    }
}

bool launch_stage(Launch& l, std::size_t i) {
    State& s = l.s;
    const bool last = i + 1 == s.req.stages.size();
    int link[2] = {-1, -1};
    if (!last && !make_pipe(link)) { l.fail(pipe_error()); return false; }
    int report[2] = {-1, -1};
    if (!make_pipe(report)) {
        if (!l.detached && s.req.exec_report_optional && (errno == EMFILE || errno == ENFILE)) {
            // Out of descriptors: launch without the exec-failure report
            // (legacy CP07: capture works under `ulimit -n 6`). An exec
            // failure is then seen only as exit status 127/126.
            report[0] = report[1] = -1;
        } else {
            l.fail(pipe_error());
            if (link[0] >= 0) { ::close(link[0]); ::close(link[1]); }
            return false;
        }
    }
    int pid_pipe[2] = {-1, -1};  // detached: child1 reports the grandchild's pid
    if (l.detached && !make_pipe(pid_pipe)) {
        l.fail(pipe_error());
        ::close(report[0]);
        ::close(report[1]);
        return false;
    }
    const pid_t target_pgid = i == 0 ? 0 : s.pgid;
    const pid_t pid = ::fork();
    if (pid < 0) {
        l.fail("cannot start " + s.req.stages[i].program + ": " + std::strerror(errno));
        for (int fd : {link[0], link[1], report[0], report[1], pid_pipe[0], pid_pipe[1]}) if (fd >= 0) ::close(fd);
        return false;
    }
    if (pid == 0) run_child(l, i, link, report, pid_pipe, target_pgid);
    record_stage(l, i, pid, link, report, pid_pipe);
    return true;
}

void close_child_ends(Launch& l) {
    if (l.prev_read >= 0) ::close(l.prev_read);
    // The child ends live only in the children now (capture files stay: they are read back).
    if (l.mode != OutputMode::FileCapture) {
        if (l.out_child >= 0) ::close(l.out_child);
        if (l.err_child >= 0) ::close(l.err_child);
    }
    if (l.in_child >= 0) ::close(l.in_child);
}

} // namespace nift::rt::host::posix

namespace nift::rt::host {

using namespace posix;

std::shared_ptr<ProcessGroup> ProcessGroup::start_impl(SpawnRequest req, HostStatus& status, HostError& error,
                                                       bool arm) {
    status = HostStatus::Ok;
    status = validate(req, error);
    if (status != HostStatus::Ok) return nullptr;
    const bool detached = req.mode == OutputMode::Detached;

    auto state = std::make_unique<State>();
    State& s = *state;
    s.req = std::move(req);
    s.detached = detached;
    s.kill_after = s.req.kill_after;
    s.st.resize(s.req.stages.size());
    std::shared_ptr<ProcessGroup> group(new ProcessGroup(std::move(state)));
    s.self = group;
    const void* key = group.get();

    if (s.req.mode == OutputMode::Stream && s.req.flush_output) s.req.flush_output();
    std::fflush(nullptr);  // no duplicated stdio buffers in children

    Launch l(s);
    l.key = key;
    l.detached = detached;
    l.mode = s.req.mode;
    std::unique_lock<std::mutex> launch_lock(nift_process_launch_mutex());
    open_endpoints(l);
    prepare_stages(l);
    for (std::size_t i = 0; i < s.req.stages.size() && !s.launch_failed; ++i)
        if (!launch_stage(l, i)) break;
    close_child_ends(l);
    launch_lock.unlock();

    if (s.launch_failed) {
        // Parent-side failure: stages that did start are killed and reaped
        // (bounded); every stage that never started carries the parent-side
        // error, as legacy does. Nothing is left running.
        for (auto& stage : s.st)
            if (stage.pid <= 0 && stage.result.error.empty()) stage.result.error = s.parent_error;
        if (!detached) begin_cancel(s, CancelScope::Pipeline, CancelReason::Cleanup, Millis(0));
        s.cancelled = false;
        s.leftover_started = true;
        close_fd(s.out_fd);
        close_fd(s.err_fd);
        group->wait();
        return group;
    }
    if (detached) {
        auto& stage = s.st.front();
        if (stage.pid > 0) {
            // Ours to reap only under the subreaper (ECHILD otherwise) -- also
            // when its exec failed: it exits at once, reparented to Nift.
            if (!stage.exec_failed) runtime::note_detached_session(s.pgid);
            runtime::reaper_add_pid(stage.pid);
        }
        stage.reaped = true;  // never waited by Nift
        s.result.state = stage.exec_failed || stage.pid <= 0 ? GroupState::LaunchFailed : GroupState::Detached;
        s.result.launched = !stage.exec_failed && stage.pid > 0;
        s.result.error = stage.result.error;
        s.result.exit_code = stage.exec_failed ? stage.result.exit_code : -1;
        s.result.pid = stage.pid;
        s.result.stages = {stage.result};
        s.finished = true;
        runtime::index_remove(key);
        return group;
    }
    if (s.req.timeout) s.deadline = Clock::now() + *s.req.timeout;  // monotonic
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.in_fd >= 0 && !s.in_pending.empty()) write_stdin(s);
    else if (s.in_fd >= 0 && s.close_stdin_when_drained) close_fd(s.in_fd);
    if (arm) rearm(s);
    return group;
}

} // namespace nift::rt::host
#endif  // !_WIN32
