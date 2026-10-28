// CP16 Host layer, POSIX internals: descriptors and exec preparation.
#ifndef _WIN32
#include "rt/host/process_posix_sys.h"

#include "rt/host/process_posix_launch.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <poll.h>
#include <sys/syscall.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>

namespace nift::rt::host::posix {

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

int fd_above_stdio_keep_cloexec(int fd) {
    if (fd < 0) return fd;
    if (fd <= 2) {
        const int high = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        const int error = errno;
        ::close(fd);
        errno = error;
        return high;
    }
    return fd;  // pipe2(O_CLOEXEC) already set it
}

bool make_pipe(int ends[2]) {
    ends[0] = ends[1] = -1;
    int raw[2];
#ifdef __linux__
    if (::pipe2(raw, O_CLOEXEC) != 0) return false;
    ends[0] = fd_above_stdio_keep_cloexec(raw[0]);
    ends[1] = fd_above_stdio_keep_cloexec(raw[1]);
#else
    if (::pipe(raw) != 0) return false;
    ends[0] = fd_above_stdio_cloexec(raw[0]);
    ends[1] = fd_above_stdio_cloexec(raw[1]);
#endif
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
#ifdef __APPLE__
        int sig = 0; sigset_t pend; sigpending(&pend); if (sigismember(&pend, SIGPIPE)) sigwait(&block, &sig);
#else
        const struct timespec zero = {0, 0};
        while (::sigtimedwait(&block, nullptr, &zero) < 0 && errno == EINTR) {}
#endif
    }
    ::pthread_sigmask(SIG_SETMASK, &old, nullptr);
    errno = error;
    return n;
}

// ---- exec preparation (legacy PreparedProcessPOSIX semantics; the Host
// layer becomes the shared implementation when legacy moves onto it, §11.11)

[[noreturn]] void child_fail(int report_fd, int kind, int error, int code, int detail) {
    if (report_fd >= 0) {
        int payload[3] = {kind, error, detail};
        struct sigaction ign {}, old {};
        ign.sa_handler = SIG_IGN;
        sigemptyset(&ign.sa_mask);
        ::sigaction(SIGPIPE, &ign, &old);
        ssize_t ignored = ::write(report_fd, payload, sizeof payload);
        (void)ignored;
        ::sigaction(SIGPIPE, &old, nullptr);
    }
    ::_exit(code);
}

std::string exec_failure_message(const SpawnStage& stage, const PreparedExec& prepared, int kind, int error,
                                 int detail) {
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
                case SetupGroup:
                    return "cannot create process group for " + stage.program + ": " + std::strerror(error);
                case SetupTerminal:
                    return "cannot take terminal for " + stage.program + ": " + std::strerror(error);
                default: return "redirection setup failed for " + stage.program + ": " + std::strerror(error);
            }
        default: return "cannot execute " + stage.program + ": " + std::strerror(error);
    }
}

void child_wait_handoff(int read_fd, int report_fd) {
    char decision = 0;
    ssize_t got;
    do { got = ::read(read_fd, &decision, 1); } while (got < 0 && errno == EINTR);
    ::close(read_fd);
    if (got != 1 || decision != 'G')
        child_fail(report_fd, ExecSetup, ECANCELED, 126, SetupTerminal);
}

void parent_commit_handoff(int write_fd) {
    if (write_fd < 0) return;
    (void)write_no_sigpipe(write_fd, "G", 1);
    ::close(write_fd);
}

void parent_abort_handoff(int write_fd) {
    if (write_fd >= 0) ::close(write_fd);
}

void child_take_terminal(int report_fd) {
    struct sigaction ign {};
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    struct sigaction prev {};
    ::sigaction(SIGTTOU, &ign, &prev);
    if (::tcsetpgrp(STDIN_FILENO, ::getpid()) != 0)
        child_fail(report_fd, ExecSetup, errno, 126, SetupTerminal);
    struct sigaction dfl {};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    ::sigaction(SIGTTOU, &dfl, nullptr);
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

bool terminal_foreground() {
    if (!::isatty(STDIN_FILENO)) return false;
    return ::tcgetpgrp(STDIN_FILENO) == ::getpgrp();
}

namespace {
std::string pipe_error() { return std::string("cannot create pipe: ") + std::strerror(errno); }
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
                     s.req.terminal != TerminalPolicy::Never &&
                     s.req.stages.front().stdin_path.empty() && terminal_foreground();
}


} // namespace nift::rt::host::posix
#endif  // !_WIN32
