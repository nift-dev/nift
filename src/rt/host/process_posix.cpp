// CP16 Host layer: POSIX process model (design §11; D-029/D-030).
// See process.h for the contract. Everything between fork() and exec in a
// child uses async-signal-safe calls only.
#ifndef _WIN32
#include "rt/host/process.h"
#include "rt/host/process_runtime.h"

#include "ProcessEnvironment.h"  // nift_environment_mutex: consistent environ snapshots
#include "ProcessPOSIX.h"        // nift_process_launch_mutex: launches serialised with legacy's

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <map>
#include <poll.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif

extern char** environ;

namespace nift::rt::host {

namespace {

constexpr std::size_t kChunk = 64 * 1024;

int fd_above_stdio_cloexec(int fd) {
    if (fd < 0) return fd;
    if (fd <= 2) {
        const int high = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        const int error = errno;
        ::close(fd);
        errno = error;
        return high;
    }
    if (::fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) { const int e = errno; ::close(fd); errno = e; return -1; }
    return fd;
}

bool make_pipe(int ends[2]) {
    ends[0] = ends[1] = -1;
    int raw[2];
#ifdef __linux__
    if (::pipe2(raw, O_CLOEXEC) != 0) return false;
#else
    if (::pipe(raw) != 0) return false;
#endif
    ends[0] = fd_above_stdio_cloexec(raw[0]);
    ends[1] = fd_above_stdio_cloexec(raw[1]);
    if (ends[0] >= 0 && ends[1] >= 0) return true;
    const int e = errno;
    if (ends[0] >= 0) ::close(ends[0]);
    if (ends[1] >= 0) ::close(ends[1]);
    ends[0] = ends[1] = -1;
    errno = e;
    return false;
}

void set_nonblocking(int fd) { ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK); }
void close_fd(int& fd) { if (fd >= 0) { ::close(fd); fd = -1; } }

int open_capture_file() {
    int fd = -1;
#if defined(__linux__) && defined(O_TMPFILE)
    fd = ::open("/tmp", O_TMPFILE | O_RDWR | O_CLOEXEC, 0600);
#endif
    if (fd < 0) {
        char name[] = "/tmp/nift-proc-XXXXXX";
        fd = ::mkstemp(name);
        if (fd < 0) return -1;
        ::unlink(name);
    }
    return fd_above_stdio_cloexec(fd);
}

int open_pidfd(pid_t pid) {
#if defined(__linux__) && defined(SYS_pidfd_open)
    const long fd = ::syscall(SYS_pidfd_open, pid, 0);
    return fd < 0 ? -1 : static_cast<int>(fd);
#else
    (void)pid;
    return -1;
#endif
}

// Writes without letting SIGPIPE kill Nift: SIGPIPE is blocked in this
// thread around the write and a pending one is consumed (process-wide
// dispositions are left alone; legacy and children are unaffected).
ssize_t write_no_sigpipe(int fd, const char* data, std::size_t size) {
    sigset_t block, old;
    sigemptyset(&block);
    sigaddset(&block, SIGPIPE);
    sigset_t pending_before;
    sigemptyset(&pending_before);
    ::sigpending(&pending_before);
    const bool was_pending = sigismember(&pending_before, SIGPIPE);
    ::pthread_sigmask(SIG_BLOCK, &block, &old);
    ssize_t n;
    do { n = ::write(fd, data, size); } while (n < 0 && errno == EINTR);
    const int error = errno;
    if (n < 0 && error == EPIPE && !was_pending) {
        const struct timespec zero = {0, 0};
#ifdef __APPLE__
        int sig = 0; sigset_t pend; sigpending(&pend); if (sigismember(&pend, SIGPIPE)) sigwait(&block, &sig);
#else
        while (::sigtimedwait(&block, nullptr, &zero) < 0 && errno == EINTR) {}
#endif
    }
    ::pthread_sigmask(SIG_SETMASK, &old, nullptr);
    errno = error;
    return n;
}

// ---- exec preparation (legacy PreparedProcessPOSIX semantics; the Host
// layer becomes the shared implementation when legacy moves onto it, §11.11)
enum ExecFailure : int { ExecNotFound = 1, ExecDenied = 2, ExecOther = 3, ExecInterpreter = 4, ExecSetup = 5 };
enum SetupStep : int { SetupChdir = 1, SetupStdin = 2, SetupStdout = 3, SetupStderr = 4, SetupRedirect = 5, SetupGroup = 6 };

[[noreturn]] void child_fail(int report_fd, int kind, int error, int code, int detail = 0) {
    if (report_fd >= 0) { int payload[3] = {kind, error, detail}; ssize_t ignored = ::write(report_fd, payload, sizeof payload); (void)ignored; }
    ::_exit(code);
}

struct PreparedExec {
    std::vector<std::string> arguments, environment, candidates;
    std::vector<char*> argv, envp, shell_argv;
    std::string cwd, stdin_path, stdout_path, stderr_path;
    bool stdout_append = false, stderr_append = false;

    PreparedExec(const SpawnStage& stage, const std::map<std::string, std::string>& base_env) {
        arguments = stage.argv();
        for (auto& a : arguments) argv.push_back(a.data());
        argv.push_back(nullptr);
        std::map<std::string, std::string> vars = base_env;
        for (const auto& kv : stage.env) vars[kv.first] = kv.second;
        for (const auto& kv : vars) environment.push_back(kv.first + "=" + kv.second);
        for (auto& e : environment) envp.push_back(e.data());
        envp.push_back(nullptr);
        if (stage.program.find('/') != std::string::npos) candidates.push_back(stage.program);
        else {
            auto it = vars.find("PATH");
            const std::string path = it == vars.end() ? "/bin:/usr/bin" : it->second;
            std::size_t start = 0;
            for (;;) {
                const auto end = path.find(':', start);
                const std::string dir = path.substr(start, end == std::string::npos ? end : end - start);
                candidates.push_back(dir.empty() ? stage.program : dir + "/" + stage.program);
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
        shell_argv.push_back(const_cast<char*>("/bin/sh"));
        shell_argv.push_back(nullptr);
        for (std::size_t i = 1; i < arguments.size(); ++i) shell_argv.push_back(arguments[i].data());
        shell_argv.push_back(nullptr);
        cwd = stage.cwd;
        stdin_path = stage.stdin_path;
        stdout_path = stage.stdout_path;
        stderr_path = stage.stderr_path;
        stdout_append = stage.stdout_append;
        stderr_append = stage.stderr_append;
    }

    [[noreturn]] void exec(int report_fd) {
        bool denied = false;
        int last = ENOENT;
        int interpreter = -1;
        for (std::size_t index = 0; index < candidates.size(); ++index) {
            const auto& candidate = candidates[index];
            ::execve(candidate.c_str(), argv.data(), envp.data());
            const int error = errno;
            if (error == ENOEXEC) {
                shell_argv[1] = const_cast<char*>(candidate.c_str());
                ::execve("/bin/sh", shell_argv.data(), envp.data());
                child_fail(report_fd, ExecOther, errno, 126);
            }
            if (error == EACCES) { denied = true; continue; }
            if (error == ENOENT && interpreter < 0 && ::access(candidate.c_str(), F_OK) == 0) interpreter = static_cast<int>(index);
            if (error != ENOENT && error != ENOTDIR) child_fail(report_fd, ExecOther, error, 126);
            last = error;
        }
        if (denied) child_fail(report_fd, ExecDenied, EACCES, 126);
        if (interpreter >= 0) child_fail(report_fd, ExecInterpreter, ENOENT, 127, interpreter);
        child_fail(report_fd, ExecNotFound, last, 127);
    }
};

std::string exec_failure_message(const SpawnStage& stage, const PreparedExec& prepared, int kind, int error, int detail) {
    switch (kind) {
        case ExecNotFound: return "executable not found: " + stage.program;
        case ExecDenied: return "permission denied: " + stage.program;
        case ExecInterpreter: {
            std::string interpreter;
            if (detail >= 0 && static_cast<std::size_t>(detail) < prepared.candidates.size()) {
                std::ifstream in(prepared.candidates[static_cast<std::size_t>(detail)], std::ios::binary);
                std::string line;
                if (std::getline(in, line) && line.rfind("#!", 0) == 0) {
                    const std::size_t b = line.find_first_not_of(" \t", 2);
                    if (b != std::string::npos) {
                        const std::size_t e = line.find_first_of(" \t\r", b);
                        interpreter = line.substr(b, e == std::string::npos ? std::string::npos : e - b);
                    }
                }
            }
            if (!interpreter.empty()) return "interpreter not found: " + interpreter + " (from " + stage.program + ")";
            return "cannot execute " + stage.program + ": its interpreter or loader was not found";
        }
        case ExecSetup:
            switch (detail) {
                case SetupChdir: return "cannot change directory to " + stage.cwd + ": " + std::strerror(error);
                case SetupStdin: return "cannot open stdin " + stage.stdin_path + ": " + std::strerror(error);
                case SetupStdout: return "cannot open stdout " + stage.stdout_path + ": " + std::strerror(error);
                case SetupStderr: return "cannot open stderr " + stage.stderr_path + ": " + std::strerror(error);
                case SetupGroup: return "cannot create process group for " + stage.program + ": " + std::strerror(error);
                default: return "redirection setup failed for " + stage.program + ": " + std::strerror(error);
            }
        default: return "cannot execute " + stage.program + ": " + std::strerror(error);
    }
}

bool child_dup2(int source, int target) {
    if (source == target) return ::fcntl(target, F_SETFD, 0) >= 0;
    int r;
    do { r = ::dup2(source, target); } while (r < 0 && errno == EINTR);
    return r >= 0;
}

void child_open_onto(int report, const std::string& path, int flags, int target, int step) {
    const int fd = ::open(path.c_str(), flags, 0666);
    if (fd < 0) child_fail(report, ExecSetup, errno, 126, step);
    if (!child_dup2(fd, target)) child_fail(report, ExecSetup, errno, 126, SetupRedirect);
    if (fd != target) ::close(fd);
}

struct Buffer {
    std::string data;
    std::size_t pos = 0;  // consumed by read()/read_line()
    std::size_t retained() const { return data.size() - pos; }
    void compact() { if (pos > 0 && pos * 2 >= data.size()) { data.erase(0, pos); pos = 0; } }
};

struct StageRt {
    pid_t pid = -1;
    int pidfd = -1;
    bool reaped = false;
    bool exec_failed = false;
    StageResult result;
};

bool terminal_foreground() {
    if (!::isatty(STDIN_FILENO)) return false;
    return ::tcgetpgrp(STDIN_FILENO) == ::getpgrp();
}

} // namespace

// --------------------------------------------------------------------------
struct ProcessGroup::State {
    SpawnRequest req;
    std::vector<StageRt> st;
    pid_t pgid = -1;
    bool detached = false;
    int out_fd = -1, err_fd = -1, in_fd = -1, out_file = -1, err_file = -1;
    Buffer out, err;
    std::string in_pending;
    std::size_t in_pos = 0;
    bool stdin_closed = false;          // closed by the user, by cancellation or by EPIPE
    bool close_stdin_when_drained = false;
    bool waiting = false;               // wait() lifts the interactive soft cap
    std::optional<Clock::time_point> deadline;
    enum class Phase { None, Term, Kill, Done } phase = Phase::None;
    Clock::time_point phase_at;
    Millis kill_after{2000};
    CancelScope scope = CancelScope::Pipeline;
    std::size_t child_stage = 0;
    CancelReason reason = CancelReason::None;
    bool cancelled = false, timed_out = false, lost = false;
    bool limit_hit = false;
    std::string limit_stream;
    bool leftover_started = false;
    bool finished = false;
    bool launch_failed = false;
    std::string parent_error;
    bool handoff = false;
    struct sigaction old_ttou {};
    bool pidfds_ok = true;
    // Cleanup of leftover group members after the stages are reaped (C1).
    bool cleanup_requested = false;
    Phase left_phase = Phase::None;
    Clock::time_point left_at;
    std::vector<long> left_members;
    ProcessResult result;

    bool all_reaped() const {
        for (const auto& s : st) if (s.pid > 0 && !s.reaped) return false;
        return true;
    }
    std::vector<long> unreaped_pids() const {
        std::vector<long> out;
        for (const auto& s : st) if (s.pid > 0 && !s.reaped) out.push_back(s.pid);
        return out;
    }
};

namespace {

using State = ProcessGroup::State;

bool group_has_members(const State& s) {
    if (s.pgid <= 0) return false;
#ifdef __linux__
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
            if (stage.pid > 0 && !stage.reaped) ::kill(stage.pid, sig);  // unreaped: the pid cannot have been reused
        }
        return;
    }
    // Pipeline: every stage at once (never stage by stage, §11.4). While any
    // stage is unreaped the pgid cannot be reused; afterwards only with live
    // members (Linux: same session).
    std::vector<long> tree;
    if (s.scope == CancelScope::Tree) tree = runtime::proc_descendants(s.unreaped_pids());
    if (s.pgid > 0 && (!s.all_reaped() || group_has_members(s))) ::kill(-s.pgid, sig);
    for (long pid : tree) ::kill(static_cast<pid_t>(pid), sig);
}

void close_stdin_end(State& s) {
    close_fd(s.in_fd);
    s.in_pending.clear();
    s.in_pos = 0;
    s.stdin_closed = true;
}

void begin_cancel(State& s, CancelScope scope, CancelReason reason, Millis kill_after, std::size_t child_stage = 0) {
    if (s.phase != State::Phase::None) return;  // idempotent: one sequence per group
    s.scope = scope;
    s.reason = reason;
    s.child_stage = child_stage;
    s.kill_after = kill_after;
    if (reason == CancelReason::Explicit || reason == CancelReason::Cleanup || reason == CancelReason::Abandoned) s.cancelled = true;
    if (reason == CancelReason::Deadline) s.timed_out = true;
    if (reason == CancelReason::Cleanup || reason == CancelReason::Abandoned) s.waiting = true;  // unread output is discarded
    if (scope != CancelScope::Child) close_stdin_end(s);  // a child blocked reading stdin sees EOF
    s.phase_at = Clock::now();
    if (kill_after.count() <= 0) {
        send_signal(s, SIGKILL);
        s.phase = State::Phase::Kill;
    } else {
        send_signal(s, SIGTERM);
        s.phase = State::Phase::Term;
    }
}

void record_exit(StageRt& stage, int status) {
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
        if (n == 0) { close_fd(fd); return; }
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
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            close_stdin_end(s);  // EPIPE: the reader is gone
            return;
        }
        s.in_pos += static_cast<std::size_t>(n);
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
        if (static_cast<std::uint64_t>(out.size()) + static_cast<std::uint64_t>(n) > limit) { out.clear(); return false; }
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
        if (s.out_file >= 0 && !read_file_bounded(s.out_file, s.req.capture_limit, data)) { s.limit_hit = true; s.limit_stream = "stdout"; }
        else s.out.data = std::move(data);
        if (!s.limit_hit && s.err_file >= 0 && !read_file_bounded(s.err_file, s.req.capture_limit, data)) { s.limit_hit = true; s.limit_stream = "stderr"; }
        else if (!s.limit_hit) s.err.data = std::move(data);
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
        else if (s.timed_out) r.state = GroupState::TimedOut;
        else if (s.cancelled) r.state = GroupState::Cancelled;
        else r.state = GroupState::Exited;
    }
    if (s.limit_hit) {
        s.out = Buffer();
        s.err = Buffer();
        r.capture_limit_exceeded = s.limit_stream;
        r.limit_message = s.req.builtin + ": captured " + s.limit_stream + " of " + s.req.stages.back().program +
                          " exceeds the read limit of " + std::to_string(s.req.capture_limit) + " bytes (NIFT_READ_LIMIT)";
    }
    r.out = s.out.data.substr(std::min(s.out.pos, s.out.data.size()));
    r.err = s.err.data.substr(std::min(s.err.pos, s.err.data.size()));
    s.out = Buffer();
    s.err = Buffer();
    s.finished = true;
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
    // Every stage reaped. Pipe modes: read what is there now; if a pipe is
    // still open, a leftover member (or an escapee) holds it.
    if (s.out_fd >= 0) drain(s, s.out_fd, s.out, true);
    if (s.err_fd >= 0) drain(s, s.err_fd, s.err, false);
    if (s.out_fd >= 0 || s.err_fd >= 0) {
        const bool interactive_unread = s.req.mode == OutputMode::Interactive && !s.waiting;
        if (interactive_unread) return false;  // the script may still read; wait()/cleanup finishes it
        const bool members = group_has_members(s);
        if (!s.leftover_started) {
            s.leftover_started = true;
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

} // namespace

// --------------------------------------------------------------------------
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
    const void* key = group.get();
    const auto& stages = s.req.stages;
    const OutputMode mode = s.req.mode;

    if (mode == OutputMode::Stream && s.req.flush_output) s.req.flush_output();
    std::fflush(nullptr);  // no duplicated stdio buffers in children

    std::unique_lock<std::mutex> launch_lock(nift_process_launch_mutex());

    auto parent_fail = [&](std::string message) {
        s.launch_failed = true;
        s.parent_error = std::move(message);
    };

    // Parent-side endpoints.
    int out_child = -1, err_child = -1, in_child = -1;  // child ends (closed in the parent after forking)
    const bool pipes = mode == OutputMode::PipeCapture || mode == OutputMode::Interactive;
    if (mode == OutputMode::FileCapture) {
        s.out_file = open_capture_file();
        s.err_file = open_capture_file();
        if (s.out_file < 0 || s.err_file < 0) parent_fail("cannot create capture files");
        out_child = s.out_file;
        err_child = s.err_file;
    } else if (pipes) {
        int o[2], e[2];
        if (!make_pipe(o)) parent_fail(std::string("cannot create pipe: ") + std::strerror(errno));
        else if (!make_pipe(e)) { parent_fail(std::string("cannot create pipe: ") + std::strerror(errno)); ::close(o[0]); ::close(o[1]); }
        else {
            s.out_fd = o[0]; out_child = o[1];
            s.err_fd = e[0]; err_child = e[1];
            set_nonblocking(s.out_fd);
            set_nonblocking(s.err_fd);
        }
    }
    const bool stdin_pipe = !s.launch_failed && !detached && (mode == OutputMode::Interactive || s.req.stdin_mode == StdinMode::Text);
    if (stdin_pipe) {
        int i[2];
        if (!make_pipe(i)) parent_fail(std::string("cannot create pipe: ") + std::strerror(errno));
        else {
            in_child = i[0];
            s.in_fd = i[1];
            set_nonblocking(s.in_fd);
            if (mode != OutputMode::Interactive) {
                s.in_pending = s.req.stdin_text;
                s.close_stdin_when_drained = true;
            }
        }
    }
    const bool null_stdin = detached || s.req.stdin_mode == StdinMode::Null;
    const bool blocking = mode == OutputMode::FileCapture || mode == OutputMode::PipeCapture || mode == OutputMode::Stream || mode == OutputMode::Inherit;
    const bool want_handoff = !detached && blocking && !stdin_pipe && !null_stdin && stages.front().stdin_path.empty() && terminal_foreground();

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
    std::vector<PreparedExec> prepared;
    prepared.reserve(stages.size());
    for (const auto& stage : stages) prepared.emplace_back(stage, base_env);

    const pid_t parent_pid = ::getpid();
    int prev_read = -1;
    runtime::index_add(key, runtime::IndexedGroup{-1, {}, detached});
    if (want_handoff) {
        struct sigaction ign {};
        ign.sa_handler = SIG_IGN;
        sigemptyset(&ign.sa_mask);
        ::sigaction(SIGTTOU, &ign, &s.old_ttou);
        s.handoff = true;
    }

    for (std::size_t i = 0; i < stages.size() && !s.launch_failed; ++i) {
        const bool last = i + 1 == stages.size();
        int link[2] = {-1, -1};
        if (!last && !make_pipe(link)) { parent_fail(std::string("cannot create pipe: ") + std::strerror(errno)); break; }
        int report[2];
        if (!make_pipe(report)) {
            parent_fail(std::string("cannot create pipe: ") + std::strerror(errno));
            if (link[0] >= 0) { ::close(link[0]); ::close(link[1]); }
            break;
        }
        int pid_pipe[2] = {-1, -1};  // detached: child1 reports the grandchild's pid
        if (detached && !make_pipe(pid_pipe)) {
            parent_fail(std::string("cannot create pipe: ") + std::strerror(errno));
            ::close(report[0]); ::close(report[1]);
            break;
        }
        const pid_t target_pgid = i == 0 ? 0 : s.pgid;
        const pid_t pid = ::fork();
        if (pid < 0) {
            parent_fail("cannot start " + stages[i].program + ": " + std::strerror(errno));
            for (int fd : {link[0], link[1], report[0], report[1], pid_pipe[0], pid_pipe[1]}) if (fd >= 0) ::close(fd);
            break;
        }
        if (pid == 0) {
            // ---- child: async-signal-safe only ----
            ::close(report[0]);
            if (detached) {
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
                if (::setpgid(0, target_pgid) != 0) child_fail(report[1], ExecSetup, errno, 126, SetupGroup);
                if (want_handoff && i == 0) {
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
                    if (::getppid() != parent_pid) ::raise(SIGKILL);
                }
#endif
            }
            PreparedExec& p = prepared[i];
            const int rep = report[1];
            if (!p.cwd.empty() && ::chdir(p.cwd.c_str()) != 0) child_fail(rep, ExecSetup, errno, 126, SetupChdir);
            // stdin
            if (prev_read >= 0) { if (!child_dup2(prev_read, STDIN_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect); }
            else if (!p.stdin_path.empty()) child_open_onto(rep, p.stdin_path, O_RDONLY, STDIN_FILENO, SetupStdin);
            else if (in_child >= 0) { if (!child_dup2(in_child, STDIN_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect); }
            else if (null_stdin) child_open_onto(rep, "/dev/null", O_RDONLY, STDIN_FILENO, SetupStdin);
            // stdout
            if (!last) { if (!child_dup2(link[1], STDOUT_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect); }
            else if (!p.stdout_path.empty()) child_open_onto(rep, p.stdout_path, O_WRONLY | O_CREAT | (p.stdout_append ? O_APPEND : O_TRUNC), STDOUT_FILENO, SetupStdout);
            else if (out_child >= 0) { if (!child_dup2(out_child, STDOUT_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect); }
            else if (detached) child_open_onto(rep, "/dev/null", O_WRONLY, STDOUT_FILENO, SetupStdout);
            // stderr
            if (!p.stderr_path.empty()) child_open_onto(rep, p.stderr_path, O_WRONLY | O_CREAT | (p.stderr_append ? O_APPEND : O_TRUNC), STDERR_FILENO, SetupStderr);
            else if (err_child >= 0) { if (!child_dup2(err_child, STDERR_FILENO)) child_fail(rep, ExecSetup, errno, 126, SetupRedirect); }
            else if (detached) child_open_onto(rep, "/dev/null", O_WRONLY, STDERR_FILENO, SetupStderr);
            // Every Nift descriptor is close-on-exec; nothing else to close.
            p.exec(rep);
        }
        // ---- parent ----
        ::close(report[1]);
        pid_t stage_pid = pid;
        if (detached) {
            ::close(pid_pipe[1]);
            long value = -1;
            std::size_t got = 0;
            while (got < sizeof value) {
                const ssize_t n = ::read(pid_pipe[0], reinterpret_cast<char*>(&value) + got, sizeof value - got);
                if (n < 0 && errno == EINTR) continue;
                if (n <= 0) break;
                got += static_cast<std::size_t>(n);
            }
            ::close(pid_pipe[0]);
            int st = 0;
            while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            s.pgid = pid;  // session and group id of the detached process
            if (got == sizeof value && value > 0) stage_pid = static_cast<pid_t>(value);
            else stage_pid = -1;
        } else {
            if (i == 0) s.pgid = pid;
            ::setpgid(pid, s.pgid);  // both sides set it: no window where the child is outside its group
            if (want_handoff && i == 0) ::tcsetpgrp(STDIN_FILENO, pid);
        }
        auto& stage = s.st[i];
        stage.pid = stage_pid;
        stage.result.pid = stage_pid;
        stage.result.launched = stage_pid > 0;
        stage.result.state = stage_pid > 0 ? StageState::Running : StageState::LaunchFailed;
        int payload[3] = {0, 0, 0};
        std::size_t got = 0;
        while (got < sizeof payload) {
            const ssize_t n = ::read(report[0], reinterpret_cast<char*>(payload) + got, sizeof payload - got);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            got += static_cast<std::size_t>(n);
        }
        ::close(report[0]);
        if (got == sizeof payload) {
            stage.exec_failed = true;
            stage.result.launched = false;
            stage.result.error = exec_failure_message(stages[i], prepared[i], payload[0], payload[1], payload[2]);
            stage.result.exit_code = payload[0] == ExecNotFound || payload[0] == ExecInterpreter ? 127 : 126;
            if (detached) stage.result.state = StageState::LaunchFailed;
        }
        if (!detached && stage_pid > 0 && s.pidfds_ok) {
            stage.pidfd = open_pidfd(stage_pid);
            if (stage.pidfd < 0) s.pidfds_ok = false;
        }
        if (prev_read >= 0) ::close(prev_read);
        if (link[1] >= 0) ::close(link[1]);
        prev_read = link[0];
        runtime::index_add(key, runtime::IndexedGroup{s.pgid, s.unreaped_pids(), detached});
    }
    if (prev_read >= 0) ::close(prev_read);
    // The child ends live only in the children now.
    if (mode != OutputMode::FileCapture) {
        if (out_child >= 0) ::close(out_child);
        if (err_child >= 0) ::close(err_child);
    }
    if (in_child >= 0) ::close(in_child);
    launch_lock.unlock();

    if (s.launch_failed) {
        // Parent-side failure: stages that did start are killed and reaped
        // (bounded); every stage that never started carries the parent-side
        // error, as legacy does. Nothing is left running.
        for (auto& stage : s.st)
            if (stage.pid <= 0 && stage.result.error.empty()) stage.result.error = s.parent_error;
        if (!detached && s.pgid > 0) begin_cancel(s, CancelScope::Pipeline, CancelReason::Cleanup, Millis(0));
        s.cancelled = false;
        s.leftover_started = true;
        close_fd(s.out_fd);
        close_fd(s.err_fd);
        group->wait();
        return group;
    }
    if (detached) {
        auto& stage = s.st.front();
        if (stage.pid > 0 && !stage.exec_failed) {
            runtime::note_detached_session(s.pgid);
            runtime::reaper_add_pid(stage.pid);  // our child only under the subreaper; ECHILD otherwise
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
    if (s.req.timeout) s.deadline = Clock::now() + *s.req.timeout;
    if (s.in_fd >= 0 && !s.in_pending.empty()) write_stdin(s);
    else if (s.in_fd >= 0 && s.close_stdin_when_drained) close_fd(s.in_fd);
    return group;
}

ProcessResult ProcessGroup::run(SpawnRequest request, HostStatus& status, HostError& error) {
    auto group = start(std::move(request), status, error);
    if (!group) return ProcessResult();
    return group->wait();
}

namespace {
// C1 leftovers: members still in the group after every stage was reaped.
// Started by begin_terminate() (or on the first pump after finish when a
// cleanup was requested); SIGTERM, grace, SIGKILL, bounded wait. Returns
// true when settled (nothing left to do).
bool leftover_step(State& s) {
    if (!s.cleanup_requested || s.left_phase == State::Phase::Done) return true;
    const auto now = Clock::now();
    const bool members = s.pgid > 0 && group_has_members(s);
    if (s.left_phase == State::Phase::None) {
        if (!members) { s.left_phase = State::Phase::Done; return true; }
#ifdef __linux__
        s.left_members = runtime::proc_group_members(s.pgid, static_cast<long>(::getsid(0)));
#endif
        ::kill(-s.pgid, SIGTERM);
        s.left_phase = State::Phase::Term;
        s.left_at = now;
        return false;
    }
    if (!members) {
        s.left_phase = State::Phase::Done;
    } else if (s.left_phase == State::Phase::Term && now >= s.left_at + s.req.kill_after) {
        ::kill(-s.pgid, SIGKILL);
        s.left_phase = State::Phase::Kill;
        s.left_at = now;
    } else if (s.left_phase == State::Phase::Kill && now >= s.left_at + s.req.post_kill_wait) {
        s.left_phase = State::Phase::Done;
    }
    if (s.left_phase != State::Phase::Done) return false;
    for (long pid : s.left_members) runtime::reaper_add_pid(pid);  // ours only under the subreaper
    s.left_members.clear();
    return true;
}
} // namespace

namespace {
bool run_loop(State& s, const void* key, std::optional<Clock::time_point> until, const std::function<bool()>& stop) {
    for (;;) {
        if (step(s, key)) return true;
        if (stop && stop()) return false;
        const auto now = Clock::now();
        Clock::time_point wake = Clock::time_point::max();
        if (until) wake = std::min(wake, *until);
        if (s.deadline && s.phase == State::Phase::None) wake = std::min(wake, *s.deadline);
        if (s.phase == State::Phase::Term) wake = std::min(wake, s.phase_at + s.kill_after);
        if (s.phase == State::Phase::Kill) wake = std::min(wake, s.phase_at + s.req.post_kill_wait);
        const bool tick = !s.pidfds_ok || s.req.mode == OutputMode::FileCapture || s.leftover_started || s.all_reaped();
        if (tick) wake = std::min(wake, now + Millis(s.req.mode == OutputMode::FileCapture ? 20 : 10));
        std::vector<pollfd> fds;
        auto room = [&](const Buffer& b) {
            return !(s.req.mode == OutputMode::Interactive && !s.waiting && !s.limit_hit && b.retained() >= s.req.buffer_limit);
        };
        if (s.out_fd >= 0 && room(s.out)) fds.push_back({s.out_fd, POLLIN, 0});
        if (s.err_fd >= 0 && room(s.err)) fds.push_back({s.err_fd, POLLIN, 0});
        if (s.in_fd >= 0 && s.in_pos < s.in_pending.size()) fds.push_back({s.in_fd, POLLOUT, 0});
        for (const auto& stage : s.st) if (stage.pidfd >= 0 && !stage.reaped) fds.push_back({stage.pidfd, POLLIN, 0});
        int timeout_ms = -1;
        if (wake != Clock::time_point::max()) {
            const auto left = std::chrono::duration_cast<Millis>(wake - now).count();
            timeout_ms = left <= 0 ? 0 : static_cast<int>(std::min<long long>(left + 1, 1 << 30));
        }
        if (fds.empty() && timeout_ms < 0) timeout_ms = 10;  // defensive: never sleep forever with nothing to watch
        const int ready = ::poll(fds.empty() ? nullptr : fds.data(), static_cast<nfds_t>(fds.size()), timeout_ms);
        if (ready > 0) {
            if (s.out_fd >= 0) drain(s, s.out_fd, s.out, true);
            if (s.err_fd >= 0) drain(s, s.err_fd, s.err, false);
            if (s.in_fd >= 0) write_stdin(s);
        }
        if (until && Clock::now() >= *until) {
            const bool done = step(s, key);
            return done;
        }
    }
}
} // namespace

bool ProcessGroup::pump(std::optional<Clock::time_point> until) {
    State& s = *s_;
    if (s.detached) {
        // Only a cancel request has anything to drive.
        if (s.phase == State::Phase::None || s.phase == State::Phase::Done) return true;
        const auto now = Clock::now();
        if (!group_has_members(s)) { s.phase = State::Phase::Done; return true; }
        if (s.phase == State::Phase::Term && now >= s.phase_at + s.kill_after) { ::kill(-s.pgid, SIGKILL); s.phase = State::Phase::Kill; s.phase_at = now; }
        else if (s.phase == State::Phase::Kill && now >= s.phase_at + s.req.post_kill_wait) { s.phase = State::Phase::Done; return true; }
        (void)until;
        return false;
    }
    if (!s.finished && !run_loop(s, this, until, nullptr)) return false;
    return leftover_step(s);
}

const ProcessResult& ProcessGroup::wait() {
    State& s = *s_;
    if (!s.finished && !s.detached) {
        s.waiting = true;
        run_loop(s, this, std::nullopt, nullptr);
        if (runtime::termination_requested()) runtime::park_for_termination();  // C2: Nift ends with the signal
    }
    return s.result;
}

bool ProcessGroup::cancel(CancelScope scope, CancelReason reason, std::optional<Millis> kill_after) {
    State& s = *s_;
    const Millis grace = kill_after ? *kill_after : s.req.kill_after;
    if (s.detached) {
        if (s.result.state != GroupState::Detached || s.pgid <= 0 || !group_has_members(s)) return false;
        if (s.phase != State::Phase::None) return true;
        s.kill_after = grace;
        s.phase_at = Clock::now();
        if (grace.count() <= 0) { ::kill(-s.pgid, SIGKILL); s.phase = State::Phase::Kill; }
        else { ::kill(-s.pgid, SIGTERM); s.phase = State::Phase::Term; }
        return true;
    }
    if (s.finished) return false;
    if (reason == CancelReason::Cleanup || reason == CancelReason::Abandoned) s.waiting = true;  // nobody will read: finish
    reap_some(s, this);
    std::size_t child = 0;
    if (scope == CancelScope::Child) {
        child = s.st.size();
        for (std::size_t i = 0; i < s.st.size(); ++i) if (s.st[i].pid > 0 && !s.st[i].reaped) { child = i; break; }
        if (child == s.st.size()) return false;
    }
    const bool anything = !s.all_reaped();
    if (!anything) return false;
    begin_cancel(s, scope, reason, grace, child);
    return true;
}

HostStatus ProcessGroup::write(std::string_view bytes, HostError& error) {
    State& s = *s_;
    if (s.in_fd < 0 || s.stdin_closed || s.finished)
        return make_error(error, "process.stdin_closed", s.req.builtin + ": write: the process's stdin is closed");
    s.in_pending.append(bytes.data(), bytes.size());
    run_loop(s, this, Clock::now(), nullptr);
    return HostStatus::Ok;
}

HostStatus ProcessGroup::close_stdin(HostError& error) {
    State& s = *s_;
    if (s.stdin_closed || s.in_fd < 0) {
        if (s.req.mode != OutputMode::Interactive)
            return make_error(error, "process.stdin_closed", s.req.builtin + ": close_stdin: the process has no stdin pipe");
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
    auto has_line = [&] { return s.out.data.find('\n', s.out.pos) != std::string::npos; };
    if (!s.finished && !has_line() && s.out_fd >= 0)
        run_loop(s, this, std::nullopt, [&] { return has_line() || s.out_fd < 0; });
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
    if (!s.finished && s.out.retained() == 0 && s.out_fd >= 0)
        run_loop(s, this, std::nullopt, [&] { return s.out.retained() > 0 || s.out_fd < 0; });
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

bool ProcessGroup::finished() const { return s_->finished; }

bool ProcessGroup::running() {
    State& s = *s_;
    if (s.detached) return s.result.state == GroupState::Detached && group_has_members(s);
    if (s.finished) return false;
    reap_some(s, this);
    return !s.all_reaped();
}

bool ProcessGroup::detached() const { return s_->detached; }
GroupState ProcessGroup::state() const {
    if (s_->finished) return s_->result.state;
    return GroupState::Running;
}
long ProcessGroup::pid() const { return s_->st.empty() ? -1 : static_cast<long>(s_->st.front().pid); }
long ProcessGroup::pgid() const { return static_cast<long>(s_->pgid); }
std::string ProcessGroup::argv0() const { return s_->req.stages.empty() ? std::string() : s_->req.stages.front().program; }
const SpawnRequest& ProcessGroup::request() const { return s_->req; }
const ProcessResult& ProcessGroup::result() const { return s_->result; }
std::vector<StageResult> ProcessGroup::stages() const {
    if (s_->finished) return s_->result.stages;
    std::vector<StageResult> out;
    for (const auto& stage : s_->st) out.push_back(stage.result);
    return out;
}
bool ProcessGroup::has_live_members() const { return group_has_members(*s_); }

ProcessGroup::TerminateOutcome ProcessGroup::begin_terminate(CancelReason reason) {
    State& s = *s_;
    TerminateOutcome outcome;
    if (s.detached) return outcome;  // explicitly detached: never touched by managed cleanup
    s.cleanup_requested = true;
    s.waiting = true;  // unread interactive output is discarded with the group
    if (!s.finished) {
        reap_some(s, this);
        if (!s.all_reaped()) {
            outcome.live_stage_terminated = true;
            begin_cancel(s, runtime::subreaper_enabled() ? CancelScope::Tree : CancelScope::Pipeline, reason, s.req.kill_after);
        }
        run_loop(s, this, Clock::now(), nullptr);  // one non-blocking step
    }
    if (s.finished && s.left_phase == State::Phase::None && s.pgid > 0 && group_has_members(s)) {
        outcome.leftovers_terminated = true;
        leftover_step(s);  // sends SIGTERM now
    }
    return outcome;
}

ProcessGroup::TerminateOutcome ProcessGroup::terminate(CancelReason reason) {
    TerminateOutcome outcome = begin_terminate(reason);
    while (!pump(Clock::now() + Millis(10))) {}
    return outcome;
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
