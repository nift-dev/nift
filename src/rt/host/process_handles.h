#pragma once
// CP16 x CP17 binding: process resources on the CP17 handle table.
//
// CP17 (src/rt/handles/) owns handle identity, reference counts, context
// ownership and the authoritative lifecycle (Active -> Terminating ->
// Completed | Failed | Lost | CleanupIncomplete). CP16 owns the OS operations
// and reports their facts once (process.h LifecycleSink). This is the only
// file that sees both sides (cp16-host-ops-impl.md §8).
//
//   * register_process_kind(): HandleKind::Process with the cleanup hook and
//     `process.detach_in_build` for DetachForbidden.
//   * Script-visible process handles are counted (abandonment by reference
//     count, D-035; only counted handles can be detached, R14).
//   * Terminations CP16 starts itself (exit, deadline, cancel) call
//     begin_termination(); the first one gets the CompletionTicket.
//   * Close / abandonment / context end reach CP16 through the hook: it moves
//     the ticket out, starts termination (never blocking) and returns
//     pending(); the ticket is completed when the group settles.
//   * Mapping (owner decision): every stage observed and nothing survived ->
//     completed(); a stage unconfirmed after SIGKILL + bound -> lost();
//     members alive after SIGKILL + bound -> alive_after_kill().
#include "rt/handles/completion.h"
#include "rt/handles/table.h"
#include "rt/host/process.h"

#include <memory>
#include <mutex>
#include <string>

namespace nift::rt::host {

// The resource stored in the table for HandleKind::Process.
class ProcessResource final : public LifecycleSink, public std::enable_shared_from_this<ProcessResource> {
public:
    explicit ProcessResource(ProcessGroupPtr group) : group_(std::move(group)) {}
    const ProcessGroupPtr& group() const { return group_; }

    // LifecycleSink
    void terminating(OsTermination cause) override;
    void settled(const OsOutcome& facts) override;

    // Binding internals (used by the hook and the factories).
    void bind(handles::HandleTable* table, const Value& handle);
    void take_ticket(handles::CompletionTicket ticket);
    void complete_if_settled();
    static handles::CleanupOutcome outcome_for(const OsOutcome& facts);

private:
    void complete_locked(const OsOutcome& facts);
    ProcessGroupPtr group_;
    std::mutex mu_;
    handles::HandleTable* table_ = nullptr;
    Value handle_;
    handles::CompletionTicket ticket_;
};

// Registers HandleKind::Process on `table` (once per table).
HandleStatus register_process_kind(handles::HandleTable& table);

// A started (interactive) process: counted handle owned by `context`.
HandleStatus create_process_handle(handles::HandleTable& table, handles::ContextId context,
                                   const ProcessGroupPtr& group, Value& out);

// A detached process: counted handle, then detached at once (D-039). If the
// detach is refused (e.g. DetachForbidden in a build family), the process is
// cancelled immediately and the handle closed: nothing runs unowned.
HandleStatus create_detached_handle(handles::HandleTable& table, handles::ContextId context,
                                    const ProcessGroupPtr& group, Value& out);

// A blocking run() whose call returned with background members still in its
// group (C1): an uncounted, script-invisible resource in `context`, so the
// context end terminates and settles them like any handle. No-op (Ok,
// `registered` false) when nothing is left.
HandleStatus register_run_residue(handles::HandleTable& table, handles::ContextId context, const ProcessGroupPtr& group,
                                  bool& registered);

// p.cancel(): attached processes start the §11.4 sequence (the lifecycle
// learns it through the seam); detached ones get the cancel request only
// (TerminationStart::detached_request: no ticket, nothing supervised).
HandleStatus cancel_process(handles::HandleTable& table, const Value& handle, handles::ContextId accessor,
                            bool& anything);

// Whether a detached launch must be refused before launching (build family).
HandleStatus detach_refused_here(const handles::HandleTable& table, handles::ContextId context, bool& refused);

} // namespace nift::rt::host
