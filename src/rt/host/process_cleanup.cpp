#include "rt/host/process_cleanup.h"

#include "rt/host/process_runtime.h"

namespace nift::rt::host {

CleanupReport process_cleanup(const ProcessGroupPtr& group, ProcessRelease reason, const std::string& context_label,
                              bool context_error) {
    CleanupReport report;
    if (!group || group->detached() || reason == ProcessRelease::Shutdown) return report;
    const std::string who = "process '" + group->argv0() + "' (pid " + std::to_string(group->pid()) + ")";
    if (reason == ProcessRelease::Abandoned || reason == ProcessRelease::Explicit) {
        const bool live = group->running();
        group->cancel(CancelScope::Pipeline,
                      reason == ProcessRelease::Abandoned ? CancelReason::Abandoned : CancelReason::Cleanup);
        if (live) {
            report.live_stage_terminated = true;
            if (reason == ProcessRelease::Abandoned) {
                runtime::warn("warning: " + who + " was abandoned while running; terminated");
                report.warned = true;
            }
        }
    } else {
        const auto outcome = group->begin_terminate(CancelReason::Cleanup);
        report.live_stage_terminated = outcome.live_stage_terminated;
        report.leftovers_terminated = outcome.leftovers_terminated;
        if (outcome.live_stage_terminated && !context_error) {
            runtime::warn("warning: " + who + " was still running when " +
                          (context_label.empty() ? std::string("its context") : context_label) + " ended; terminated");
            report.warned = true;
        }
    }
    // Never block in a hook (D-035): the reaper keeps the group alive and its
    // host-loop timers finish the sequence; the table waits for the outcome.
    if (!group->settled()) {
        runtime::reaper_adopt(group);
        report.handed_to_reaper = true;
    }
    return report;
}

} // namespace nift::rt::host
