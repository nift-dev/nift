// CP21 D3: atomic duplicate-with-CLOEXEC regression on a real PTY.
// Scanner execution and cleanup live in cp21_scanner_supervisor; this driver
// never signals or enumerates its own children.
#define _XOPEN_SOURCE 700
#include "rt/host/process_terminal.h"
#include "cp21_fd_protocol.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

int failures = 0;
using ArmFn = void (*)();
using SeenFn = int (*)();

void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
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

std::string temp_path(const char* prefix) {
    char name[128];
    std::snprintf(name, sizeof name, "/tmp/%s-XXXXXX", prefix);
    const int fd = ::mkstemp(name);
    if (fd >= 0) ::close(fd);
    return name;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const char* scanner = argv[1];
    const char* supervisor = argv[2];
    const bool unrelated_flag = argc > 3 && std::string(argv[3]) == "--unrelated-child";
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
    int waited = 0;
    while (seen() < 0 && waited < 3000) {
        ::usleep(1000);
        ++waited;
    }
    const int duplicated = seen();
    if (duplicated < 0) {
        acquire.join();
        std::printf("SETUP_FAIL duplicate window not observed\n");
        return 2;
    }

    pid_t unrelated = -1;
    if (unrelated_flag) {
        unrelated = ::fork();
        if (unrelated == 0) {
            ::execl("/bin/sleep", "cp21-unrelated-sleep", "60", static_cast<char*>(nullptr));
            ::_exit(127);
        }
        if (unrelated < 0) {
            acquire.join();
            std::printf("SETUP_FAIL unrelated child failed\n");
            return 2;
        }
    }

    const std::string output_path = temp_path("cp21-scan-output");
    const std::string status_path = temp_path("cp21-scan-status");
    const pid_t supervisor_pid = ::fork();
    if (supervisor_pid == 0) {
        ::setpgid(0, 0);
        ::execl(supervisor, supervisor, scanner, output_path.c_str(), status_path.c_str(), "5000",
                static_cast<char*>(nullptr));
        ::_exit(127);
    }
    if (supervisor_pid < 0) {
        acquire.join();
        std::printf("SETUP_FAIL supervisor fork failed\n");
        return 2;
    }
    ::setpgid(supervisor_pid, supervisor_pid);
    int status = 0;
    bool supervisor_ok = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t got = ::waitpid(supervisor_pid, &status, WNOHANG);
        if (got == supervisor_pid) {
            supervisor_ok = true;
            break;
        }
        if (got < 0 && errno == ECHILD) break;
        if (got < 0 && errno != EINTR) break;
        ::usleep(2000);
    }
    if (!supervisor_ok) {
        ::kill(-supervisor_pid, SIGKILL);
        ::kill(supervisor_pid, SIGKILL);
        ::waitpid(supervisor_pid, &status, 0);
    }
    acquire.join();
    const bool released = nift::rt::host::posix::terminal_release(&owner);
    const std::string status_text = read_file(status_path);
    const std::string output = read_file(output_path);
    ::unlink(status_path.c_str());
    ::unlink(output_path.c_str());

    bool unrelated_survived = true;
    if (unrelated > 0) {
        unrelated_survived = (::kill(unrelated, 0) == 0);
        check("unrelated-child-survived", unrelated_survived);
        ::kill(unrelated, SIGKILL);
        int ignored = 0;
        ::waitpid(unrelated, &ignored, 0);
    }

    std::printf("duplicated_fd=%d supervisor_status=%s child_output=%s\n", duplicated,
                status_text.c_str(), output.c_str());
    check("fallback-acquired", acquired);
    check("fallback-released", released);
    if (!supervisor_ok) {
        std::printf("SETUP_FAIL supervisor did not complete\n");
        return 2;
    }
    if (status_text.rfind("SETUP_FAIL", 0) == 0) {
        std::printf("%s", status_text.c_str());
        return 2;
    }
    if (status_text.rfind("TIMEOUT_CLEANUP_FAIL", 0) == 0) {
        std::printf("SETUP_FAIL cleanup: %s", status_text.c_str());
        return 2;
    }
    if (status_text.find("TIMEOUT_CLEANED") != std::string::npos) {
        std::printf("TIMEOUT exec child did not finish and was killed/reaped cleaned=1\n");
        return 2;
    }
    if (status_text.rfind("SIGNALED", 0) == 0) {
        std::printf("SETUP_FAIL protocol: child did not exit normally\n");
        return 2;
    }
    if (status_text.rfind("SETUP_FAIL", 0) != 0 &&
        status_text.rfind("COMPLETED ", 0) != 0) {
        std::printf("SETUP_FAIL protocol: supervisor result unavailable\n");
        return 2;
    }
    if (status_text.rfind("COMPLETED_CLEANED_DESCENDANTS", 0) == 0) {
        std::printf("SETUP_FAIL protocol: scanner left descendants\n");
        return 2;
    }
    const int exit_code = std::atoi(status_text.c_str() + std::strlen("COMPLETED "));
    const auto protocol = cp21::parse_fd_protocol(output, true, exit_code);
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
