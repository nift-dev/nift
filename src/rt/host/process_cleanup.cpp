#include "rt/host/process_cleanup.h"

#include "rt/host/process_runtime.h"

#include <algorithm>
#include <thread>

namespace nift::rt::host {

namespace {
thread_local ContextCleanup* g_current = nullptr;

void settle_later(const ProcessGroupPtr& group, CleanupReport& report) {
    if (group->settled()) return;
    if (ContextCleanup* batch = ContextCleanup::current()) {
        batch->add(group);
        report.queued = true;
    } else {
        runtime::reaper_adopt(group);
        report.handed_to_reaper = true;
    }
}
} // namespace

ContextCleanup::ContextCleanup(std::string label, bool failed)
    : label_(std::move(label)), failed_(failed), previous_(g_current) {
    g_current = this;
}

ContextCleanup::~ContextCleanup() {
    wait();
    g_current = previous_;
}

ContextCleanup* ContextCleanup::current() { return g_current; }

void ContextCleanup::add(ProcessGroupPtr group) {
    if (group) groups_.push_back(std::move(group));
}

std::vector<std::string> ContextCleanup::wait() {
    std::vector<std::string> failures;
    if (waited_) return failures;
    waited_ = true;
    if (groups_.empty()) return failures;
    // The groups are driven by the host loop (timers + pidfd watches armed by
    // begin_terminate); this thread only sleeps until one of them settles.
    Millis bound{0};
    for (const auto& g : groups_) bound = std::max(bound, g->request().kill_after + g->request().post_kill_wait);
    const auto deadline = Clock::now() + bound + Millis(200);
    for (;;) {
        const auto seen = runtime::settle_generation();
        bool all = true;
        for (const auto& g : groups_) all = g->settled() && all;
        if (all || Clock::now() >= deadline) break;
        runtime::wait_settle_change(seen, deadline);
    }
    // Genuine cleanup failures are reported separately from the routine
    // warning, even when the context ended on an error (that error stays the
    // primary diagnostic; this report follows it).
    for (const auto& g : groups_) {
        const std::string who = "process '" + g->argv0() + "' (pid " + std::to_string(g->pid()) + ")";
        if (!g->settled()) {
            runtime::reaper_adopt(g);
            failures.push_back("error: cleanup: " + who + " could not be terminated within " + std::to_string(bound.count()) +
                               " ms; left to the reaper");
        } else if (g->cleanup_failed()) {
            failures.push_back("error: cleanup: " + who + " could not be terminated (still running after SIGKILL); left to the reaper");
        }
    }
    for (const auto& f : failures) runtime::report_cleanup_failure(f);
    groups_.clear();
    return failures;
}

CleanupReport process_cleanup(const ProcessGroupPtr& group, ProcessRelease reason) {
    CleanupReport report;
    if (!group || group->detached() || reason == ProcessRelease::Shutdown) return report;
    const std::string who = "process '" + group->argv0() + "' (pid " + std::to_string(group->pid()) + ")";
    if (reason == ProcessRelease::Abandoned || reason == ProcessRelease::Explicit) {
        const bool live = group->running();
        group->cancel(CancelScope::Pipeline, reason == ProcessRelease::Abandoned ? CancelReason::Abandoned : CancelReason::Cleanup);
        if (live) {
            report.live_stage_terminated = true;
            if (reason == ProcessRelease::Abandoned) {
                runtime::warn("warning: " + who + " was abandoned while running; terminated");
                report.warned = true;
            }
        }
        // Asynchronous (§11.5): the script is not blocked; the reaper keeps the
        // group alive and its host-loop timers finish the sequence.
        if (!group->settled()) {
            runtime::reaper_adopt(group);
            report.handed_to_reaper = true;
        }
        return report;
    }
    // ContextEnd: start now, wait once for the whole context.
    const auto outcome = group->begin_terminate(CancelReason::Cleanup);
    report.live_stage_terminated = outcome.live_stage_terminated;
    report.leftovers_terminated = outcome.leftovers_terminated;
    const ContextCleanup* batch = ContextCleanup::current();
    if (outcome.live_stage_terminated && !(batch && batch->failed())) {
        runtime::warn("warning: " + who + " was still running when " + (batch ? batch->label() : std::string("its context")) +
                      " ended; terminated");
        report.warned = true;
    }
    settle_later(group, report);
    return report;
}

} // namespace nift::rt::host
