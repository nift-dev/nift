// CP16 Host layer: C2 signal cleanup, subreaper and orphan sweep.
#include "rt/host/process_signals.h"

#include "rt/host/process_runtime.h"
#ifndef _WIN32
#include "ProcessPOSIX.h"  // launch mutex, child reporting
#endif

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <poll.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/prctl.h>
#include <linux/futex.h>
#endif

namespace nift::rt::host::runtime {

#ifdef __linux__
namespace {
void sleep_until_or(Clock::time_point until, const std::function<bool()>& done) {
    while (Clock::now() < until && !done()) std::this_thread::sleep_for(Millis(10));
}
} // namespace
#endif

#ifndef _WIN32
// ---------------------------------------------------------------- C2 signal cleanup
namespace {
// Wakes the watcher thread from the signal handler. Linux: a futex word (no
// descriptor: the legacy CP07 guarantee "capture works under ulimit -n 6"
// leaves no room for one). Elsewhere: a self-pipe.
#ifdef __linux__
std::atomic<int> g_wake_word{0};  // 0 nothing, > 0 signal number, -1 stop
void wake_post(int value) {  // async-signal-safe
    g_wake_word.store(value);
    ::syscall(SYS_futex, reinterpret_cast<int*>(&g_wake_word), FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
}
int wake_wait() {
    for (;;) {
        const int value = g_wake_word.exchange(0);
        if (value != 0) return value;
        ::syscall(SYS_futex, reinterpret_cast<int*>(&g_wake_word), FUTEX_WAIT_PRIVATE, 0, nullptr, nullptr, 0);
    }
}
bool wake_open() {
    // A previous watcher (uninstalled) consumes its stop value first (bounded).
    for (int i = 0; i < 1000 && g_wake_word.load() == -1; ++i) ::usleep(1000);
    return true;
}
void wake_close() {}
#else
int g_signal_pipe[2] = {-1, -1};
void wake_post(int value) {
    const unsigned char byte = value < 0 ? 0 : static_cast<unsigned char>(value);
    if (g_signal_pipe[1] >= 0) { ssize_t ignored = ::write(g_signal_pipe[1], &byte, 1); (void)ignored; }
}
int wake_wait() {
    const int fd = g_signal_pipe[0];
    for (;;) {
        unsigned char byte = 0;
        const ssize_t n = ::read(fd, &byte, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || byte == 0) { ::close(fd); return -1; }
        return byte;
    }
}
bool wake_open() {
    int fds[2];
    if (::pipe(fds) != 0) return false;
    for (int fd : fds) ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFL, ::fcntl(fds[1], F_GETFL) | O_NONBLOCK);
    g_signal_pipe[0] = fds[0];
    g_signal_pipe[1] = fds[1];
    return true;
}
void wake_close() {
    ::close(g_signal_pipe[1]);
    g_signal_pipe[1] = -1;
    g_signal_pipe[0] = -1;  // closed by the watcher
}
#endif
std::atomic<bool> g_terminating{false};
std::atomic<int> g_signal{0};
std::atomic<bool> g_installed{false};
std::mutex g_done_mutex;
std::condition_variable g_done_cv;
bool g_cleanup_done = false;
struct sigaction g_old_int, g_old_term;
SignalCleanupOptions& signal_options() { static auto* o = new SignalCleanupOptions; return *o; }

extern "C" void nift_host_signal_handler(int signo) {
    const int saved = errno;
    g_terminating.store(true);
    g_signal.store(signo);
    wake_post(signo);
    errno = saved;
}

void watcher_loop() {
    for (;;) {
        const int signo = wake_wait();
        if (signo < 0) return;  // uninstalled
        const auto options = signal_options();
        terminate_all_indexed(options.kill_after, options.post_kill_wait);
        // Adopted orphans (legacy run() background members, under the
        // subreaper): terminated too; other components' children are left alone.
        if (subreaper_enabled() && child_reporting())
            sweep_orphans(options.kill_after, options.post_kill_wait, [](long pid) { return is_direct_child(pid); });
        {
            std::lock_guard<std::mutex> lock(g_done_mutex);
            g_cleanup_done = true;
        }
        g_done_cv.notify_all();
        if (options.on_done) { options.on_done(signo); continue; }
        struct sigaction dfl{};
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        ::sigaction(signo, &dfl, nullptr);
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, signo);
        ::pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
        ::kill(::getpid(), signo);  // default action: Nift ends with a signal-derived status
        for (;;) ::pause();
    }
}
} // namespace

// atexit: the program reached its end while the watcher was still cleaning
// up after SIGINT/SIGTERM. Wait for it (bounded) and end with the signal.
extern "C" void nift_host_exit_handler() {
    if (!g_terminating.load() || !g_installed.load()) return;
    const auto options = signal_options();
    if (options.on_done) return;
    std::unique_lock<std::mutex> lock(g_done_mutex);
    g_done_cv.wait_for(lock, options.kill_after + options.post_kill_wait + Millis(1000), [] { return g_cleanup_done; });
    lock.unlock();
    const int signo = g_signal.load() ? g_signal.load() : SIGTERM;
    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    ::sigaction(signo, &dfl, nullptr);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, signo);
    ::pthread_sigmask(SIG_UNBLOCK, &set, nullptr);
    ::raise(signo);
}

bool termination_requested() { return g_terminating.load(); }
bool shutting_down() { return g_terminating.load(); }
void request_shutdown_for_test(int signo) { g_signal = signo; g_terminating = true; }
void clear_shutdown_for_test() { g_terminating = false; g_signal = 0; }

bool install_signal_cleanup(SignalCleanupOptions options) {
    if (g_installed.exchange(true)) { signal_options() = std::move(options); return true; }
    signal_options() = std::move(options);
    if (!wake_open()) { g_installed = false; return false; }
    std::thread(watcher_loop).detach();
    static bool registered = false;
    if (!registered) { registered = true; std::atexit(nift_host_exit_handler); }
    struct sigaction sa{};
    sa.sa_handler = nift_host_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    ::sigaction(SIGINT, &sa, &g_old_int);
    ::sigaction(SIGTERM, &sa, &g_old_term);
    return true;
}

void uninstall_signal_cleanup() {
    if (!g_installed.exchange(false)) return;
    ::sigaction(SIGINT, &g_old_int, nullptr);
    ::sigaction(SIGTERM, &g_old_term, nullptr);
    wake_post(-1);
    wake_close();
}

// ---------------------------------------------------------------- subreaper / orphans
namespace { std::atomic<bool> g_subreaper{false}; }

#ifdef __linux__
namespace {
struct sigaction g_old_chld;
extern "C" void nift_host_sigchld(int signo, siginfo_t* info, void* context) {
    const int saved = errno;
    loop_notify_sigchld();
    // Chain whatever was installed before (never change other code's SIGCHLD semantics).
    if (g_old_chld.sa_flags & SA_SIGINFO) {
        if (g_old_chld.sa_sigaction) g_old_chld.sa_sigaction(signo, info, context);
    } else if (g_old_chld.sa_handler != SIG_DFL && g_old_chld.sa_handler != SIG_IGN && g_old_chld.sa_handler)
        g_old_chld.sa_handler(signo);
    errno = saved;
}
} // namespace
#endif

bool enable_subreaper() {
#ifdef __linux__
    if (g_subreaper) return true;
    if (::prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0) return false;
    g_subreaper = true;
    // Adopted orphans become our children: reap them when they exit (no
    // zombies while Nift lives). SIGCHLD only wakes the host loop.
    loop_at(Clock::now(), [] {});  // make sure the loop (and its wake pipe) exists
    struct sigaction current {};
    ::sigaction(SIGCHLD, nullptr, &current);
    if (!(current.sa_flags & SA_SIGINFO) && current.sa_handler == SIG_IGN) return true;  // children auto-reaped already
    struct sigaction sa {};
    sa.sa_sigaction = nift_host_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESTART | SA_NOCLDSTOP;
    ::sigaction(SIGCHLD, &sa, &g_old_chld);
    return true;
#else
    return false;
#endif
}
bool subreaper_enabled() { return g_subreaper; }

namespace {
void report_direct_child(long pid) { note_direct_child(pid); }
} // namespace

bool enable_subreaper_for_legacy() {
#ifdef __linux__
    if (g_subreaper) return true;
    std::lock_guard<std::mutex> launching(nift_process_launch_mutex());
    nift_process_child_observer().store(&report_direct_child);
    set_child_reporting(true);
    const long self = static_cast<long>(::getpid());
    std::set<long> stages;
    for (const auto& g : index_snapshot()) for (long p : g.stage_pids) stages.insert(p);
    for (long pid : own_children())
        if (!stages.count(pid)) note_direct_child(pid);
    (void)self;
    // No SIGCHLD handler and no host loop here: legacy code is not prepared
    // for EINTR from a new handler, and a plain run() must not cost
    // descriptors (CP07, `ulimit -n 6`). Adopted orphans are reaped after each
    // legacy run() and at script end (reap_adopted_children), and C2 sweeps them.
    if (::prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0) return false;
    g_subreaper = true;
    return true;
#else
    return false;
#endif
}

std::size_t sweep_orphans(Millis kill_after, Millis post_kill_wait, const std::function<bool(long)>& foreign) {
#ifdef __linux__
    const long self = static_cast<long>(::getpid());
    std::set<long> managed;
    std::set<long> managed_groups;
    for (const auto& g : index_snapshot()) {
        for (long p : g.stage_pids) managed.insert(p);
        if (g.pgid > 0) managed_groups.insert(g.pgid);
    }
    const auto detached = detached_sessions_snapshot();
    const std::set<long> sessions(detached.begin(), detached.end());
    std::vector<long> orphans;
    std::vector<long> zombies;
    for (const auto& p : proc_scan()) {
        if (p.ppid != self || managed.count(p.pid) || managed_groups.count(p.pgrp) || sessions.count(p.session))
            continue;
        if (foreign && foreign(p.pid)) continue;
        if (p.state == 'Z') zombies.push_back(p.pid);
        else orphans.push_back(p.pid);
    }
    for (long pid : zombies) { int st; ::waitpid(static_cast<pid_t>(pid), &st, WNOHANG); }
    if (orphans.empty()) return 0;
    // The orphans' own descendants too (they would be adopted next).
    for (long pid : proc_descendants(orphans)) host_kill(pid, SIGTERM, "sweep-term-tree", nullptr);
    for (long pid : orphans) host_kill(pid, SIGTERM, "sweep-term", nullptr);
    std::set<long> left(orphans.begin(), orphans.end());
    auto reap = [&] {
        for (auto it = left.begin(); it != left.end();) {
            int st = 0;
            const pid_t got = ::waitpid(static_cast<pid_t>(*it), &st, WNOHANG);
            if (got == static_cast<pid_t>(*it) || (got < 0 && errno == ECHILD)) it = left.erase(it);
            else ++it;
        }
        return left.empty();
    };
    sleep_until_or(Clock::now() + kill_after, reap);
    for (long pid : proc_descendants(std::vector<long>(left.begin(), left.end())))
        host_kill(pid, SIGKILL, "sweep-kill-tree", nullptr);
    for (long pid : left) host_kill(pid, SIGKILL, "sweep-kill", nullptr);
    sleep_until_or(Clock::now() + post_kill_wait, reap);
    for (long pid : left) reaper_add_pid(pid);
    return orphans.size();
#else
    (void)kill_after; (void)post_kill_wait; (void)foreign;
    return 0;
#endif
}
#else  // _WIN32: the C2 equivalent (console control handler) and job objects live in process_win.cpp.
bool termination_requested() { return false; }
bool shutting_down() { return false; }
void request_shutdown_for_test(int) {}
void clear_shutdown_for_test() {}
bool install_signal_cleanup(SignalCleanupOptions) { return false; }
void uninstall_signal_cleanup() {}
bool enable_subreaper() { return false; }
bool enable_subreaper_for_legacy() { return false; }
bool subreaper_enabled() { return false; }
std::size_t sweep_orphans(Millis, Millis, const std::function<bool(long)>&) { return 0; }
#endif

} // namespace nift::rt::host::runtime
