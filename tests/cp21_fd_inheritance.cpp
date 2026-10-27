// CP21 D3: atomic duplicate-with-CLOEXEC regression on a real PTY.
#define _XOPEN_SOURCE 700
#include "rt/host/process_terminal.h"
#include "cp21_fd_protocol.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <dirent.h>
#include <fstream>
#include <map>
#include <poll.h>
#include <set>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/prctl.h>
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

bool proc_available() {
    DIR* dir = ::opendir("/proc");
    if (!dir) return false;
    ::closedir(dir);
    std::ifstream self("/proc/self/stat");
    std::string line;
    return static_cast<bool>(std::getline(self, line));
}

bool tagged_processes(const std::string& tag, std::set<pid_t>& found) {
    found.clear();
    DIR* dir = ::opendir("/proc");
    if (!dir) return false;
    const std::string needle = "CP21_SCANNER_TAG=" + tag;
    bool ok = true;
    while (dirent* entry = ::readdir(dir)) {
        char* end = nullptr;
        errno = 0;
        const long pid = std::strtol(entry->d_name, &end, 10);
        if (errno != 0 || end == entry->d_name || *end != 0 || pid <= 0) continue;
        if (pid == static_cast<long>(::getpid())) continue;
        std::ifstream environ("/proc/" + std::to_string(pid) + "/environ", std::ios::binary);
        if (!environ) continue;
        std::string bytes((std::istreambuf_iterator<char>(environ)), std::istreambuf_iterator<char>());
        if (bytes.find(needle) != std::string::npos) found.insert(static_cast<pid_t>(pid));
    }
    ::closedir(dir);
    return ok;
}

bool reap_bounded(pid_t child, const std::chrono::steady_clock::time_point& end) {
    bool direct_reaped = child <= 0;
    while (std::chrono::steady_clock::now() < end) {
        int status = 0;
        const pid_t got = ::waitpid(-1, &status, WNOHANG);
        if (got == child) direct_reaped = true;
        if (got > 0) continue;
        if (got < 0 && errno == EINTR) continue;
        if (got < 0 && errno == ECHILD) return true;  // no children remain
        if (got < 0) return false;
        ::usleep(2000);
    }
    return direct_reaped;
}

bool terminate_scanner(pid_t child, const std::string& tag) {
    if (child <= 0) return true;
    std::set<pid_t> targets;
    if (!tagged_processes(tag, targets)) return false;
    targets.insert(child);
    const pid_t pgid = ::getpgid(child);
    if (pgid == child) {
        if (::kill(-child, SIGKILL) != 0 && errno != ESRCH) return false;
    }
    for (pid_t pid : targets)
        if (::kill(pid, SIGKILL) != 0 && errno != ESRCH) return false;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    if (!reap_bounded(child, end)) return false;
    while (std::chrono::steady_clock::now() < end) {
        if (!tagged_processes(tag, targets)) return false;
        if (targets.empty()) return true;
        for (pid_t pid : targets)
            if (::kill(pid, SIGKILL) != 0 && errno != ESRCH) return false;
        if (!reap_bounded(child, end)) return false;
        ::usleep(2000);
    }
    if (!tagged_processes(tag, targets)) return false;
    return targets.empty();
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
            const bool reaped = terminate_scanner(child, (::getenv("CP21_SCANNER_TAG") ? ::getenv("CP21_SCANNER_TAG") : ""));
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
            const bool reaped = terminate_scanner(child, (::getenv("CP21_SCANNER_TAG") ? ::getenv("CP21_SCANNER_TAG") : ""));
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
#ifdef __linux__
    if (::prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0) {
        std::printf("SETUP_FAIL subreaper unavailable\n");
        return 2;
    }
    if (!proc_available()) {
        std::printf("SETUP_FAIL proc unavailable\n");
        return 2;
    }
    const std::string tag = "cp21-scanner-" + std::to_string(::getpid());
    if (::setenv("CP21_SCANNER_TAG", tag.c_str(), 1) != 0) {
        std::printf("SETUP_FAIL scanner tag unavailable\n");
        return 2;
    }
#endif
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
        std::printf("TIMEOUT exec child did not finish and was killed/reaped cleaned=1\n");
        return 2;
    }
    if (scan.kind != ScannerKind::Completed || !WIFEXITED(scan.status)) {
        std::printf("SETUP_FAIL exec child did not complete normally\n");
        return 2;
    }

    const auto protocol = cp21::parse_fd_protocol(scan.output, true, WEXITSTATUS(scan.status));
    if (!protocol.valid) {
        std::printf("SETUP_FAIL protocol: %s\n", protocol.error.c_str());
        return 2;
    }
    check("exec-child-scanned-fds", true);
    check("exec-child-valid-status", true);
    check("exec-child-no-extra-fds", protocol.count == 0);
    std::printf("%s\n", failures ? "FAIL CP21 fd inheritance" : "PASS CP21 fd inheritance");
    return failures ? 1 : 0;
}
