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
using ParentArmFn = void (*)(int);
using ChildArmFn = void (*)(int);

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

void launch_parent_only_failure(ParentArmFn parent_arm) {
    char effect[] = "/tmp/cp21-parent-effect-XXXXXX";
    const int effect_fd = ::mkstemp(effect);
    if (effect_fd >= 0) ::close(effect_fd);
    parent_arm(2);
    SpawnRequest request;
    SpawnStage stage;
    stage.program = "/bin/sh";
    stage.args = {"-c", "printf 'PARENT-FAIL-CHILD-EFFECT\\n'"};
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
    parent_arm(0);
    std::string effect_text;
    if (effect_fd >= 0) {
        std::ifstream in(effect);
        std::getline(in, effect_text);
        ::unlink(effect);
    }
    check("parent-only-child-not-executed", effect_text.find("PARENT-FAIL-CHILD-EFFECT") == std::string::npos);
    check("parent-only-launch-fails", !group || !result.launched || !result.error.empty() ||
                                          result.state == GroupState::LaunchFailed);
    if (group) group->terminate(CancelReason::Cleanup);
    auto healthy = spawn("/bin/true", {}, TerminalPolicy::Auto);
    const bool ok = healthy && healthy->wait().exit_code == 0;
    if (healthy) healthy->terminate(CancelReason::Cleanup);
    check("parent-only-recovers", ok);
    check("parent-only-disposition", default_ttou());
}

void launch_child_only_failure(ChildArmFn child_arm) {
    char effect[] = "/tmp/cp21-child-effect-XXXXXX";
    const int effect_fd = ::mkstemp(effect);
    if (effect_fd >= 0) ::close(effect_fd);
    child_arm(1);
    SpawnRequest request;
    SpawnStage stage;
    stage.program = "/bin/sh";
    stage.args = {"-c", "printf 'CHILD-ONLY-EFFECT\\n'"};
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
    child_arm(0);
    std::string effect_text;
    if (effect_fd >= 0) {
        std::ifstream in(effect);
        std::getline(in, effect_text);
        ::unlink(effect);
    }
    check("child-only-child-not-executed", effect_text.find("CHILD-ONLY-EFFECT") == std::string::npos);
    check("child-only-launch-fails", !group || !result.launched || !result.error.empty() ||
                                         result.state == GroupState::LaunchFailed);
    if (group) group->terminate(CancelReason::Cleanup);
    auto healthy = spawn("/bin/true", {}, TerminalPolicy::Auto);
    const bool ok = healthy && healthy->wait().exit_code == 0;
    if (healthy) healthy->terminate(CancelReason::Cleanup);
    check("child-only-recovers", ok);
    check("child-only-disposition", default_ttou());
}

void tty_closed_stdin() {
    const int saved = ::dup(STDIN_FILENO);
    auto group = spawn("/bin/sleep", {"5"});
    check("closed-stdin-group", group != nullptr && saved >= 0);
    if (!group || saved < 0) return;
    check("closed-stdin-give", group->give_terminal());
    ::close(STDIN_FILENO);
    check("closed-stdin-reclaim", group->reclaim_terminal());
    check("closed-stdin-actual-shell-owns", ::tcgetpgrp(saved) == ::getpgrp());
    ::dup2(saved, STDIN_FILENO);
    ::close(saved);
    check("closed-stdin-disposition", default_ttou());
    group->terminate(CancelReason::Cleanup);
}

void tty_replaced_stdin() {
    const int saved = ::dup(STDIN_FILENO);
    auto group = spawn("/bin/sleep", {"5"});
    check("replaced-stdin-group", group != nullptr && saved >= 0);
    if (!group || saved < 0) return;
    check("replaced-stdin-give", group->give_terminal());
    const int nullfd = ::open("/dev/null", O_RDONLY);
    if (nullfd >= 0) { ::dup2(nullfd, STDIN_FILENO); ::close(nullfd); }
    check("replaced-stdin-reclaim", group->reclaim_terminal());
    check("replaced-stdin-actual-shell-owns", ::tcgetpgrp(saved) == ::getpgrp());
    ::dup2(saved, STDIN_FILENO);
    ::close(saved);
    check("replaced-stdin-disposition", default_ttou());
    group->terminate(CancelReason::Cleanup);
}

void tty_persistent_restore(ArmFn arm) {
    auto group = spawn("/bin/sleep", {"5"});
    check("tty-persistent-group", group != nullptr);
    if (!group) return;
    check("tty-persistent-give", group->give_terminal());
    arm(5);
    check("tty-persistent-release-fails", !group->reclaim_terminal());
    check("tty-persistent-retry-fails", !group->reclaim_terminal());
    int sentinel = 0;
    check("tty-persistent-blocks-acquire", !nift::rt::host::posix::terminal_acquire(&sentinel));
    arm(0);
    check("tty-persistent-recovers", group->reclaim_terminal());
    check("tty-persistent-shell-owns", ::tcgetpgrp(STDIN_FILENO) == ::getpgrp());
    check("tty-persistent-disposition", default_ttou());
    group->terminate(CancelReason::Cleanup);
}

void destruction_after_tty_failure(ArmFn arm) {
    auto group = spawn("/bin/sleep", {"5"});
    check("tty-destruction-group", group != nullptr);
    if (!group) return;
    check("tty-destruction-give", group->give_terminal());
    arm(5);
    group.reset();
    int sentinel = 0;
    check("tty-destruction-pending-blocks-acquire", !nift::rt::host::posix::terminal_acquire(&sentinel));
    arm(0);
    check("tty-destruction-recovers", nift::rt::host::posix::terminal_acquire(&sentinel));
    check("tty-destruction-release", nift::rt::host::posix::terminal_release(&sentinel));
    check("tty-destruction-disposition", default_ttou());
}

int count_fds() {
    int count = 0;
    for (int fd = 0; fd < 1024; ++fd) if (::fcntl(fd, F_GETFD) != -1) ++count;
    return count;
}

void lease_fd_no_growth() {
    const int before = count_fds();
    for (int i = 0; i < 64; ++i) {
        int owner = 0;
        if (!nift::rt::host::posix::terminal_acquire(&owner) ||
            !nift::rt::host::posix::terminal_release(&owner)) {
            check("lease-fd-cycles", false);
            return;
        }
    }
    check("lease-fd-no-growth", count_fds() == before);
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

    auto parent_arm = reinterpret_cast<ParentArmFn>(::dlsym(RTLD_DEFAULT, "cp21_parent_arm"));
    auto child_arm = reinterpret_cast<ChildArmFn>(::dlsym(RTLD_DEFAULT, "cp21_child_arm"));
    if (!parent_arm || !child_arm) {
        std::printf("FAIL launch fault arms not loaded\n");
        return 1;
    }

    one_shot_signal_restore(arm);
    persistent_signal_restore(arm);
    tty_restore_recovery(arm);
    launch_handoff_failure(arm);
    destruction_after_failure(arm);
    launch_parent_only_failure(parent_arm);
    launch_child_only_failure(child_arm);
    tty_closed_stdin();
    tty_replaced_stdin();
    tty_persistent_restore(arm);
    destruction_after_tty_failure(arm);
    lease_fd_no_growth();

    ::sigaction(SIGTTOU, &original, nullptr);
    std::printf("%s\n", failures ? "FAIL CP21 terminal recovery" : "PASS CP21 terminal recovery");
    return failures ? 1 : 0;
}
