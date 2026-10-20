#pragma once
// CP16 Host layer: the reaper (pids and groups nobody waits for any more)
// and the termination index (live groups, for C2 and the orphan sweep only).
#include "rt/host/process.h"
#include "rt/host/process_loop.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace nift::rt::host::runtime {

// ---- reaper (built on the host loop) ----
// Pids nobody waits for any more (Lost stages, detached processes adopted
// under the subreaper, orphans) are reaped when their pidfd becomes readable
// (fallback without pidfd: a 100 ms timer per pid). Adopted groups (abandoned
// or explicitly released handles) are kept alive and driven to completion by
// their own timers.
void reaper_add_pid(long pid);
void reaper_adopt(ProcessGroupPtr group);
// Called by a group once settled; true if it was adopted (and is now released).
bool reaper_release(const ProcessGroup* group);
std::size_t reaper_pending();
// Test support: waits (bounded) until the reaper has nothing left; returns true if idle.
bool reaper_wait_idle(Millis bound);

// ---- termination index (not ownership) ----
struct IndexedGroup { long pgid = -1; std::vector<long> stage_pids; bool detached = false; };
void index_add(const void* key, IndexedGroup group);
void index_update_pids(const void* key, std::vector<long> unreaped_stage_pids);
void index_remove(const void* key);
std::vector<IndexedGroup> index_snapshot();
void note_detached_session(long sid);
std::vector<long> detached_sessions_snapshot();
// Every process group the Host layer creates is noted, so adopted orphans
// can be told apart from children launched by other code.
void note_managed_pgid(long pgid);
// Reaps zombie children that are adopted orphans (under the subreaper): a
// process group Nift created, or a session other than Nift's (escapees,
// detached sessions). Never an unreaped stage of a live group, never a
// child of other code in Nift's own session and groups (no waitpid(-1)).
// Runs on the host loop after SIGCHLD; returns how many were reaped.
std::size_t reap_adopted_orphans();
// Direct children of other components (legacy launcher, job control, serve):
// reported through nift_process_note_child (ProcessPOSIX.h) under the launch
// mutex, identified by pid and start time. Under the subreaper every other
// child of Nift that is no stage is an adopted orphan.
void note_direct_child(long pid);
// Switches on "every other child is an adopted orphan" (only once every fork
// site reports its children: the legacy interpreter, enable_subreaper_for_legacy).
void set_child_reporting(bool on);
bool child_reporting();
// Nift's current children (Linux: /proc/self/task/*/children; cheap, no full scan).
std::vector<long> own_children();
// reap_adopted_orphans() over own_children() only, without SIGCHLD: for the
// legacy interpreter (after each run() and at script end). Returns how many.
std::size_t reap_adopted_children();
bool is_direct_child(long pid);
// Runs the C2 sequence synchronously (also used by the watcher thread).
// Returns the number of groups that still had live members.
std::size_t terminate_all_indexed(Millis kill_after, Millis post_kill_wait);

} // namespace nift::rt::host::runtime
