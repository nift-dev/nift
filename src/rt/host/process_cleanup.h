#pragma once
// CP16 Host layer: the per-kind cleanup hook for process resources (design
// §11.3, §11.5; D-030) and the context-end wait.
//
// CP17's handle table decides WHEN a process resource is cleaned up
// (ownership by execution context, reclamation of abandoned handles, detach
// transfer) and calls the kind's hook exactly once with the reason. This file
// decides only WHAT happens to the process. Following CP17's integration
// note, the hook never waits out a grace period: it STARTS the §11.4 sequence
// and returns; the context-end path then waits once for every group of the
// context against one deadline (ContextCleanup below).
//
//   Abandoned (last counted reference dropped):
//     running -> cancelled asynchronously (the reaper finishes it) and
//                "warning: process '<argv0>' (pid N) was abandoned while running; terminated"
//     exited  -> reaped silently, unread output discarded
//   Explicit (released by its owner): as Abandoned, without the warning.
//   ContextEnd:
//     live stage -> terminated and, unless the context ended on an error,
//                "warning: process '<argv0>' (pid N) was still running when <context> ended; terminated"
//     leftover background descendants (C1) -> terminated silently
//   Shutdown (CP17: detached handles at table shutdown): nothing.
//   Detached groups: never touched, whatever the reason.
//
// One warning per group (never one per pipeline stage), and only when Nift
// actually terminates a still-running process.
#include "rt/host/process.h"

#include <string>
#include <vector>

namespace nift::rt::host {

// Mirrors CP17's handles::ReleaseReason one to one.
enum class ProcessRelease : std::uint8_t { Explicit, Abandoned, ContextEnd, Shutdown };

struct CleanupReport {
    bool warned = false;
    bool live_stage_terminated = false;
    bool leftovers_terminated = false;
    bool handed_to_reaper = false;
    bool queued = false;  // waited for by the active ContextCleanup
};

// The context-end path (CP16-owned: the code that ends a script, page render,
// hook, build or --auto pass) wraps the table's close_context() in one of
// these: hooks running on this thread meanwhile add their groups to it, and
// wait() drives them all to completion against one deadline (grace + bound,
// not one grace per process). It also carries what CP17's CleanupEvent does
// not: the context's label for the warning and whether it ended on an error.
class ContextCleanup {
public:
    ContextCleanup(std::string label, bool failed);
    ~ContextCleanup();  // wait()s if not done yet
    ContextCleanup(const ContextCleanup&) = delete;
    ContextCleanup& operator=(const ContextCleanup&) = delete;
    static ContextCleanup* current();  // innermost on this thread, or null
    const std::string& label() const { return label_; }
    bool failed() const { return failed_; }
    void add(ProcessGroupPtr group);
    // Returns the number of groups that had to be handed to the reaper
    // because they were not settled by the deadline (Lost stages).
    std::size_t wait();

private:
    std::string label_;
    bool failed_;
    std::vector<ProcessGroupPtr> groups_;
    ContextCleanup* previous_;
    bool waited_ = false;
};

CleanupReport process_cleanup(const ProcessGroupPtr& group, ProcessRelease reason);

} // namespace nift::rt::host
