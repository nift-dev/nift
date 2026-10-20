// CP16 Host layer: shared process-runtime pieces (warnings, test events,
// signal recording, /proc helpers).
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
        if (p.pgrp == pgid && p.state != 'Z' && p.state != 'X' && (session < 0 || p.session == session))
            out.push_back(p.pid);
    return out;
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

} // namespace nift::rt::host::runtime
