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

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace nift::rt::host::runtime {

// ---- warnings (stderr by default; tests capture) ----
using WarningSink = std::function<void(const std::string&)>;
void set_warning_sink(WarningSink sink);  // empty: stderr
void warn(const std::string& line);

// ---- reaper ----
void reaper_add_pid(long pid);
void reaper_adopt(ProcessGroupPtr group);  // the reaper pumps it to completion
std::size_t reaper_pending();
// Test support: waits (bounded) until the reaper has nothing left; returns true if idle.
bool reaper_wait_idle(Millis bound);

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
// True once SIGINT/SIGTERM arrived with the handler installed. Blocking
// waits that complete afterwards park instead of returning, so the script
// cannot carry on (or exit with its own status) while the watcher cleans up
// and then ends Nift with the signal.
bool termination_requested();
[[noreturn]] void park_for_termination();
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

// ---- /proc helpers (Linux; empty elsewhere) ----
struct ProcInfo { long pid = 0; char state = '?'; long ppid = 0; long pgrp = 0; long session = 0; };
std::vector<ProcInfo> proc_scan();
bool proc_read(long pid, ProcInfo& out);
// Descendants of `roots` by parent links (excluding the roots).
std::vector<long> proc_descendants(const std::vector<long>& roots);
// Live (non-zombie) members of a process group; `session` >= 0 restricts to that session.
std::vector<long> proc_group_members(long pgid, long session);

} // namespace nift::rt::host::runtime
