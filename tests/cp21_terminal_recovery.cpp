// CP21 terminal-lease failure and recovery contract (real PTY).
#define _XOPEN_SOURCE 700
#include "rt/host/process.h"
#include "rt/host/process_terminal.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

using namespace nift::rt::host;

namespace {

int failures = 0;
using ArmFn = void (*)(int);

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
    ::dup2(slave, STDIN_FILENO);
    if (slave > STDERR_FILENO) ::close(slave);
    return true;
}

bool default_ttou() {
    struct sigaction current {};
    ::sigaction(SIGTTOU, nullptr, &current);
    return current.sa_handler == SIG_DFL;
}

std::shared_ptr<ProcessGroup> spawn(const char* program, const std::vector<std::string>& args,
                                    TerminalPolicy terminal = TerminalPolicy::Never) {
    SpawnRequest request;
    SpawnStage stage;
    stage.program = program;
    stage.args = args;
    request.stages = {stage};
    request.mode = OutputMode::Inherit;
    request.terminal = terminal;
    request.own_group = true;
    HostStatus status = HostStatus::Ok;
    HostError error;
    auto group = ProcessGroup::start_for_wait(std::move(request), status, error);
    return status == HostStatus::Ok ? group : nullptr;
}

void one_shot_signal_restore(ArmFn arm) {
    int a = 0;
    check("signal-one-shot-acquires", nift::rt::host::posix::terminal_acquire(&a));
    arm(1);
    check("signal-one-shot-release-fails", !nift::rt::host::posix::terminal_release(&a));
    check("signal-one-shot-not-owner", !nift::rt::host::posix::terminal_owned_by(&a));
    check("signal-one-shot-retry-restores", nift::rt::host::posix::terminal_release(&a));
    int b = 0;
    check("signal-one-shot-next-acquire", nift::rt::host::posix::terminal_acquire(&b));
    check("signal-one-shot-next-release", nift::rt::host::posix::terminal_release(&b));
    check("signal-one-shot-disposition", default_ttou());
}

void persistent_signal_restore(ArmFn arm) {
    int a = 0;
    check("signal-persistent-acquires", nift::rt::host::posix::terminal_acquire(&a));
    arm(4);
    check("signal-persistent-release-fails", !nift::rt::host::posix::terminal_release(&a));
    check("signal-persistent-retry-fails", !nift::rt::host::posix::terminal_release(&a));
    int b = 0;
    check("signal-persistent-blocks-acquire", !nift::rt::host::posix::terminal_acquire(&b));
    arm(0);
    check("signal-persistent-recovers", nift::rt::host::posix::terminal_release(&a));
    check("signal-persistent-next-acquire", nift::rt::host::posix::terminal_acquire(&b));
    check("signal-persistent-next-release", nift::rt::host::posix::terminal_release(&b));
    check("signal-persistent-disposition", default_ttou());
}

void tty_restore_recovery(ArmFn arm) {
    auto group = spawn("/bin/sleep", {"5"});
    check("tty-restore-group", group != nullptr);
    if (!group) return;
    check("tty-restore-give", group->give_terminal());
    arm(2);
    check("tty-restore-fails", !group->reclaim_terminal());
    check("tty-restore-retry", group->reclaim_terminal());
    check("tty-restore-shell-owns", ::tcgetpgrp(STDIN_FILENO) == ::getpgrp());
    check("tty-restore-disposition", default_ttou());
    group->terminate(CancelReason::Cleanup);
}

void launch_handoff_failure(ArmFn arm) {
    char effect[] = "/tmp/cp21-launch-effect-XXXXXX";
    const int effect_fd = ::mkstemp(effect);
    if (effect_fd >= 0) ::close(effect_fd);
    arm(3);
    SpawnRequest request;
    SpawnStage stage;
    stage.program = "/bin/sh";
    stage.args = {"-c", "printf 'CHILD-EFFECT\\n'"};
    stage.stdout_path = effect;
    request.stages = {stage};
    request.mode = OutputMode::Inherit;
    request.terminal = TerminalPolicy::Auto;
    request.own_group = true;
    HostStatus status = HostStatus::Ok;
    HostError error;
    auto group = ProcessGroup::start_for_wait(std::move(request), status, error);
    ProcessResult result;
    if (group) result = group->wait();
    arm(0);
    std::string effect_text;
    if (effect_fd >= 0) {
        std::ifstream in(effect);
        std::getline(in, effect_text);
        ::unlink(effect);
    }
    check("launch-handoff-fails", !group || !result.launched || !result.error.empty() ||
                                        result.state == GroupState::LaunchFailed);
    check("launch-handoff-child-not-executed", effect_text.find("CHILD-EFFECT") == std::string::npos);
    check("launch-handoff-no-clean-success", !(group && result.launched && result.exit_code == 0 &&
                                               result.error.empty()));
    if (group) group->terminate(CancelReason::Cleanup);
    auto healthy = spawn("/bin/true", {}, TerminalPolicy::Auto);
    bool ok = healthy && healthy->wait().exit_code == 0;
    if (healthy) healthy->terminate(CancelReason::Cleanup);
    check("launch-handoff-recovers", ok);
    check("launch-handoff-disposition", default_ttou());
}

void destruction_after_failure(ArmFn arm) {
    auto group = spawn("/bin/sleep", {"5"});
    check("destruction-group", group != nullptr);
    if (!group) return;
    check("destruction-give", group->give_terminal());
    arm(4);
    group.reset();  // destructor cannot restore; coordinator keeps pending state
    int sentinel = 0;
    check("destruction-pending-blocks-acquire", !nift::rt::host::posix::terminal_acquire(&sentinel));
    arm(0);
    check("destruction-recovers", nift::rt::host::posix::terminal_acquire(&sentinel));
    check("destruction-release", nift::rt::host::posix::terminal_release(&sentinel));
    check("destruction-disposition", default_ttou());
}

} // namespace

int main() {
    auto arm = reinterpret_cast<ArmFn>(::dlsym(RTLD_DEFAULT, "cp21_arm"));
    if (!arm) {
        std::printf("FAIL cp21_arm not loaded (run with LD_PRELOAD)\n");
        return 1;
    }
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

    one_shot_signal_restore(arm);
    persistent_signal_restore(arm);
    tty_restore_recovery(arm);
    launch_handoff_failure(arm);
    destruction_after_failure(arm);

    ::sigaction(SIGTTOU, &original, nullptr);
    std::printf("%s\n", failures ? "FAIL CP21 terminal recovery" : "PASS CP21 terminal recovery");
    return failures ? 1 : 0;
}
