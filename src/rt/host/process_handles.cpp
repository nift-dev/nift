#include "rt/host/process_handles.h"

#include "rt/host/process_cleanup.h"
#include "rt/host/process_runtime.h"

namespace nift::rt::host {

namespace {

handles::TerminationCause cause_of(OsTermination cause) {
    switch (cause) {
        case OsTermination::Exited: return handles::TerminationCause::Exited;
        case OsTermination::Deadline: return handles::TerminationCause::Deadline;
        case OsTermination::Cancel: return handles::TerminationCause::Cancel;
    }
    return handles::TerminationCause::Exited;
}

ProcessRelease release_of(handles::ReleaseReason reason) {
    switch (reason) {
        case handles::ReleaseReason::Explicit: return ProcessRelease::Explicit;
        case handles::ReleaseReason::Abandoned: return ProcessRelease::Abandoned;
        case handles::ReleaseReason::ContextEnd: return ProcessRelease::ContextEnd;
        case handles::ReleaseReason::Shutdown: return ProcessRelease::Shutdown;
    }
    return ProcessRelease::ContextEnd;
}

// The Process kind's cleanup hook. Never blocks (D-035).
handles::CleanupOutcome process_hook(handles::CleanupEvent& event) {
    auto resource = std::static_pointer_cast<ProcessResource>(event.resource);
    if (!resource || !resource->group()) return handles::CleanupOutcome::completed();
    // Detached (D-039): bookkeeping only; managed cleanup never touches the process.
    if (event.owner == handles::kDetached || resource->group()->detached()) return handles::CleanupOutcome::completed();
    const bool own_ticket = event.ticket != nullptr;  // the lifecycle was Active: this cleanup terminates it
    if (own_ticket) resource->take_ticket(std::move(*event.ticket));
    process_cleanup(resource->group(), release_of(event.reason), event.context_label, event.context_error);
    if (!own_ticket) return handles::CleanupOutcome::completed();  // the existing ticket decides
    resource->complete_if_settled();
    return handles::CleanupOutcome::pending();  // completed through the moved-out ticket when the group settles
}

} // namespace

handles::CleanupOutcome ProcessResource::outcome_for(const OsOutcome& facts) {
    if (facts.stage_unconfirmed) return handles::CleanupOutcome::lost("process.lost", facts.detail);
    if (facts.members_survived)
        return handles::CleanupOutcome::alive_after_kill("process.alive_after_kill", facts.detail);
    return handles::CleanupOutcome::completed();
}

void ProcessResource::bind(handles::HandleTable* table, const Value& handle) {
    std::lock_guard<std::mutex> lock(mu_);
    table_ = table;
    handle_ = handle;
}

void ProcessResource::terminating(OsTermination cause) {
    handles::HandleTable* table = nullptr;
    Value handle;
    {
        std::lock_guard<std::mutex> lock(mu_);
        table = table_;
        handle = handle_;
    }
    if (!table) return;
    handles::CompletionTicket ticket;
    handles::TerminationStart info;
    // Closed / already terminating / detached: no ticket; termination happens once.
    if (table->begin_termination(handle, HandleKind::Process, cause_of(cause), ticket, &info) == HandleStatus::Ok &&
        info.started) {
        take_ticket(std::move(ticket));
        // Lifecycle events are delivered after the group's lock is released,
        // possibly on two threads (a cancel() caller's `terminating`, the host
        // loop's `settled`): a `settled` delivered before this ticket existed
        // completed nothing. The group's state is settled before its event is
        // generated, so checking it after taking the ticket closes the gap
        // (r3: deadline_races hung in close_context, 1 in ~200 rounds).
        complete_if_settled();
    }
}

void ProcessResource::take_ticket(handles::CompletionTicket ticket) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!ticket_) ticket_ = std::move(ticket);
}

void ProcessResource::complete_locked(const OsOutcome& facts) {
    ticket_.complete(outcome_for(facts));  // the table decides and records the state
}

void ProcessResource::settled(const OsOutcome& facts) {
    bool completed = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (ticket_) {
            complete_locked(facts);
            completed = true;
        }
    }
    // Reported: break the group -> sink -> group cycle (never called with the group's lock held).
    if (completed) group_->set_lifecycle_sink(nullptr);
}

void ProcessResource::complete_if_settled() {
    if (!group_->settled()) return;  // the seam reports the settle later
    settled(group_->os_outcome());
}

HandleStatus register_process_kind(handles::HandleTable& table) {
    handles::KindSpec spec;
    spec.name = "process";
    spec.cleanup = process_hook;
    spec.detach_forbidden_code = "process.detach_in_build";
    return table.register_typed_kind<ProcessResource>(HandleKind::Process, std::move(spec));
}

HandleStatus create_process_handle(handles::HandleTable& table, handles::ContextId context,
                                   const ProcessGroupPtr& group, Value& out) {
    auto resource = std::make_shared<ProcessResource>(group);
    handles::CreateOptions options;
    options.counted = true;  // abandonment by reference count (D-035); detachable (R14)
    const HandleStatus status = table.create(context, HandleKind::Process, resource, out, options);
    if (status != HandleStatus::Ok) return status;
    resource->bind(&table, out);
    group->set_lifecycle_sink(resource);
    return HandleStatus::Ok;
}

HandleStatus create_detached_handle(handles::HandleTable& table, handles::ContextId context,
                                    const ProcessGroupPtr& group, Value& out) {
    auto resource = std::make_shared<ProcessResource>(group);
    handles::CreateOptions options;
    options.counted = true;  // only counted handles can be detached (R14)
    HandleStatus status = table.create(context, HandleKind::Process, resource, out, options);
    if (status != HandleStatus::Ok) {
        group->cancel(CancelScope::Pipeline, CancelReason::Explicit);  // never left running unowned
        runtime::reaper_adopt(group);
        return status;
    }
    resource->bind(&table, out);
    status = table.detach_as(out, HandleKind::Process, context);
    if (status != HandleStatus::Ok) {
        group->cancel(CancelScope::Pipeline, CancelReason::Explicit);
        runtime::reaper_adopt(group);
        table.close(out, HandleKind::Process, context);
    }
    return status;
}

HandleStatus register_run_residue(handles::HandleTable& table, handles::ContextId context, const ProcessGroupPtr& group,
                                  bool& registered) {
    registered = false;
    if (!group || group->detached() || !group->has_live_members()) return HandleStatus::Ok;
    auto resource = std::make_shared<ProcessResource>(group);
    Value handle;
    const HandleStatus status =
        table.create(context, HandleKind::Process, resource, handle);  // uncounted: never script-visible
    if (status != HandleStatus::Ok) return status;
    resource->bind(&table, handle);
    group->set_lifecycle_sink(resource, false);  // the call's own exit is not this resource's lifecycle
    registered = true;
    return HandleStatus::Ok;
}

HandleStatus cancel_process(handles::HandleTable& table, const Value& handle, handles::ContextId accessor,
                            bool& anything) {
    anything = false;
    std::shared_ptr<ProcessResource> resource;
    const HandleStatus status = table.resolve_as(handle, HandleKind::Process, accessor, resource);
    if (status != HandleStatus::Ok) return status;  // Closed -> runtime.handle_closed at the native
    if (resource->group()->detached()) {
        handles::CompletionTicket ticket;
        handles::TerminationStart info;
        table.begin_termination(handle, HandleKind::Process, handles::TerminationCause::Cancel, ticket, &info);
        if (info.detached_request) {
            anything = resource->group()->cancel();
            if (anything) runtime::reaper_adopt(resource->group());  // drives the escalation; nothing is supervised
        }
        return HandleStatus::Ok;
    }
    anything = resource->group()->cancel();  // the seam reports Cancel and takes the ticket
    return HandleStatus::Ok;
}

HandleStatus detach_refused_here(const handles::HandleTable& table, handles::ContextId context, bool& refused) {
    return table.context_in_build(context, refused);
}

} // namespace nift::rt::host
