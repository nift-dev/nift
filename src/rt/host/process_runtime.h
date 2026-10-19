#pragma once
// CP16 Host layer: process-wide pieces of the process model (design §11.3,
// §11.4, §11.9).
//
//  - Reaper: a lazily started background thread that reaps pids nobody waits
//    for any more (Lost stages, detached processes adopted under the
//    subreaper, orphans) and completes the termination sequence of abandoned
//    groups asynchronously (§11.5). Never zombies while Nift lives.
//  - Termination index: the set of process groups with live members, used
//    ONLY for emergency termination when Nift itself receives SIGINT/SIGTERM
//    (C2) and to exclude managed stages from the orphan sweep. It records no
//    ownership (that is CP17's registry).
//  - SIGINT/SIGTERM cleanup (C2): opt-in handler + watcher thread: SIGTERM to
//    every indexed group, grace, SIGKILL, bounded wait, then Nift dies of the
//    same signal (signal-derived exit status). Detached processes untouched.
//  - Linux subreaper (PR_SET_CHILD_SUBREAPER) and the orphan sweep for root
//    context ends (§11.9 row 3).
#include "rt/host/process.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace nift::rt::host::runtime {

// ---- warnings (stderr by default; tests capture) ----
using WarningSink = std::function<void(const std::string&)>;
void set_warning_sink(WarningSink sink);  // empty: stderr
void warn(const std::string& line);

// ---- cleanup failures (reported separately from routine warnings) ----
// "error: cleanup: process 'x' (pid N) could not be terminated ..." -- printed
// even when the context ended on an error (that error stays the primary
// diagnostic; this is a second, distinct report).
void set_failure_sink(WarningSink sink);  // empty: stderr
void report_cleanup_failure(const std::string& line);

// ---- host loop (timers and one-shot fd watches) ----
// One lazily started thread that sleeps in poll() until the nearest timer or
// a watched descriptor becomes readable -- no periodic polling. Callbacks run
// on the loop thread without the loop's lock held. Generic on purpose: CP22's
// scheduler can take this over (or host it) without changing its users.
using TaskId = std::uint64_t;
TaskId loop_at(Clock::time_point when, std::function<void()> fn);  // one-shot timer (monotonic clock)
TaskId loop_when_readable(int fd, std::function<void()> fn);      // one-shot; the loop owns `fd` and closes it
void loop_cancel(TaskId id);                                       // no-op for fired/unknown ids
std::size_t loop_pending();

// ---- reaper (built on the host loop) ----
// Pids nobody waits for any more (Lost stages, detached processes adopted
// under the subreaper, orphans) are reaped when their pidfd becomes readable
// (fallback without pidfd: a 100 ms timer per pid). Adopted groups (abandoned
// or explicitly released handles) are kept alive and driven to completion by
// their own timers.
void reaper_add_pid(long pid);
void reaper_adopt(ProcessGroupPtr group);
// Called by a group once settled; true if it was adopted (the reaper then
// reports a genuine cleanup failure itself: nobody else will see it).
bool reaper_release(const ProcessGroup* group);
// Settle notifications: a counter bumped whenever a group settles, so a
// context end can sleep until something changes instead of polling.
std::uint64_t settle_generation();
void notify_settled();
void wait_settle_change(std::uint64_t seen, Clock::time_point until);
std::size_t reaper_pending();
// Test support: waits (bounded) until the reaper has nothing left; returns true if idle.
bool reaper_wait_idle(Millis bound);

// ---- test hooks (only with -DNIFT_HOST_TEST_HOOKS; never user-controllable) ----
#ifdef NIFT_HOST_TEST_HOOKS
// Event trace for diagnosing OS-level ordering (monotonic timestamps, pids,
// pgids, signals sent, pipe EOFs, reaps, state transitions).
struct EventRecord {
    Clock::time_point at;
    const void* group;
    const char* what;   // spawn, signal, eof, reap, cancel, finish, tick, ...
    long a, b, c;       // meaning per event (documented at the call sites)
    const char* detail;
};
struct TestHooks {
    std::atomic<bool> suppress_sigkill{false};
    std::atomic<bool> record{false};
    std::mutex log_mutex;
    std::vector<EventRecord> log;
};
TestHooks& test_hooks();
#endif
// Records one event in the test build (no-op otherwise).
void test_event(const void* group, const char* what, long a = 0, long b = 0, long c = 0, const char* detail = "");
// Every signal the Host layer sends goes through here (target < 0: a process
// group), so the test build can record it.
int host_kill(long target, int sig, const char* site, const void* group);

// ---- termination index (not ownership) ----
struct IndexedGroup { long pgid = -1; std::vector<long> stage_pids; bool detached = false; };
void index_add(const void* key, IndexedGroup group);
void index_update_pids(const void* key, std::vector<long> unreaped_stage_pids);
void index_remove(const void* key);
std::vector<IndexedGroup> index_snapshot();

// ---- C2: Nift receives SIGINT / SIGTERM ----
struct SignalCleanupOptions {
    Millis kill_after{2000};
    Millis post_kill_wait{5000};
    // Test hook: called instead of re-raising the signal (the default ends
    // Nift with the signal's default action, so the exit status is 130/143).
    std::function<void(int)> on_done;
};
bool install_signal_cleanup(SignalCleanupOptions options = SignalCleanupOptions());
void uninstall_signal_cleanup();
// True once SIGINT/SIGTERM arrived with the handler installed: Nift is
// shutting down. From then on no new managed process is launched
// (`host.shutting_down`) and HostCall refuses every externally visible op
// (writes, spawns); the build engine checks the same flag before starting a
// build or a page render (wiring later). Running code may continue to its
// shutdown path while the watcher terminates the managed children (SIGTERM,
// grace, SIGKILL, bounded wait); then Nift ends with the signal. If the
// program exits first, an atexit handler waits for the watcher (bounded) and
// re-raises, so the exit status is always signal-derived. This is a bounded
// normal-shutdown guarantee only: SIGKILL or a crash runs none of it.
bool termination_requested();
bool shutting_down();
void request_shutdown_for_test(int signo);  // marks shutting down without a signal (tests)
void clear_shutdown_for_test();
// Runs the C2 sequence synchronously (also used by the watcher thread).
// Returns the number of groups that still had live members.
std::size_t terminate_all_indexed(Millis kill_after, Millis post_kill_wait);

// ---- subreaper and orphan sweep (Linux) ----
bool enable_subreaper();   // false where unavailable
bool subreaper_enabled();
// Terminates and reaps processes reparented to Nift that belong to no
// indexed group and to no detached session (sid), excluding pids for which
// `foreign` returns true (children launched by other code, e.g. legacy job
// control). Call at the end of a root context (script, build, --auto pass).
// Returns the number of orphans terminated.
std::size_t sweep_orphans(Millis kill_after, Millis post_kill_wait,
                          const std::function<bool(long pid)>& foreign = nullptr);
void note_detached_session(long sid);
// Every process group the Host layer creates is noted, so adopted orphans
// can be told apart from children launched by other code.
void note_managed_pgid(long pgid);
// Reaps zombie children that are adopted orphans (under the subreaper): a
// process group Nift created, or a session other than Nift's (escapees,
// detached sessions). Never an unreaped stage of a live group, never a
// child of other code in Nift's own session and groups (no waitpid(-1)).
// Runs on the host loop after SIGCHLD; returns how many were reaped.
std::size_t reap_adopted_orphans();

// ---- /proc helpers (Linux; empty elsewhere) ----
struct ProcInfo { long pid = 0; char state = '?'; long ppid = 0; long pgrp = 0; long session = 0; };
std::vector<ProcInfo> proc_scan();
bool proc_read(long pid, ProcInfo& out);
// Descendants of `roots` by parent links (excluding the roots).
std::vector<long> proc_descendants(const std::vector<long>& roots);
// Live (non-zombie) members of a process group; `session` >= 0 restricts to that session.
std::vector<long> proc_group_members(long pgid, long session);

} // namespace nift::rt::host::runtime
