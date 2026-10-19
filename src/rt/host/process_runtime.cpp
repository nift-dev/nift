#include "rt/host/process_runtime.h"

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
#include "ProcessPOSIX.h"  // nift_process_launch_mutex

#include <dirent.h>
#include <poll.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __linux__
#include <sys/prctl.h>
#endif

namespace nift::rt::host::runtime {

// ---------------------------------------------------------------- warnings
namespace {
std::mutex& warn_mutex() { static std::mutex m; return m; }
WarningSink& warn_sink() { static WarningSink s; return s; }
} // namespace

void set_warning_sink(WarningSink sink) {
    std::lock_guard<std::mutex> lock(warn_mutex());
    warn_sink() = std::move(sink);
}

void warn(const std::string& line) {
    WarningSink sink;
    {
        std::lock_guard<std::mutex> lock(warn_mutex());
        sink = warn_sink();
    }
    if (sink) { sink(line); return; }
    std::fflush(stdout);
    std::fprintf(stderr, "%s\n", line.c_str());
    std::fflush(stderr);
}

// ---------------------------------------------------------------- /proc
#ifdef __linux__
bool proc_read(long pid, ProcInfo& out) {
    char path[64];
    std::snprintf(path, sizeof path, "/proc/%ld/stat", pid);
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buf[1024];
    ssize_t n;
    do { n = ::read(fd, buf, sizeof buf - 1); } while (n < 0 && errno == EINTR);
    ::close(fd);
    if (n <= 0) return false;
    buf[n] = 0;
    const char* close_paren = std::strrchr(buf, ')');
    if (!close_paren) return false;
    char state = '?';
    long ppid = 0, pgrp = 0, session = 0;
    if (std::sscanf(close_paren + 1, " %c %ld %ld %ld", &state, &ppid, &pgrp, &session) != 4) return false;
    out.pid = pid; out.state = state; out.ppid = ppid; out.pgrp = pgrp; out.session = session;
    return true;
}

std::vector<ProcInfo> proc_scan() {
    std::vector<ProcInfo> out;
    DIR* dir = ::opendir("/proc");
    if (!dir) return out;
    while (dirent* entry = ::readdir(dir)) {
        const char* name = entry->d_name;
        if (*name < '1' || *name > '9') continue;
        char* end = nullptr;
        const long pid = std::strtol(name, &end, 10);
        if (!end || *end) continue;
        ProcInfo info;
        if (proc_read(pid, info)) out.push_back(info);
    }
    ::closedir(dir);
    return out;
}
#else
bool proc_read(long, ProcInfo&) { return false; }
std::vector<ProcInfo> proc_scan() { return {}; }
#endif

std::vector<long> proc_descendants(const std::vector<long>& roots) {
    std::vector<long> result;
    if (roots.empty()) return result;
    const auto all = proc_scan();
    std::multimap<long, long> children;
    for (const auto& p : all) children.emplace(p.ppid, p.pid);
    std::vector<long> frontier(roots.begin(), roots.end());
    std::set<long> seen(roots.begin(), roots.end());
    while (!frontier.empty()) {
        const long parent = frontier.back();
        frontier.pop_back();
        auto range = children.equal_range(parent);
        for (auto it = range.first; it != range.second; ++it) {
            if (seen.insert(it->second).second) {
                result.push_back(it->second);
                frontier.push_back(it->second);
            }
        }
    }
    return result;
}

std::vector<long> proc_group_members(long pgid, long session) {
    std::vector<long> out;
    for (const auto& p : proc_scan())
        if (p.pgrp == pgid && p.state != 'Z' && p.state != 'X' && (session < 0 || p.session == session)) out.push_back(p.pid);
    return out;
}

// ---------------------------------------------------------------- failures
namespace {
WarningSink& failure_sink() { static WarningSink s; return s; }
} // namespace

void set_failure_sink(WarningSink sink) {
    std::lock_guard<std::mutex> lock(warn_mutex());
    failure_sink() = std::move(sink);
}

void report_cleanup_failure(const std::string& line) {
    WarningSink sink;
    {
        std::lock_guard<std::mutex> lock(warn_mutex());
        sink = failure_sink();
    }
    if (sink) { sink(line); return; }
    std::fflush(stdout);
    std::fprintf(stderr, "%s\n", line.c_str());
    std::fflush(stderr);
}

#ifdef NIFT_HOST_TEST_HOOKS
TestHooks& test_hooks() { static TestHooks h; return h; }
#endif

void test_event(const void* group, const char* what, long a, long b, long c, const char* detail) {
#ifdef NIFT_HOST_TEST_HOOKS
    auto& h = test_hooks();
    if (!h.record.load()) return;
    std::lock_guard<std::mutex> lock(h.log_mutex);
    h.log.push_back(EventRecord{Clock::now(), group, what, a, b, c, detail});
#else
    (void)group; (void)what; (void)a; (void)b; (void)c; (void)detail;
#endif
}

int host_kill(long target, int sig, const char* site, const void* group) {
    if (sig != 0) test_event(group, "signal", target, sig, 0, site);  // a = target (<0: group), b = signal
#ifndef _WIN32
    return ::kill(static_cast<pid_t>(target), sig);
#else
    (void)target; (void)sig;
    return -1;
#endif
}

// ---------------------------------------------------------------- host loop
#ifndef _WIN32
namespace {
std::atomic<int> g_loop_wake_fd{-1};
std::atomic<bool> g_sigchld{false};
struct LoopState {
    std::mutex mutex;
    std::multimap<Clock::time_point, TaskId> timers;
    struct Task { std::function<void()> fn; int fd = -1; Clock::time_point when; bool timer = false; };
    std::map<TaskId, Task> tasks;
    TaskId next = 1;
    int wake[2] = {-1, -1};
    bool started = false;
};
LoopState& loop() { static LoopState* s = new LoopState; return *s; }  // never destroyed: the thread outlives statics

void loop_wake(LoopState& l) {
    if (l.wake[1] < 0) return;
    const char byte = 1;
    ssize_t ignored = ::write(l.wake[1], &byte, 1);  // non-blocking; a full pipe already means "wake up"
    (void)ignored;
}

void loop_thread() {
    auto& l = loop();
    for (;;) {
        std::vector<pollfd> fds;
        std::vector<TaskId> fd_tasks;
        int timeout_ms = -1;
        {
            std::lock_guard<std::mutex> lock(l.mutex);
            fds.push_back({l.wake[0], POLLIN, 0});
            for (const auto& kv : l.tasks)
                if (!kv.second.timer) { fds.push_back({kv.second.fd, POLLIN, 0}); fd_tasks.push_back(kv.first); }
            if (!l.timers.empty()) {
                const auto left = std::chrono::duration_cast<Millis>(l.timers.begin()->first - Clock::now()).count();
                timeout_ms = left <= 0 ? 0 : static_cast<int>(std::min<long long>(left + 1, 1 << 30));
            }
        }
        const int ready = ::poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout_ms);
        std::vector<std::function<void()>> due;
        {
            std::lock_guard<std::mutex> lock(l.mutex);
            if (ready > 0 && (fds[0].revents & POLLIN)) {
                char buf[64];
                while (::read(l.wake[0], buf, sizeof buf) > 0) {}
            }
            if (ready > 0) {
                for (std::size_t i = 1; i < fds.size(); ++i) {
                    if (!fds[i].revents) continue;
                    auto it = l.tasks.find(fd_tasks[i - 1]);
                    if (it == l.tasks.end()) continue;  // cancelled meanwhile
                    due.push_back(std::move(it->second.fn));
                    ::close(it->second.fd);
                    l.tasks.erase(it);
                }
            }
            const auto now = Clock::now();
            while (!l.timers.empty() && l.timers.begin()->first <= now) {
                const TaskId id = l.timers.begin()->second;
                l.timers.erase(l.timers.begin());
                auto it = l.tasks.find(id);
                if (it == l.tasks.end()) continue;
                due.push_back(std::move(it->second.fn));
                l.tasks.erase(it);
            }
        }
        for (auto& fn : due) if (fn) fn();
        if (g_sigchld.exchange(false)) reap_adopted_orphans();
    }
}

void loop_start_locked(LoopState& l) {
    if (l.started) return;
    int fds[2];
    if (::pipe(fds) != 0) return;
    for (int fd : fds) {
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
        ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    }
    l.wake[0] = fds[0];
    l.wake[1] = fds[1];
    g_loop_wake_fd = fds[1];
    l.started = true;
    std::thread(loop_thread).detach();
}
} // namespace

TaskId loop_at(Clock::time_point when, std::function<void()> fn) {
    auto& l = loop();
    std::lock_guard<std::mutex> lock(l.mutex);
    loop_start_locked(l);
    const TaskId id = l.next++;
    LoopState::Task task;
    task.fn = std::move(fn);
    task.when = when;
    task.timer = true;
    l.tasks.emplace(id, std::move(task));
    const bool earliest = l.timers.empty() || when < l.timers.begin()->first;
    l.timers.emplace(when, id);
    if (earliest) loop_wake(l);
    return id;
}

TaskId loop_when_readable(int fd, std::function<void()> fn) {
    auto& l = loop();
    std::lock_guard<std::mutex> lock(l.mutex);
    loop_start_locked(l);
    const TaskId id = l.next++;
    LoopState::Task task;
    task.fn = std::move(fn);
    task.fd = fd;
    l.tasks.emplace(id, std::move(task));
    loop_wake(l);
    return id;
}

void loop_cancel(TaskId id) {
    if (!id) return;
    auto& l = loop();
    std::lock_guard<std::mutex> lock(l.mutex);
    auto it = l.tasks.find(id);
    if (it == l.tasks.end()) return;
    if (it->second.timer) {
        auto range = l.timers.equal_range(it->second.when);
        for (auto t = range.first; t != range.second; ++t) if (t->second == id) { l.timers.erase(t); break; }
    } else {
        ::close(it->second.fd);
        loop_wake(l);  // drop the fd from the current poll set
    }
    l.tasks.erase(it);
}

std::size_t loop_pending() {
    auto& l = loop();
    std::lock_guard<std::mutex> lock(l.mutex);
    return l.tasks.size();
}

std::size_t loop_fd_count() {
    auto& l = loop();
    std::lock_guard<std::mutex> lock(l.mutex);
    std::size_t n = l.started ? 2 : 0;
    for (const auto& kv : l.tasks) if (!kv.second.timer) ++n;
    return n;
}
#else
TaskId loop_at(Clock::time_point, std::function<void()>) { return 0; }
TaskId loop_when_readable(int, std::function<void()>) { return 0; }
void loop_cancel(TaskId) {}
std::size_t loop_pending() { return 0; }
std::size_t loop_fd_count() { return 0; }
#endif

// ---------------------------------------------------------------- reaper
namespace {
struct ReaperState {
    std::mutex mutex;
    std::condition_variable idle;
    std::set<long> pids;
    std::vector<ProcessGroupPtr> groups;
};
ReaperState& reaper() { static ReaperState* s = new ReaperState; return *s; }

void reaper_done_pid(long pid) {
    auto& r = reaper();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.pids.erase(pid);
    if (r.pids.empty() && r.groups.empty()) r.idle.notify_all();
}

#ifndef _WIN32
int open_pidfd_for(long pid) {
#if defined(__linux__) && defined(SYS_pidfd_open)
    const long fd = ::syscall(SYS_pidfd_open, static_cast<pid_t>(pid), 0);
    return fd < 0 ? -1 : static_cast<int>(fd);
#else
    (void)pid;
    return -1;
#endif
}

// Reaps `pid` if it is our child and has exited; true when nothing is left to wait for.
bool try_reap(long pid) {
    int status = 0;
    pid_t got;
    do { got = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG); } while (got < 0 && errno == EINTR);
    if (got == static_cast<pid_t>(pid) || (got < 0 && errno == ECHILD)) return true;
    // Not our child (no subreaper) and still alive: nothing to reap, nothing to wait for.
    if (got == 0) return false;
    return true;
}

void watch_pid(long pid) {
    const int fd = open_pidfd_for(pid);
    if (fd >= 0) {
        loop_when_readable(fd, [pid] {
            if (try_reap(pid)) reaper_done_pid(pid);
            else watch_pid(pid);  // spurious: re-arm
        });
    } else {
        // No pidfd (old kernel, or the pid is already gone): one timer, not a busy loop.
        if (try_reap(pid)) { reaper_done_pid(pid); return; }
        loop_at(Clock::now() + Millis(100), [pid] { watch_pid(pid); });
    }
}
#endif
} // namespace

void reaper_add_pid(long pid) {
    if (pid <= 0) return;
    auto& r = reaper();
    {
        std::lock_guard<std::mutex> lock(r.mutex);
        if (!r.pids.insert(pid).second) return;
    }
#ifndef _WIN32
    watch_pid(pid);
#else
    reaper_done_pid(pid);
#endif
}

void reaper_adopt(ProcessGroupPtr group) {
    if (!group) return;
    {
        auto& r = reaper();
        std::lock_guard<std::mutex> lock(r.mutex);
        if (std::find(r.groups.begin(), r.groups.end(), group) != r.groups.end()) return;
        r.groups.push_back(group);
    }
    group->timer_tick();  // drives it now; its timers keep driving it until settled
}

bool reaper_release(const ProcessGroup* group) {
    ProcessGroupPtr keep;  // destroyed after the lock is released
    {
        auto& r = reaper();
        std::lock_guard<std::mutex> lock(r.mutex);
        for (auto it = r.groups.begin(); it != r.groups.end(); ++it)
            if (it->get() == group) { keep = std::move(*it); r.groups.erase(it); break; }
        if (r.pids.empty() && r.groups.empty()) r.idle.notify_all();
    }
    return keep != nullptr;
}

namespace {
std::mutex& settle_mutex() { static std::mutex m; return m; }
std::condition_variable& settle_cv() { static std::condition_variable c; return c; }
std::atomic<std::uint64_t> g_settle{0};
} // namespace

std::uint64_t settle_generation() { return g_settle.load(); }

void notify_settled() {
    {
        std::lock_guard<std::mutex> lock(settle_mutex());
        ++g_settle;
    }
    settle_cv().notify_all();
}

void wait_settle_change(std::uint64_t seen, Clock::time_point until) {
    std::unique_lock<std::mutex> lock(settle_mutex());
    settle_cv().wait_until(lock, until, [&] { return g_settle.load() != seen; });
}

std::size_t reaper_pending() {
    auto& r = reaper();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.pids.size() + r.groups.size();
}

bool reaper_wait_idle(Millis bound) {
    auto& r = reaper();
    std::unique_lock<std::mutex> lock(r.mutex);
    return r.idle.wait_for(lock, bound, [&] { return r.pids.empty() && r.groups.empty(); });
}

// ---------------------------------------------------------------- termination index
namespace {
std::mutex& index_mutex() { static std::mutex m; return m; }
std::map<const void*, IndexedGroup>& index_map() { static auto* m = new std::map<const void*, IndexedGroup>; return *m; }
std::mutex& detached_mutex() { static std::mutex m; return m; }
std::set<long>& detached_sessions() { static auto* s = new std::set<long>; return *s; }
} // namespace

void index_add(const void* key, IndexedGroup group) {
    std::lock_guard<std::mutex> lock(index_mutex());
    index_map()[key] = std::move(group);
}
void index_update_pids(const void* key, std::vector<long> pids) {
    std::lock_guard<std::mutex> lock(index_mutex());
    auto it = index_map().find(key);
    if (it != index_map().end()) it->second.stage_pids = std::move(pids);
}
void index_remove(const void* key) {
    std::lock_guard<std::mutex> lock(index_mutex());
    index_map().erase(key);
}
std::vector<IndexedGroup> index_snapshot() {
    std::lock_guard<std::mutex> lock(index_mutex());
    std::vector<IndexedGroup> out;
    for (const auto& kv : index_map()) out.push_back(kv.second);
    return out;
}
void note_detached_session(long sid) {
    std::lock_guard<std::mutex> lock(detached_mutex());
    detached_sessions().insert(sid);
}

namespace {
std::mutex& managed_mutex() { static std::mutex m; return m; }
std::set<long>& managed_pgids() { static auto* s = new std::set<long>; return *s; }
} // namespace

void note_managed_pgid(long pgid) {
    if (pgid <= 0) return;
    std::lock_guard<std::mutex> lock(managed_mutex());
    managed_pgids().insert(pgid);
}

std::size_t reap_adopted_orphans() {
#ifdef __linux__
    // A stage that exits between fork() and its registration in the index
    // must not be taken for an orphan: launches hold this mutex across both.
    std::lock_guard<std::mutex> launching(nift_process_launch_mutex());
    const long self = static_cast<long>(::getpid());
    const long own_session = static_cast<long>(::getsid(0));
    std::set<long> stages;
    for (const auto& g : index_snapshot()) for (long p : g.stage_pids) stages.insert(p);
    const auto all = proc_scan();
    std::size_t reaped = 0;
    std::lock_guard<std::mutex> lock(managed_mutex());
    auto& known = managed_pgids();
    for (const auto& p : all) {
        if (p.ppid != self || p.state != 'Z' || stages.count(p.pid)) continue;
        const bool ours = known.count(p.pgrp) || p.session != own_session;
        if (!ours) continue;  // another component's child (legacy run(), job control): theirs to wait for
        int status = 0;
        if (::waitpid(static_cast<pid_t>(p.pid), &status, WNOHANG) == static_cast<pid_t>(p.pid)) {
            ++reaped;
            test_event(nullptr, "orphan-reap", p.pid, p.pgrp, status);
        }
    }
    // Forget groups that no longer exist, so a reused pgid of other code is never claimed.
    std::set<long> present;
    for (const auto& p : all) present.insert(p.pgrp);
    for (auto it = known.begin(); it != known.end();) {
        if (!present.count(*it)) it = known.erase(it);
        else ++it;
    }
    return reaped;
#else
    return 0;
#endif
}

#ifndef _WIN32
namespace {
bool group_alive(const IndexedGroup& g) {
#ifdef __linux__
    return !proc_group_members(g.pgid, static_cast<long>(::getsid(0))).empty();
#else
    return ::kill(-static_cast<pid_t>(g.pgid), 0) == 0;
#endif
}
void sleep_until_or(Clock::time_point until, const std::function<bool()>& done) {
    while (Clock::now() < until && !done()) std::this_thread::sleep_for(Millis(10));
}
} // namespace

std::size_t terminate_all_indexed(Millis kill_after, Millis post_kill_wait) {
    std::vector<IndexedGroup> groups;
    for (auto& g : index_snapshot())
        if (!g.detached && g.pgid > 0 && group_alive(g)) groups.push_back(g);
    for (const auto& g : groups) host_kill(-g.pgid, SIGTERM, "c2-term", nullptr);
    auto any_alive = [&] {
        for (const auto& g : groups) if (group_alive(g)) return true;
        return false;
    };
    sleep_until_or(Clock::now() + kill_after, [&] { return !any_alive(); });
    for (const auto& g : groups)
        if (group_alive(g)) host_kill(-g.pgid, SIGKILL, "c2-kill", nullptr);
    sleep_until_or(Clock::now() + post_kill_wait, [&] { return !any_alive(); });
    return groups.size();
}

// ---------------------------------------------------------------- C2 signal cleanup
namespace {
int g_signal_pipe[2] = {-1, -1};
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
    const unsigned char byte = static_cast<unsigned char>(signo);
    if (g_signal_pipe[1] >= 0) { ssize_t ignored = ::write(g_signal_pipe[1], &byte, 1); (void)ignored; }
    errno = saved;
}

void watcher_loop(int read_fd) {
    for (;;) {
        unsigned char byte = 0;
        const ssize_t n = ::read(read_fd, &byte, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0 || byte == 0) { ::close(read_fd); return; }  // uninstalled
        const int signo = byte;
        const auto options = signal_options();
        terminate_all_indexed(options.kill_after, options.post_kill_wait);
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
    int fds[2];
    if (::pipe(fds) != 0) { g_installed = false; return false; }
    for (int fd : fds) ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    ::fcntl(fds[1], F_SETFL, ::fcntl(fds[1], F_GETFL) | O_NONBLOCK);
    g_signal_pipe[0] = fds[0];
    g_signal_pipe[1] = fds[1];
    std::thread(watcher_loop, fds[0]).detach();
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
    const unsigned char stop = 0;
    ssize_t ignored = ::write(g_signal_pipe[1], &stop, 1);
    (void)ignored;
    ::close(g_signal_pipe[1]);
    g_signal_pipe[1] = -1;
    g_signal_pipe[0] = -1;  // closed by the watcher
}

// ---------------------------------------------------------------- subreaper / orphans
namespace { std::atomic<bool> g_subreaper{false}; }

#ifdef __linux__
namespace {
struct sigaction g_old_chld;
extern "C" void nift_host_sigchld(int signo, siginfo_t* info, void* context) {
    const int saved = errno;
    g_sigchld.store(true);
    const int fd = g_loop_wake_fd.load();
    if (fd >= 0) { const char byte = 2; ssize_t ignored = ::write(fd, &byte, 1); (void)ignored; }
    // Chain whatever was installed before (never change other code's SIGCHLD semantics).
    if (g_old_chld.sa_flags & SA_SIGINFO) { if (g_old_chld.sa_sigaction) g_old_chld.sa_sigaction(signo, info, context); }
    else if (g_old_chld.sa_handler != SIG_DFL && g_old_chld.sa_handler != SIG_IGN && g_old_chld.sa_handler) g_old_chld.sa_handler(signo);
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

std::size_t sweep_orphans(Millis kill_after, Millis post_kill_wait, const std::function<bool(long)>& foreign) {
#ifdef __linux__
    const long self = static_cast<long>(::getpid());
    std::set<long> managed;
    std::set<long> managed_groups;
    for (const auto& g : index_snapshot()) {
        for (long p : g.stage_pids) managed.insert(p);
        if (g.pgid > 0) managed_groups.insert(g.pgid);
    }
    std::set<long> sessions;
    {
        std::lock_guard<std::mutex> lock(detached_mutex());
        sessions = detached_sessions();
    }
    std::vector<long> orphans;
    std::vector<long> zombies;
    for (const auto& p : proc_scan()) {
        if (p.ppid != self || managed.count(p.pid) || managed_groups.count(p.pgrp) || sessions.count(p.session)) continue;
        if (foreign && foreign(p.pid)) continue;
        if (p.state == 'Z') zombies.push_back(p.pid);
        else orphans.push_back(p.pid);
    }
    for (long pid : zombies) { int st; ::waitpid(static_cast<pid_t>(pid), &st, WNOHANG); }
    if (orphans.empty()) return 0;
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
std::size_t terminate_all_indexed(Millis, Millis) { return 0; }
bool termination_requested() { return false; }
bool shutting_down() { return false; }
void request_shutdown_for_test(int) {}
void clear_shutdown_for_test() {}
bool install_signal_cleanup(SignalCleanupOptions) { return false; }
void uninstall_signal_cleanup() {}
bool enable_subreaper() { return false; }
bool subreaper_enabled() { return false; }
std::size_t sweep_orphans(Millis, Millis, const std::function<bool(long)>&) { return 0; }
#endif

} // namespace nift::rt::host::runtime
