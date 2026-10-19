#pragma once
// Vocabulary of the CP17 handle table: execution contexts, the authoritative
// resource lifecycle, cleanup hooks and their outcomes, cleanup reports and
// table options. No RuntimeValue.
#include "rt/handle.h"
#include "rt/value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nift::rt::handles {

class CompletionTicket;  // rt/handles/completion.h

// Execution contexts are identified by ids that are never reused, so a stale
// context id fails closed (ContextClosed / UnknownContext).
using ContextId = std::uint64_t;
// Owner of a detached handle: no execution context owns it.
inline constexpr ContextId kDetached = 0;
// Accessor for trusted host code (reapers, the host's own bookkeeping) that
// resolves without an ownership check. Natives always pass their context.
inline constexpr ContextId kAnyAccessor = ~ContextId(0);

// D-029/D-030 context kinds. Build-family contexts (Build, BuildHook,
// PageRender, AutoRebuild) and every context nested in one forbid detaching.
enum class ContextKind : std::uint8_t { Script, PageRender, BuildHook, Build, AutoRebuild };

// Why a handle's slot is being given up. A kind's hook sees exactly one
// cleanup event per handle, whatever the reason.
enum class ReleaseReason : std::uint8_t {
    Explicit,    // close/release by the owner (or a context nested in it)
    Abandoned,   // the last counted reference was dropped
    ContextEnd,  // the owning context ended with the handle still owned (also:
                 // a refused transfer with TransferPolicy::ReclaimOnRefusal)
    Shutdown,    // the table shut down (detached handles; contexts left open)
};
const char* release_reason_name(ReleaseReason reason);

// Authoritative lifecycle of a resource (D-035). The table owns it and alone
// decides it; CP16 only reports OS facts through a CompletionTicket.
//   Active --(termination begins: close, last reference, context end,
//             shutdown, or CP16's deadline / cancel / natural exit)--> Terminating
//   Terminating --(ticket completed exactly once)--> one of the settled states
// Settled states (owner definitions):
//   Completed          the required cleanup finished (reaped, closed; for a
//                      detached process: bookkeeping released, process lives on)
//   Failed             the resource is known to be alive after forced termination
//   Lost               termination could not be confirmed (never "still running")
//   CleanupIncomplete  the resource is known to have ended (exited, closed) but
//                      reaping or handle cleanup could not be completed (never
//                      "still alive", never Lost)
// Failed, Lost and CleanupIncomplete are cleanup failures: the run exits
// nonzero (CloseReport::final_exit_status). Terminating (graceful termination
// in progress) is not a failure.
enum class LifeState : std::uint8_t { Active, Terminating, Completed, Failed, Lost, CleanupIncomplete };
const char* life_state_name(LifeState state);

// What started termination. Deadline/Cancel/Exited outcomes stay on the
// resource's own result (CP16); the handle stays usable until closed.
enum class TerminationCause : std::uint8_t { None, Exited, Deadline, Cancel, Close, Abandoned, ContextEnd, Shutdown };
const char* termination_cause_name(TerminationCause cause);

struct CleanupEvent {
    HandleKind kind;
    Value handle;                             // the (already closed) handle
    ContextId owner;                          // kDetached for detached handles
    ReleaseReason reason;
    const std::shared_ptr<void>& resource;    // the hook may copy it (the reaper's own ownership)
    LifeState state;                          // lifecycle when cleanup began
    TerminationCause cause;                   // what started termination (None while Active)
    // The owning context's label ("page 'index'", "build", "script x.f"; empty
    // when detached) and whether that context is ending on an error (D-035:
    // one source of truth for CP16's warnings; false unless reason is ContextEnd).
    const std::string& context_label;         // valid for the duration of the hook call
    bool context_error;
    // Detached resources (owner == kDetached): never a ticket; the hook only
    // releases bookkeeping, its result is ignored and the table records
    // Completed at once (D-039: no supervision kept for a detached outcome).
    // Otherwise non-null exactly when `state` was Active: the hook is then responsible
    // for the resource's termination and must either return a final outcome
    // or move the ticket out, return pending() and complete it later. Null
    // when termination was already started (a deadline, a cancel) or settled:
    // the existing ticket decides, and the hook only releases bookkeeping.
    CompletionTicket* ticket;
};

// What a hook (or a ticket) reports: OS facts, mapped one to one onto the
// settled states. Pending means "scheduled, the moved-out ticket will report".
struct CleanupOutcome {
    enum class Result : std::uint8_t { Completed, Failed, Lost, CleanupIncomplete, Pending };
    Result result = Result::Completed;
    std::string code;     // specific code; empty -> the state's default
    std::string message;  // empty -> the state's default
    static CleanupOutcome completed() { return {}; }
    static CleanupOutcome success() { return {}; }
    // Known alive after forced termination (SIGKILL / TerminateJobObject).
    static CleanupOutcome alive_after_kill(std::string code, std::string message) {
        return CleanupOutcome{Result::Failed, std::move(code), std::move(message)};
    }
    // Termination could not be confirmed.
    static CleanupOutcome lost(std::string code, std::string message) {
        return CleanupOutcome{Result::Lost, std::move(code), std::move(message)};
    }
    // Known to have ended (exited, closed), but reaping or cleanup failed.
    static CleanupOutcome cleanup_incomplete(std::string code, std::string message) {
        return CleanupOutcome{Result::CleanupIncomplete, std::move(code), std::move(message)};
    }
    static CleanupOutcome exited_cleanup_incomplete(std::string code, std::string message) {
        return cleanup_incomplete(std::move(code), std::move(message));
    }
    // A cleanup step failed (e.g. a file's final flush): CleanupIncomplete.
    // Never implies "alive"; use alive_after_kill() for that fact.
    static CleanupOutcome failure(std::string code, std::string message) {
        return cleanup_incomplete(std::move(code), std::move(message));
    }
    static CleanupOutcome pending() { return CleanupOutcome{Result::Pending, {}, {}}; }
};

// Runs without the table lock held, on the thread that triggered cleanup
// (which may be VM execution dropping its last reference): it must not block
// (D-035: schedule, then complete the ticket later). It may resolve, create
// (in an open context), close or drop other handles, but must not close the
// context that owns the handle being cleaned. An exception escaping a hook is
// caught and settles the resource as Lost (`internal.handle_cleanup_threw`:
// whether the resource ended is unknown).
using CleanupHook = std::function<CleanupOutcome(CleanupEvent&)>;

struct KindSpec {
    std::string name;                 // "file", "stream", "process" ... (diagnostics only)
    CleanupHook cleanup;              // empty: cleanup completes immediately
    // Code reported for DetachForbidden on this kind (CP16 registers
    // "process.detach_in_build"); empty: "runtime.detach_in_build".
    std::string detach_forbidden_code;
    const void* type_tag = nullptr;   // set by register_typed_kind<T>
};

// A resource whose lifecycle settled Failed, Lost or CleanupIncomplete,
// reported in its owner context's CloseReport (detached: shutdown()).
struct CleanupFailure {
    Value handle;
    HandleKind kind = HandleKind::None;
    ContextId owner = kDetached;
    ReleaseReason reason = ReleaseReason::Explicit;   // why the slot was given up (Explicit if still open)
    TerminationCause cause = TerminationCause::None;  // what started termination
    LifeState state = LifeState::Failed;              // Failed, Lost or CleanupIncomplete
    std::string context_label;                        // the owner's label at settle time (empty: detached)
    std::string code;
    std::string message;
};

struct CloseReport {
    HandleStatus status = HandleStatus::Ok;   // Ok, ContextClosed or UnknownContext
    std::string label;                        // the closed context's label
    bool on_error = false;                    // it ended on an error (close_context(id, true))
    std::size_t cleaned = 0;                  // handles whose cleanup this close started
    std::size_t contexts = 0;                 // contexts ended (the context and its descendants)
    // The context was already being closed by another close_context call
    // (status ContextClosed): this call returned only after it was gone, and
    // its failures are in that call's report. For a parent close: the number of
    // descendants another close owned (their failures are in that report).
    bool closed_elsewhere = false;
    std::size_t descendants_closed_elsewhere = 0;
    std::vector<CleanupFailure> failures;     // in settle order (D-035: every unresolved obligation
                                              // was resolved before this report was produced)
    // shutdown() only: failures Nift actually knows about for detached
    // resources (a hook that threw while releasing bookkeeping). D-039: they
    // never change the exit status, and Nift keeps no supervision to find more.
    std::vector<CleanupFailure> detached_failures;
    bool cleanup_failed() const { return !failures.empty(); }
    // D-035/D-020: the run's exit status after this cleanup. A nonzero
    // `original` (an earlier error, or a signal-derived 130/143) stays primary;
    // otherwise a cleanup failure makes it 1.
    int final_exit_status(int original) const { return original != 0 ? original : (cleanup_failed() ? 1 : 0); }
};

struct CreateOptions {
    // Counted handles carry a reference count starting at 1 (the creator's
    // reference); dropping the last one schedules cleanup (Abandoned).
    // Uncounted handles live until closed or until their owning context ends.
    bool counted = false;
};

// What transfer() does when the destination has ended or is cancelling.
// D-035: a refused transfer reclaims the resource (the default).
enum class TransferPolicy : std::uint8_t {
    ReclaimOnRefusal,  // the resource is cleaned up now (reason ContextEnd)
    KeepOnRefusal,     // the source keeps it (its context end is the fallback)
};

inline constexpr const char* kHandleLimitSetting = "NIFT_HANDLE_LIMIT";

struct TableOptions {
    // Maximum number of slots (live + free + retired). Creating past it is
    // LimitExceeded (`limit.exceeded`, naming NIFT_HANDLE_LIMIT).
    std::uint32_t capacity = 1u << 20;
    // Non-empty: the rejected NIFT_HANDLE_LIMIT text; every create() then
    // fails with InvalidSetting (never silently replaced by the default).
    std::string invalid_setting;
};

struct TableStats {
    std::size_t live = 0, slots = 0, retired = 0, detached = 0, contexts = 0;
    std::size_t records = 0;              // lifecycles not yet settled or still attached to a live slot
    std::size_t pending_obligations = 0;  // closed handles whose resource is not reclaimed yet
    std::uint64_t created = 0, cleaned = 0, settled = 0;
};

template <class T> const void* type_tag_of() {
    static const char tag = 0;
    return &tag;
}

} // namespace nift::rt::handles
