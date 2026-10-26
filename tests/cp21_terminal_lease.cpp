// CP21 Host terminal-lease contract on a real PTY.
#include "rt/host/process.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace nift::rt::host;

namespace {

int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

bool make_controlling_tty() {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || ::grantpt(master) != 0 || ::unlockpt(master) != 0) return false;
    char name[128];
    if (::ptsname_r(master, name, sizeof name) != 0) return false;
    if (::setsid() < 0) return false;
    const int slave = ::open(name, O_RDWR);
    if (slave < 0) return false;
    if (::ioctl(slave, TIOCSCTTY, 0) != 0) return false;
    ::dup2(slave, STDIN_FILENO);
    if (slave > STDERR_FILENO) ::close(slave);
    return true;
}

std::shared_ptr<ProcessGroup> spawn_sleep() {
    SpawnRequest request;
    SpawnStage stage;
    stage.program = "/bin/sleep";
    stage.args = {"5"};
    request.stages = {stage};
    request.mode = OutputMode::Inherit;
    request.terminal = TerminalPolicy::Never;
    request.own_group = true;
    HostStatus status = HostStatus::Ok;
    HostError error;
    auto group = ProcessGroup::start_for_wait(std::move(request), status, error);
    return status == HostStatus::Ok ? group : nullptr;
}

} // namespace

int main() {
    if (!make_controlling_tty()) {
        std::printf("FAIL cannot create controlling PTY\n");
        return 1;
    }
    struct sigaction original {};
    struct sigaction dfl {};
    ::sigaction(SIGTTOU, nullptr, &original);
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    ::sigaction(SIGTTOU, &dfl, nullptr);

    auto a = spawn_sleep();
    auto b = spawn_sleep();
    check("groups-started", a && b);
    if (!a || !b) return 1;

    check("first-owner-acquires", a->give_terminal());
    check("competing-owner-rejected", !b->give_terminal());
    check("owner-reclaims", a->reclaim_terminal());
    // A former competing owner must not be able to take the lease back by
    // reclaiming after the real owner released it (the review's overlap).
    check("non-owner-cannot-reclaim", !b->reclaim_terminal());
    check("original-disposition-restored", [] {
        struct sigaction current {};
        ::sigaction(SIGTTOU, nullptr, &current);
        return current.sa_handler == SIG_DFL;
    }());
    check("former-owner-cannot-reclaim", !a->reclaim_terminal());

    check("second-owner-acquires-after-release", b->give_terminal());
    check("second-owner-reclaims", b->reclaim_terminal());
    check("disposition-restored-again", [] {
        struct sigaction current {};
        ::sigaction(SIGTTOU, nullptr, &current);
        return current.sa_handler == SIG_DFL;
    }());

    a->terminate(CancelReason::Cleanup);
    b->terminate(CancelReason::Cleanup);
    ::sigaction(SIGTTOU, &original, nullptr);
    std::printf("%s\n", failures ? "FAIL CP21 terminal lease" : "PASS CP21 terminal lease");
    return failures ? 1 : 0;
}
