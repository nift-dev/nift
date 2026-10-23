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
//
// This header holds the shared pieces (warnings, test hooks, signal
// recording, /proc helpers) and includes the parts: process_loop.h (host
// loop), process_reaper.h (reaper, termination index), process_signals.h
// (C2, subreaper, orphan sweep).
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
    // Microseconds to pause inside the cancellation start, between signalling
    // the stages and closing their stdin (pins that order: r2 TSan `deadlines`).
    std::atomic<int> cancel_gap_us{0};
    std::atomic<bool> close_exec_report{false};  // H2 test: parent drops the report read end
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

// ---- /proc helpers (Linux; empty elsewhere) ----
struct ProcInfo { long pid = 0; char state = '?'; long ppid = 0; long pgrp = 0; long session = 0; };
// Test/evidence: number of managed process-group ids currently remembered.
std::size_t managed_pgid_count();
std::vector<ProcInfo> proc_scan();
bool proc_read(long pid, ProcInfo& out);
// Descendants of `roots` by parent links (excluding the roots).
std::vector<long> proc_descendants(const std::vector<long>& roots);
// Live (non-zombie) members of a process group; `session` >= 0 restricts to that session.
std::vector<long> proc_group_members(long pgid, long session);

} // namespace nift::rt::host::runtime

#include "rt/host/process_loop.h"
#include "rt/host/process_reaper.h"
#include "rt/host/process_signals.h"
