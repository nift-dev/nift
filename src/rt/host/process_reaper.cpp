// CP16 Host layer: the reaper and the termination index.
#include "rt/host/process_reaper.h"

#include "rt/host/process_runtime.h"
#ifndef _WIN32
#include "ProcessPOSIX.h"  // nift_process_launch_mutex
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
std::map<const void*, IndexedGroup>& index_map() {
    static auto* m = new std::map<const void*, IndexedGroup>;
    return *m;
}
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
std::vector<long> detached_sessions_snapshot() {
    std::lock_guard<std::mutex> lock(detached_mutex());
    return std::vector<long>(detached_sessions().begin(), detached_sessions().end());
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

namespace {
struct DirectChild { long pid; unsigned long long start; };
std::mutex& direct_mutex() { static std::mutex m; return m; }
std::vector<DirectChild>& direct_children() { static auto* v = new std::vector<DirectChild>; return *v; }
bool matches(const DirectChild& c, const ProcInfo& p) {
    return c.pid == p.pid && (c.start == 0 || p.start == 0 || c.start == p.start);
}
std::atomic<bool> g_child_reporting{false};
} // namespace

void set_child_reporting(bool on) { g_child_reporting = on; }
bool child_reporting() { return g_child_reporting; }

void note_direct_child(long pid) {
    if (pid <= 0) return;
    ProcInfo p;
    const unsigned long long start = proc_read(pid, p) ? p.start : 0;
    std::lock_guard<std::mutex> lock(direct_mutex());
    direct_children().push_back(DirectChild{pid, start});
}

bool is_direct_child(long pid) {
    ProcInfo p;
    if (!proc_read(pid, p)) p.pid = pid;
    std::lock_guard<std::mutex> lock(direct_mutex());
    for (const auto& c : direct_children()) if (matches(c, p)) return true;
    return false;
}

namespace {
// Reaps the zombie children among `candidates` that are ours (see the
// header); `all` refreshes the bookkeeping of known groups and direct
// children (a full scan; null: only the candidates were read).
std::size_t reap_zombie_children(const std::vector<ProcInfo>& candidates, const std::vector<ProcInfo>* all) {
#ifdef __linux__
    const long self = static_cast<long>(::getpid());
    const long own_session = static_cast<long>(::getsid(0));
    std::set<long> stages;
    for (const auto& g : index_snapshot()) for (long p : g.stage_pids) stages.insert(p);
    std::vector<DirectChild> direct;
    {
        std::lock_guard<std::mutex> direct_lock(direct_mutex());
        auto& list = direct_children();
        // Forget direct children that are gone (reaped by their owners): a
        // full scan, or Nift's current children (a direct child is one until reaped).
        const std::vector<ProcInfo>* present = all ? all : &candidates;
        {
            std::vector<DirectChild> kept;
            for (const auto& c : list)
                for (const auto& p : *present)
                    if (matches(c, p)) { kept.push_back(c); break; }
            list.swap(kept);
        }
        direct = list;
    }
    auto is_direct = [&](const ProcInfo& p) {
        for (const auto& c : direct) if (matches(c, p)) return true;
        return false;
    };
    const bool adopted_rule = subreaper_enabled() && child_reporting();
    std::size_t reaped = 0;
    std::lock_guard<std::mutex> lock(managed_mutex());
    auto& known = managed_pgids();
    for (const auto& p : candidates) {
        if (p.ppid != self || p.state != 'Z' || stages.count(p.pid) || is_direct(p)) continue;
        // Ours: a group the Host layer created, another session, or -- under
        // the subreaper -- any child no other component reported (an orphan
        // adopted from a legacy run() stage, which stays in Nift's group).
        const bool ours = known.count(p.pgrp) || p.session != own_session || adopted_rule;
        if (!ours) continue;  // another component's child: theirs to wait for
        int status = 0;
        if (::waitpid(static_cast<pid_t>(p.pid), &status, WNOHANG) == static_cast<pid_t>(p.pid)) {
            ++reaped;
            test_event(nullptr, "orphan-reap", p.pid, p.pgrp, status);
        }
    }
    if (all) {
        // Forget groups that no longer exist, so a reused pgid of other code is never claimed.
        std::set<long> present;
        for (const auto& p : *all) present.insert(p.pgrp);
        for (auto it = known.begin(); it != known.end();) {
            if (!present.count(*it)) it = known.erase(it);
            else ++it;
        }
    }
    return reaped;
#else
    (void)candidates; (void)all;
    return 0;
#endif
}
} // namespace

std::size_t reap_adopted_orphans() {
#ifdef __linux__
    // A stage that exits between fork() and its registration in the index
    // must not be taken for an orphan: launches hold this mutex across both.
    std::lock_guard<std::mutex> launching(nift_process_launch_mutex());
    const auto all = proc_scan();
    return reap_zombie_children(all, &all);
#else
    return 0;
#endif
}

std::vector<long> own_children() {
    std::vector<long> out;
#ifdef __linux__
    DIR* tasks = ::opendir("/proc/self/task");
    if (!tasks) return out;
    while (dirent* task = ::readdir(tasks)) {
        if (task->d_name[0] == '.') continue;
        const std::string path = std::string("/proc/self/task/") + task->d_name + "/children";
        FILE* f = std::fopen(path.c_str(), "r");
        if (!f) continue;
        long pid = 0;
        while (std::fscanf(f, "%ld", &pid) == 1) out.push_back(pid);
        std::fclose(f);
    }
    ::closedir(tasks);
#endif
    return out;
}

std::size_t reap_adopted_children() {
#ifdef __linux__
    if (!subreaper_enabled()) return 0;
    std::lock_guard<std::mutex> launching(nift_process_launch_mutex());
    std::vector<ProcInfo> candidates;
    for (long pid : own_children()) {
        ProcInfo p;
        if (proc_read(pid, p)) candidates.push_back(p);
    }
    return reap_zombie_children(candidates, nullptr);
#else
    return 0;
#endif
}

#ifndef _WIN32
namespace {
bool pid_alive(long pid) {
#ifdef __linux__
    ProcInfo info;
    return proc_read(pid, info) && info.state != 'Z' && info.state != 'X';
#else
    return ::kill(static_cast<pid_t>(pid), 0) == 0;
#endif
}
// A group in Nift's own process group (own_group = false) is signalled and
// watched by its unreaped stage pids, never by group (that would be Nift).
bool group_alive(const IndexedGroup& g) {
    if (g.pgid <= 0) {
        for (long pid : g.stage_pids) if (pid_alive(pid)) return true;
        return false;
    }
#ifdef __linux__
    return !proc_group_members(g.pgid, static_cast<long>(::getsid(0))).empty();
#else
    return ::kill(-static_cast<pid_t>(g.pgid), 0) == 0;
#endif
}
void signal_group(const IndexedGroup& g, int sig, const char* site) {
    if (g.pgid > 0) { host_kill(-g.pgid, sig, site, nullptr); return; }
    for (long pid : g.stage_pids) if (pid_alive(pid)) host_kill(pid, sig, site, nullptr);
}
void sleep_until_or(Clock::time_point until, const std::function<bool()>& done) {
    while (Clock::now() < until && !done()) std::this_thread::sleep_for(Millis(10));
}
} // namespace

std::size_t terminate_all_indexed(Millis kill_after, Millis post_kill_wait) {
    std::vector<IndexedGroup> groups;
    for (auto& g : index_snapshot())
        if (!g.detached && (g.pgid > 0 || !g.stage_pids.empty()) && group_alive(g)) groups.push_back(g);
    for (const auto& g : groups) signal_group(g, SIGTERM, "c2-term");
    auto any_alive = [&] {
        for (const auto& g : groups) if (group_alive(g)) return true;
        return false;
    };
    sleep_until_or(Clock::now() + kill_after, [&] { return !any_alive(); });
    for (const auto& g : groups)
        if (group_alive(g)) signal_group(g, SIGKILL, "c2-kill");
    sleep_until_or(Clock::now() + post_kill_wait, [&] { return !any_alive(); });
    return groups.size();
}
#else
std::size_t terminate_all_indexed(Millis, Millis) { return 0; }
#endif

} // namespace nift::rt::host::runtime
