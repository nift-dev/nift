// CP16 Host layer, POSIX internals: driving a group over time.
#ifndef _WIN32
#include "rt/host/process_posix_lifecycle.h"

#include "rt/host/process_posix_loop.h"
#include "rt/host/process_posix_sys.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <vector>

namespace nift::rt::host::posix {
// C1 leftovers: members still in the group after every stage was reaped.
// Started by begin_terminate() (or on the first pump after finish when a
// cleanup was requested); SIGTERM, grace, SIGKILL, bounded wait. Returns
// true when settled (nothing left to do).
bool leftover_step(State& s) {
    if (!s.cleanup_requested || s.left_phase == State::Phase::Done) return true;
    if (s.lost) {  // a stage already survived SIGKILL: reported as Lost, nothing more to try
        s.left_phase = State::Phase::Done;
        return true;
    }
    const auto now = Clock::now();
    const bool members = s.pgid > 0 && group_has_members(s);
    if (s.left_phase == State::Phase::None) {
        if (!members) { s.left_phase = State::Phase::Done; return true; }
#ifdef __linux__
        s.left_members = runtime::proc_group_members(s.pgid, static_cast<long>(::getsid(0)));
#endif
        runtime::test_event(&s, "leftover-start", s.pgid, static_cast<long>(s.left_members.size()));
        runtime::host_kill(-s.pgid, SIGTERM, "leftover-term", &s);
        s.left_phase = State::Phase::Term;
        s.left_at = now;
        return false;
    }
    if (!members) {
        s.left_phase = State::Phase::Done;
    } else if (s.left_phase == State::Phase::Term && now >= s.left_at + s.req.kill_after) {
        if (!kill_suppressed_for_test()) runtime::host_kill(-s.pgid, SIGKILL, "leftover-kill", &s);
        s.left_phase = State::Phase::Kill;
        s.left_at = now;
    } else if (s.left_phase == State::Phase::Kill && now >= s.left_at + s.req.post_kill_wait) {
        s.left_phase = State::Phase::Done;
        s.leftover_failed = true;  // still members after SIGKILL + bound
    }
    if (s.left_phase != State::Phase::Done) return false;
    for (long pid : s.left_members) runtime::reaper_add_pid(pid);  // ours only under the subreaper
    s.left_members.clear();
    return true;
}

// Detached groups: only an explicit cancel request has anything to drive.
bool detached_step(State& s) {
    if (s.phase == State::Phase::None || s.phase == State::Phase::Done) return true;
    const auto now = Clock::now();
    if (!group_has_members(s)) { s.phase = State::Phase::Done; return true; }
    if (s.phase == State::Phase::Term && now >= s.phase_at + s.kill_after) {
        if (!kill_suppressed_for_test()) runtime::host_kill(-s.pgid, SIGKILL, "detached-kill", &s);
        s.phase = State::Phase::Kill;
        s.phase_at = now;
    } else if (s.phase == State::Phase::Kill && now >= s.phase_at + s.req.post_kill_wait) {
        s.phase = State::Phase::Done;
        return true;
    }
    return false;
}

bool group_settled(const State& s) {
    if (s.detached) return s.phase == State::Phase::None || s.phase == State::Phase::Done;
    return s.finished && (!s.cleanup_requested || s.left_phase == State::Phase::Done);
}

// The event loop. The group lock is held except while sleeping in poll(),
// so other threads (cancel, the host loop's timers, context cleanup) can act
// on the group meanwhile; every state change happens under the lock.
bool run_loop(State& s, const void* key, std::unique_lock<std::mutex>& lk, std::optional<Clock::time_point> until,
              const std::function<bool()>& stop) {
    for (;;) {
        if (step(s, key)) return true;
        if (stop && stop()) return false;
        const auto now = Clock::now();
        Clock::time_point wake = Clock::time_point::max();
        if (until) wake = std::min(wake, *until);
        if (s.deadline && s.phase == State::Phase::None) wake = std::min(wake, *s.deadline);
        if (s.phase == State::Phase::Term) wake = std::min(wake, s.phase_at + s.kill_after);
        if (s.phase == State::Phase::Kill) wake = std::min(wake, s.phase_at + s.req.post_kill_wait);
        const bool tick = !s.pidfds_ok || s.req.mode == OutputMode::FileCapture || s.leftover_started || s.all_reaped();
        if (tick) wake = std::min(wake, now + Millis(s.req.mode == OutputMode::FileCapture ? 20 : 10));
        std::vector<pollfd> fds;
        auto room = [&](const Buffer& b) {
            return !(s.req.mode == OutputMode::Interactive && !s.waiting && !s.limit_hit &&
                     b.retained() >= s.req.buffer_limit);
        };
        if (s.out_fd >= 0 && room(s.out)) fds.push_back({s.out_fd, POLLIN, 0});
        if (s.err_fd >= 0 && room(s.err)) fds.push_back({s.err_fd, POLLIN, 0});
        if (s.in_fd >= 0 && s.in_pos < s.in_pending.size()) fds.push_back({s.in_fd, POLLOUT, 0});
        for (const auto& stage : s.st) if (stage.pidfd >= 0 && !stage.reaped) fds.push_back({stage.pidfd, POLLIN, 0});
        int timeout_ms = -1;
        if (wake != Clock::time_point::max()) {
            const auto left = std::chrono::duration_cast<Millis>(wake - now).count();
            timeout_ms = left <= 0 ? 0 : static_cast<int>(std::min<long long>(left + 1, 1 << 30));
        }
        if (fds.empty() && timeout_ms < 0) timeout_ms = 10;  // defensive: never sleep forever with nothing to watch
        lk.unlock();
        const int ready = ::poll(fds.empty() ? nullptr : fds.data(), static_cast<nfds_t>(fds.size()), timeout_ms);
        lk.lock();
        // Descriptors may have been closed by another thread meanwhile: use
        // the current ones; a stale readiness result only costs one pass.
        if (ready > 0) {
            if (s.out_fd >= 0) drain(s, s.out_fd, s.out, true);
            if (s.err_fd >= 0) drain(s, s.err_fd, s.err, false);
            if (s.in_fd >= 0) write_stdin(s);
        }
        if (until && Clock::now() >= *until) return step(s, key);
    }
}

// Arms the host loop for an unattended group: a timer for the next deadline /
// escalation / leftover step, and pidfd watches so an exit is reaped at once.
// Called with the group lock held at the end of every public operation.
void rearm(State& s) {
    for (auto id : s.loop_tasks) runtime::loop_cancel(id);
    s.loop_tasks.clear();
    if (group_settled(s)) return;
    const auto now = Clock::now();
    Clock::time_point wake = Clock::time_point::max();
    if (s.detached) {
        if (s.phase == State::Phase::Term) wake = s.phase_at + s.kill_after;
        if (s.phase == State::Phase::Kill) wake = s.phase_at + s.req.post_kill_wait;
        wake = std::min(wake, now + Millis(20));  // membership has no descriptor to watch
    } else if (!s.finished) {
        if (s.deadline && s.phase == State::Phase::None && !s.all_reaped()) wake = std::min(wake, *s.deadline);
        if (s.phase == State::Phase::Term) wake = std::min(wake, s.phase_at + s.kill_after);
        if (s.phase == State::Phase::Kill) wake = std::min(wake, s.phase_at + s.req.post_kill_wait);
        if (s.req.mode == OutputMode::FileCapture) wake = std::min(wake, now + Millis(20));  // capture-size check
        if (s.leftover_started || (s.all_reaped() && (s.waiting || s.phase != State::Phase::None)))
            wake = std::min(wake, now + Millis(10));
        if (!s.pidfds_ok && !s.all_reaped()) wake = std::min(wake, now + Millis(50));
        for (const auto& stage : s.st) {
            if (stage.pidfd < 0 || stage.reaped) continue;
            const int dup = ::fcntl(stage.pidfd, F_DUPFD_CLOEXEC, 3);
            if (dup < 0) continue;
            std::weak_ptr<ProcessGroup> w = s.self;
            s.loop_tasks.push_back(runtime::loop_when_readable(dup, [w] { if (auto g = w.lock()) g->timer_tick(); }));
        }
    } else {
        if (s.left_phase == State::Phase::Term) wake = s.left_at + s.req.kill_after;
        if (s.left_phase == State::Phase::Kill) wake = s.left_at + s.req.post_kill_wait;
        wake = std::min(wake, now + Millis(20));  // membership has no descriptor to watch
    }
    if (wake != Clock::time_point::max()) {
        std::weak_ptr<ProcessGroup> w = s.self;
        s.loop_tasks.push_back(runtime::loop_at(wake, [w] { if (auto g = w.lock()) g->timer_tick(); }));
    }
}

OsOutcome outcome_of(const State& s) {
    OsOutcome o;
    o.stage_unconfirmed = s.lost;
    o.members_survived = s.leftover_failed;
    const std::string who = "process '" + (s.req.stages.empty() ? std::string() : s.req.stages.front().program) +
                            "' (pid " + std::to_string(s.st.empty() ? -1L : static_cast<long>(s.st.front().pid)) + ")";
    if (s.lost) o.detail = who + " did not exit after SIGKILL; left to the reaper";
    else if (s.leftover_failed) o.detail = who + ": background members survived SIGKILL";
    return o;
}

// Collects lifecycle events that became due (lock held); the returned
// closure delivers them (call it after unlocking).
std::function<void()> collect_lifecycle(State& s) {
    if (!s.sink || s.detached) return nullptr;
    std::vector<std::function<void()>> events;
    auto sink = s.sink;
    if (!s.term_notified) {
        bool due = false;
        OsTermination cause = OsTermination::Exited;
        if (s.phase != State::Phase::None && s.reason == CancelReason::Deadline) {
            due = true;
            cause = OsTermination::Deadline;
        } else if (s.phase != State::Phase::None &&
                   (s.reason == CancelReason::Explicit || s.reason == CancelReason::Limit)) {
            due = true;
            cause = OsTermination::Cancel;
        } else if (s.finished || s.all_reaped())
            due = true;  // every stage exited by itself (leftover handling included)
        if (due) {
            s.term_notified = true;
            events.push_back([sink, cause] { sink->terminating(cause); });
        }
    }
    const bool now_settled = group_settled(s);
    if (now_settled && !s.settle_notified) {
        s.settle_notified = true;
        const OsOutcome facts = outcome_of(s);
        events.push_back([sink, facts] { sink->settled(facts); });
    } else if (!now_settled) {
        s.settle_notified = false;  // a cleanup made it unsettled again: report the next settle too
    }
    if (events.empty()) return nullptr;
    return [events] { for (const auto& e : events) e(); };
}

} // namespace nift::rt::host::posix
#endif  // !_WIN32
