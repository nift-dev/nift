// Linux F6: real group/zombie lifecycle, with linker-only syscall observation.
// No test switches are compiled into production. Run with f6_cleanup_controls.py.
#include "rt/host/process.h"
#include "rt/host/process_runtime.h"
#include "ProcessPOSIX.h"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unistd.h>

using namespace nift::rt::host;
namespace {
thread_local bool observing = false;
thread_local int proc_opens = 0;
thread_local int forced_errno = 0;
thread_local long observed_pgid = -1;
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
bool gone(long pid) { runtime::ProcInfo p; return !runtime::proc_read(pid, p); }
template<class F> bool until(F fn) {
    for (int i = 0; i < 600; ++i) {
        if (fn()) return true;
        std::this_thread::sleep_for(Millis(5));
    }
    return fn();
}
auto launch(const char* command) {
    SpawnRequest r;
    SpawnStage stage;
    stage.program = "/bin/sh";
    stage.args = {"-c", command};
    stage.env.push_back({"NIFT_TEST_PROC_TAG", "cp16-f6-cleanup"});
    r.stages.push_back(std::move(stage));
    r.mode = OutputMode::Inherit;
    r.stdin_mode = StdinMode::Null;
    r.terminal = TerminalPolicy::Never;
    HostStatus status; HostError error;
    auto g = ProcessGroup::start_for_wait(std::move(r), status, error);
    require(g && status == HostStatus::Ok, "launch failed");
    require(g->wait().exit_code == 0, "stage failed");
    return g;
}
void completed() {
    const auto before = runtime::managed_pgid_count();
    auto g = launch("exit 0");
    const long pgid = g->pgid();
    require(pgid > 1 && ::kill(-pgid, 0) == -1 && errno == ESRCH, "completed group not absent");
    observed_pgid = pgid; proc_opens = 0; observing = true;
    g.reset();
    observing = false;
    require(proc_opens == 0, "completed cleanup enumerated proc");
    require(runtime::managed_pgid_count() == before, "completed group attribution leaked");
}
void descendant(bool zombie, int error) {
    require(runtime::enable_subreaper(), "subreaper unavailable");
    auto g = launch(zombie ? "/bin/sleep 0.25 &" : "/bin/sleep 20 &");
    const long pgid = g->pgid();
    require(gone(g->pid()), "leader not reaped");
    long orphan = -1;
    for (const auto& p : runtime::proc_scan())
        if (p.pgrp == pgid && p.ppid == ::getpid()) orphan = p.pid;
    require(orphan > 0, "leaderless adopted member missing");
    {
        // Keep the runtime orphan sweep from reaping the exact zombie under test.
        std::unique_lock<std::mutex> hold(nift_process_launch_mutex());
        if (zombie) require(until([&] {runtime::ProcInfo p; return runtime::proc_read(orphan,p) && p.state=='Z';}), "no zombie");
        require(::kill(-pgid, 0) == 0, "existing group including zombie reported absent");
        const auto before = runtime::managed_pgid_count();
        observed_pgid = pgid; forced_errno = error; proc_opens = 0; observing = true;
        g.reset();
        observing = false; forced_errno = 0;
        require(proc_opens > 0, "existing/uncertain group bypassed zombie-aware scan");
        require(runtime::managed_pgid_count() == before, "required descendant attribution forgotten");
        if (!zombie) require(::kill(static_cast<pid_t>(orphan), SIGKILL) == 0, "cannot stop test descendant");
    }
    require(until([&] { runtime::reap_adopted_orphans(); return gone(orphan); }), "adopted descendant not reaped");
    runtime::reap_adopted_orphans();
}
}
extern "C" int __real_kill(pid_t, int);
extern "C" DIR* __real_opendir(const char*);
extern "C" int __wrap_kill(pid_t pid, int sig) {
    if (observing && forced_errno && pid == -observed_pgid && sig == 0) {
        errno = forced_errno; return -1;
    }
    return __real_kill(pid, sig);
}
extern "C" DIR* __wrap_opendir(const char* path) {
    if (observing && std::strcmp(path, "/proc") == 0) ++proc_opens;
    return __real_opendir(path);
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "one case required");
        if (!std::strcmp(argv[1], "completed")) completed();
        else if (!std::strcmp(argv[1], "zombie")) descendant(true,0);
        else if (!std::strcmp(argv[1], "leaderless")) descendant(false,0);
        else if (!std::strcmp(argv[1], "eperm")) descendant(false,EPERM);
        else if (!std::strcmp(argv[1], "eintr")) descendant(false,EINTR);
        else if (!std::strcmp(argv[1], "einval")) descendant(false,EINVAL);
        else throw std::runtime_error("unknown case");
        std::printf("PASS F6 %s\n",argv[1]); return 0;
    } catch (const std::exception& e) {
        observing = false;
        std::fprintf(stderr,"FAIL F6 %s: %s\n",argc>1?argv[1]:"?",e.what()); return 1;
    }
}
