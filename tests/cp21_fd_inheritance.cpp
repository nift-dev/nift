// CP21 D3: atomic duplicate-with-CLOEXEC regression on a real PTY.
#define _XOPEN_SOURCE 700
#include "rt/host/process_terminal.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
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

    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) != 0) {
        acquire.join();
        return 2;
    }
    const pid_t child = ::fork();
    if (child == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        if (pipefd[1] != STDOUT_FILENO) ::close(pipefd[1]);
        ::execl(argv[1], argv[1], static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(pipefd[1]);
    std::string child_output;
    char chunk[256];
    ssize_t got;
    while ((got = ::read(pipefd[0], chunk, sizeof chunk)) > 0) child_output.append(chunk, static_cast<std::size_t>(got));
    ::close(pipefd[0]);
    int status = 0;
    ::waitpid(child, &status, 0);
    acquire.join();
    const bool released = nift::rt::host::posix::terminal_release(&owner);
    const bool scanned = child_output.find("exec_extra_fds=") != std::string::npos;

    std::printf("duplicated_fd=%d child_output=%s\n", duplicated, child_output.c_str());
    check("fallback-acquired", acquired);
    check("fallback-released", released);
    if (!scanned) {
        std::printf("SETUP_FAIL exec child did not scan descriptors\n");
        return 2;
    }
    check("exec-child-scanned-fds", true);
    check("exec-child-no-extra-fds", child_output.find("exec_extra_fds=0") != std::string::npos);
    std::printf("%s\n", failures ? "FAIL CP21 fd inheritance" : "PASS CP21 fd inheritance");
    return failures ? 1 : 0;
}
