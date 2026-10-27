// CP21 D3: atomic duplicate-with-CLOEXEC regression on a real PTY.
#define _XOPEN_SOURCE 700
#include "rt/host/process_terminal.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

int failures = 0;
using ArmFn = void (*)();
using SeenFn = int (*)();

void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

enum class ScannerKind { Completed, Timeout, Setup };

struct ScannerResult {
    ScannerKind kind = ScannerKind::Setup;
    std::string output;
    int status = 0;
    pid_t child = -1;
};

bool terminate_scanner(pid_t child) {
    if (child <= 0) return true;
    ::kill(-child, SIGKILL);
    ::kill(child, SIGKILL);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < end) {
        int status = 0;
        const pid_t got = ::waitpid(child, &status, WNOHANG);
        if (got == child || (got < 0 && errno == ECHILD)) return true;
        if (got < 0 && errno != EINTR) return true;  // not ours / already gone
        ::usleep(2000);
    }
    return false;
}

ScannerResult run_scanner(const char* path, int timeout_ms) {
    ScannerResult result;
    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) != 0) return result;
    ::fcntl(pipefd[0], F_SETFL, ::fcntl(pipefd[0], F_GETFL) | O_NONBLOCK);
    const pid_t child = ::fork();
    if (child < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return result;
    }
    if (child == 0) {
        ::setpgid(0, 0);
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        if (pipefd[1] != STDOUT_FILENO) ::close(pipefd[1]);
        ::execl(path, path, static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::setpgid(child, child);  // both sides set it; the child may have exited already
    ::close(pipefd[1]);
    result.child = child;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    bool eof = false;
    while (!eof) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            const bool reaped = terminate_scanner(child);
            ::close(pipefd[0]);
            result.kind = reaped ? ScannerKind::Timeout : ScannerKind::Setup;
            return result;
        }
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        struct pollfd pfd {pipefd[0], POLLIN, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(left));
        if (ready < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ready == 0) continue;
        char chunk[256];
        const ssize_t got = ::read(pipefd[0], chunk, sizeof chunk);
        if (got > 0) {
            result.output.append(chunk, static_cast<std::size_t>(got));
        } else if (got == 0) {
            eof = true;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            break;
        }
    }
    ::close(pipefd[0]);
    int status = 0;
    for (;;) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            const bool reaped = terminate_scanner(child);
            result.kind = reaped ? ScannerKind::Timeout : ScannerKind::Setup;
            return result;
        }
        const pid_t got = ::waitpid(child, &status, WNOHANG);
        if (got == child) break;
        if (got < 0 && errno == ECHILD) return result;  // someone else reaped it
        if (got < 0 && errno != EINTR) return result;
        ::usleep(2000);
    }
    result.kind = ScannerKind::Completed;
    result.status = status;
    return result;
}

bool make_controlling_tty() {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || ::grantpt(master) != 0 || ::unlockpt(master) != 0) return false;
    const char* name = ::ptsname(master);
    if (!name) return false;
    if (::setsid() < 0) return false;
    const int slave = ::open(name, O_RDWR);
    if (slave < 0) return false;
    if (::ioctl(slave, TIOCSCTTY, 0) != 0) return false;
    ::fcntl(master, F_SETFD, FD_CLOEXEC);
    ::dup2(slave, STDIN_FILENO);
    if (slave > STDERR_FILENO) ::close(slave);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    auto arm = reinterpret_cast<ArmFn>(::dlsym(RTLD_DEFAULT, "cp21_fallback_arm"));
    auto seen = reinterpret_cast<SeenFn>(::dlsym(RTLD_DEFAULT, "cp21_dup_fd"));
    if (!arm || !seen || !make_controlling_tty()) {
        std::printf("SETUP_FAIL fallback arms or PTY unavailable\n");
        return 2;
    }

    int owner = 0;
    bool acquired = false;
    arm();
    std::thread acquire([&] { acquired = nift::rt::host::posix::terminal_acquire(&owner); });
    const int deadline = 3000;
    int waited = 0;
    while (seen() < 0 && waited < deadline) {
        ::usleep(1000);
        ++waited;
    }
    const int duplicated = seen();
    if (duplicated < 0) {
        acquire.join();
        std::printf("SETUP_FAIL duplicate window not observed\n");
        return 2;
    }

    const ScannerResult scan = run_scanner(argv[1], 5000);
    acquire.join();
    const bool released = nift::rt::host::posix::terminal_release(&owner);

    std::printf("duplicated_fd=%d child_kind=%d child_status=%d child_output=%s\n",
                duplicated, static_cast<int>(scan.kind), scan.status, scan.output.c_str());
    check("fallback-acquired", acquired);
    check("fallback-released", released);
    if (scan.kind == ScannerKind::Timeout) {
        std::printf("TIMEOUT exec child did not finish and was killed/reaped\n");
        return 2;
    }
    if (scan.kind != ScannerKind::Completed || !WIFEXITED(scan.status)) {
        std::printf("SETUP_FAIL exec child did not complete normally\n");
        return 2;
    }

    const std::size_t marker = scan.output.find("exec_extra_fds=");
    if (marker == std::string::npos ||
        scan.output.find("exec_extra_fds=", marker + 1) != std::string::npos) {
        std::printf("SETUP_FAIL exec child scan marker missing or duplicated\n");
        return 2;
    }
    const char* number = scan.output.c_str() + marker + std::string("exec_extra_fds=").size();
    char* end = nullptr;
    errno = 0;
    const long count = std::strtol(number, &end, 10);
    if (errno != 0 || end == number || count < 0) {
        std::printf("SETUP_FAIL exec child scan count invalid\n");
        return 2;
    }
    const std::size_t extras = static_cast<std::size_t>(count);
    std::size_t seen_lines = 0;
    for (std::size_t at = scan.output.find("extra_fd="); at != std::string::npos;
         at = scan.output.find("extra_fd=", at + 9))
        ++seen_lines;
    if (seen_lines != extras || (extras == 0 && scan.output.find("extra_fd=") != std::string::npos)) {
        std::printf("SETUP_FAIL exec child descriptor lines disagree with count\n");
        return 2;
    }

    const int exit_code = WEXITSTATUS(scan.status);
    const int expected_exit = count == 0 ? 0 : 1;
    check("exec-child-scanned-fds", true);
    check("exec-child-valid-status", exit_code == expected_exit);
    check("exec-child-no-extra-fds", count == 0);
    std::printf("%s\n", failures ? "FAIL CP21 fd inheritance" : "PASS CP21 fd inheritance");
    return failures ? 1 : 0;
}
