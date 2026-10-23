// CP16 Host layer, POSIX internals: descriptors and exec preparation.
#ifndef _WIN32
#include "rt/host/process_posix_sys.h"

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

bool terminal_foreground() {
    if (!::isatty(STDIN_FILENO)) return false;
    return ::tcgetpgrp(STDIN_FILENO) == ::getpgrp();
}

} // namespace nift::rt::host::posix
#endif  // !_WIN32
