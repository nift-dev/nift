#pragma once
// CP16 Host layer: what happens to a process when its handle is cleaned up
// (design §11.3, §11.5; D-030, D-035). CP17's handle table decides WHEN
// (ownership by execution context, reference-counted abandonment, detach)
// and owns the lifecycle verdict; the binding (process_handles.h) calls this
// from the Process kind's cleanup hook. It never blocks: it STARTS the §11.4
// sequence and hands an unsettled group to the reaper, whose host-loop
// timers finish it; the outcome reaches the table through the lifecycle seam
// (process.h) and the moved-out CompletionTicket.
//
//   Abandoned (last counted reference dropped):
//     running -> cancelled asynchronously and
//                "warning: process '<argv0>' (pid N) was abandoned while running; terminated"
//     exited  -> reaped silently, unread output discarded
//   Explicit (closed by its owner): as Abandoned, without the warning.
//   ContextEnd:
//     live stage -> terminated and, unless the context is ending on an
//                error (CleanupEvent::context_error), "warning: process
//                '<argv0>' (pid N) was still running when <context> ended; terminated"
//     leftover background descendants (C1) -> terminated silently
//   Shutdown (detached handles at table shutdown): nothing.
//   Detached groups: never touched, whatever the reason.
//
// One warning per group (never one per pipeline stage), and only when Nift
// actually terminates a still-running process. Genuine cleanup failures are
// not printed here: the table reports them (CloseReport::failures) and they
// force a nonzero exit (D-035).
#include "rt/host/process.h"

#include <string>

namespace nift::rt::host {

// Mirrors CP17's handles::ReleaseReason one to one.
enum class ProcessRelease : std::uint8_t { Explicit, Abandoned, ContextEnd, Shutdown };

struct CleanupReport {
    bool warned = false;
    bool live_stage_terminated = false;
    bool leftovers_terminated = false;
    bool handed_to_reaper = false;
};

CleanupReport process_cleanup(const ProcessGroupPtr& group, ProcessRelease reason, const std::string& context_label,
                              bool context_error);

} // namespace nift::rt::host
