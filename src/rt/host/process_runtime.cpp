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
#include <dirent.h>
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

// ---------------------------------------------------------------- reaper
namespace {
struct ReaperState {
    std::mutex mutex;
    std::condition_variable idle;
    std::set<long> pids;
    std::vector<ProcessGroupPtr> groups;
    bool running = false;
};
ReaperState& reaper() { static ReaperState* s = new ReaperState; return *s; }  // never destroyed: the thread may outlive statics

void reaper_loop() {
    auto& r = reaper();
    for (;;) {
        std::vector<ProcessGroupPtr> groups;
        std::vector<long> pids;
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            if (r.pids.empty() && r.groups.empty()) {
                r.running = false;
                r.idle.notify_all();
                return;
            }
            groups = r.groups;
            pids.assign(r.pids.begin(), r.pids.end());
        }
        std::vector<ProcessGroupPtr> done_groups;
        for (auto& g : groups)
            if (g->pump(Clock::now())) done_groups.push_back(g);
        std::vector<long> done_pids;
#ifndef _WIN32
        for (long pid : pids) {
            int status = 0;
            const pid_t got = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
            if (got == static_cast<pid_t>(pid) || (got < 0 && errno == ECHILD)) done_pids.push_back(pid);
        }
#else
        done_pids = pids;
#endif
        {
            std::lock_guard<std::mutex> lock(r.mutex);
            for (long pid : done_pids) r.pids.erase(pid);
            for (auto& g : done_groups) r.groups.erase(std::remove(r.groups.begin(), r.groups.end(), g), r.groups.end());
        }
        std::this_thread::sleep_for(Millis(20));
    }
}

void reaper_kick_locked(ReaperState& r) {
    if (r.running) return;
    r.running = true;
    std::thread(reaper_loop).detach();
}
} // namespace

void reaper_add_pid(long pid) {
    if (pid <= 0) return;
    auto& r = reaper();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.pids.insert(pid);
    reaper_kick_locked(r);
}

void reaper_adopt(ProcessGroupPtr group) {
    if (!group) return;
    auto& r = reaper();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.groups.push_back(std::move(group));
    reaper_kick_locked(r);
}

std::size_t reaper_pending() {
    auto& r = reaper();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.pids.size() + r.groups.size();
}

bool reaper_wait_idle(Millis bound) {
    auto& r = reaper();
    std::unique_lock<std::mutex> lock(r.mutex);
    return r.idle.wait_for(lock, bound, [&] { return !r.running && r.pids.empty() && r.groups.empty(); });
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
    for (const auto& g : groups) ::kill(-static_cast<pid_t>(g.pgid), SIGTERM);
    auto any_alive = [&] {
        for (const auto& g : groups) if (group_alive(g)) return true;
        return false;
    };
    sleep_until_or(Clock::now() + kill_after, [&] { return !any_alive(); });
    for (const auto& g : groups)
        if (group_alive(g)) ::kill(-static_cast<pid_t>(g.pgid), SIGKILL);
    sleep_until_or(Clock::now() + post_kill_wait, [&] { return !any_alive(); });
    return groups.size();
}

// ---------------------------------------------------------------- C2 signal cleanup
namespace {
int g_signal_pipe[2] = {-1, -1};
std::atomic<bool> g_terminating{false};
std::atomic<bool> g_installed{false};
struct sigaction g_old_int, g_old_term;
SignalCleanupOptions& signal_options() { static auto* o = new SignalCleanupOptions; return *o; }

extern "C" void nift_host_signal_handler(int signo) {
    const int saved = errno;
    g_terminating.store(true);
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
        if (options.on_done) { g_terminating = false; options.on_done(signo); continue; }
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

bool termination_requested() { return g_terminating.load(); }

void park_for_termination() {
    for (;;) std::this_thread::sleep_for(Millis(1000));
}

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

bool enable_subreaper() {
#ifdef __linux__
    if (g_subreaper) return true;
    if (::prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) == 0) { g_subreaper = true; return true; }
#endif
    return false;
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
    for (long pid : orphans) ::kill(static_cast<pid_t>(pid), SIGTERM);
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
    for (long pid : left) ::kill(static_cast<pid_t>(pid), SIGKILL);
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
void park_for_termination() { for (;;) std::this_thread::sleep_for(Millis(1000)); }
bool install_signal_cleanup(SignalCleanupOptions) { return false; }
void uninstall_signal_cleanup() {}
bool enable_subreaper() { return false; }
bool subreaper_enabled() { return false; }
std::size_t sweep_orphans(Millis, Millis, const std::function<bool(long)>&) { return 0; }
#endif

} // namespace nift::rt::host::runtime
