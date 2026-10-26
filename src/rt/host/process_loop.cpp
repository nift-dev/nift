// CP16 Host layer: the host loop.
#include "rt/host/process_loop.h"

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

// ---------------------------------------------------------------- host loop
// POSIX: poll() over a wake pipe, timers and fd watches. Windows (and POSIX
// builds with NIFT_HOST_LOOP_PORTABLE, which run the portable branch under
// the sanitizers): timers only, on a condition variable (no fd watches there).
#if !defined(_WIN32) && !defined(NIFT_HOST_LOOP_PORTABLE)
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
void loop_notify_sigchld() {
    g_sigchld.store(true);
    const int fd = g_loop_wake_fd.load();
    if (fd >= 0) { const char byte = 2; ssize_t ignored = ::write(fd, &byte, 1); (void)ignored; }
}
#else
// CP22: the portable host loop (timers). Same contract as the POSIX loop:
// one lazily started thread, callbacks run on it without the lock held, a
// timer never fires early, cancel is a no-op for fired/unknown ids.
namespace {
struct PortableLoop {
    std::mutex mutex;
    std::condition_variable cv;
    std::multimap<Clock::time_point, TaskId> timers;
    std::map<TaskId, std::pair<Clock::time_point, std::function<void()>>> tasks;
    TaskId next = 1;
    bool started = false;
};
PortableLoop& ploop() { static PortableLoop* s = new PortableLoop; return *s; }  // never destroyed (as above)

void ploop_thread() {
    auto& l = ploop();
    std::unique_lock<std::mutex> lock(l.mutex);
    for (;;) {
        if (l.timers.empty()) { l.cv.wait(lock); continue; }
        const auto when = l.timers.begin()->first;
        if (Clock::now() < when) { l.cv.wait_until(lock, when); continue; }
        std::vector<std::function<void()>> due;
        const auto now = Clock::now();
        while (!l.timers.empty() && l.timers.begin()->first <= now) {
            auto it = l.tasks.find(l.timers.begin()->second);
            l.timers.erase(l.timers.begin());
            if (it == l.tasks.end()) continue;
            due.push_back(std::move(it->second.second));
            l.tasks.erase(it);
        }
        lock.unlock();
        for (auto& fn : due) if (fn) fn();
        lock.lock();
    }
}
} // namespace

void loop_notify_sigchld() {}
TaskId loop_at(Clock::time_point when, std::function<void()> fn) {
    auto& l = ploop();
    std::lock_guard<std::mutex> lock(l.mutex);
    if (!l.started) { l.started = true; std::thread(ploop_thread).detach(); }
    const TaskId id = l.next++;
    l.tasks.emplace(id, std::make_pair(when, std::move(fn)));
    const bool earliest = l.timers.empty() || when < l.timers.begin()->first;
    l.timers.emplace(when, id);
    if (earliest) l.cv.notify_one();
    return id;
}
TaskId loop_when_readable(int, std::function<void()>) { return 0; }  // no descriptor watches on this path
void loop_cancel(TaskId id) {
    if (!id) return;
    auto& l = ploop();
    std::lock_guard<std::mutex> lock(l.mutex);
    auto it = l.tasks.find(id);
    if (it == l.tasks.end()) return;
    auto range = l.timers.equal_range(it->second.first);
    for (auto t = range.first; t != range.second; ++t) if (t->second == id) { l.timers.erase(t); break; }
    l.tasks.erase(it);
}
std::size_t loop_pending() {
    auto& l = ploop();
    std::lock_guard<std::mutex> lock(l.mutex);
    return l.tasks.size();
}
std::size_t loop_fd_count() { return 0; }
#endif

} // namespace nift::rt::host::runtime
