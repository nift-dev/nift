#pragma once
// The CP17 handle table: typed, generation-checked handles owned by execution
// contexts; the authoritative resource lifecycle (D-035); per-kind cleanup
// hooks with an asynchronous completion API; bounded slot reuse; a
// thread-safe interface for the Host layer (CP16) and natives. Design record:
// docs/runtime-2/checkpoints/cp17-typed-handles.md.
//
// Identity vs ownership. A handle Value only *identifies* a resource. Each
// live resource has exactly one owner (an execution context, or kDetached)
// and one cleanup policy (its kind's hook). Copies of the Value and counted
// references (HandleRef) never own anything. Ownership changes only by
// detach() or transfer() (host-internal), each one locked step.
//
// Lifecycle. Closing a handle (close, last reference, context end, shutdown)
// and settling its resource (Completed / Failed / Lost, reported once through
// a CompletionTicket) are separate events. A closed handle is unusable at once
// (`runtime.handle_closed`); its context keeps an obligation until the
// resource settles, and close_context() returns only when every obligation of
// the closed contexts is settled, so its report is final.
//
// Concurrency. Every operation is linearizable under one mutex; cleanup hooks
// run without it and must not block. A slot is given up exactly once, by
// whichever of close / last drop / context close / shutdown gets there first;
// a lifecycle leaves Active exactly once and settles exactly once.
#include "rt/handle.h"
#include "rt/handles/completion.h"
#include "rt/handles/context_tree.h"
#include "rt/handles/lifecycle.h"
#include "rt/handles/slot_store.h"
#include "rt/handles/types.h"

#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace nift::rt::handles {

struct TerminationStart {
    bool started = false;                        // true: the caller got the ticket and must terminate
    LifeState state = LifeState::Active;         // lifecycle before the call
    TerminationCause cause = TerminationCause::None;  // what had started termination, if anything
};

class HandleTable {
public:
    explicit HandleTable(TableOptions options = {});
    ~HandleTable();  // shutdown(); failures are discarded (call shutdown() to see them)
    HandleTable(const HandleTable&) = delete;
    HandleTable& operator=(const HandleTable&) = delete;

    // Kinds: any HandleKind but None, registered once per table. Entries are
    // never changed afterwards, so hooks are read without the lock.
    HandleStatus register_kind(HandleKind kind, KindSpec spec);
    template <class T> HandleStatus register_typed_kind(HandleKind kind, KindSpec spec) {
        spec.type_tag = type_tag_of<T>();
        return register_kind(kind, std::move(spec));
    }

    // Contexts (D-029/D-030): Script, Build, BuildHook, PageRender, AutoRebuild.
    HandleStatus open_context(ContextKind kind, ContextId parent, ContextId& out);
    // Marks a context and its descendants Cancelling: no new handles,
    // children, detaches or inbound transfers; existing handles stay open.
    HandleStatus cancel_context(ContextId id);
    // Ends `id` and its descendants: descendants first (reverse open order),
    // each context's handles newest first, one hook call per handle with
    // reason ContextEnd; then waits until every obligation of these contexts
    // has settled (CP16 bounds that wait). The report lists every Failed/Lost
    // settle of their resources; it is final.
    CloseReport close_context(ContextId id);
    // Whether `id` is a build-family context, where detach() is
    // DetachForbidden: lets a native refuse a detached launch up front.
    HandleStatus context_in_build(ContextId id, bool& in_build) const;

    // Creates a live handle owned by `owner` (an open context).
    HandleStatus create(ContextId owner, HandleKind kind, std::shared_ptr<void> resource, Value& out,
                        CreateOptions options = {});
    // Resolves for `accessor` (the owner, a context nested in it, or anyone
    // for a detached handle; kAnyAccessor skips the check for host code).
    HandleStatus resolve(const Value& handle, HandleKind kind, ContextId accessor,
                         std::shared_ptr<void>& out) const;
    template <class T>
    HandleStatus resolve_as(const Value& handle, HandleKind kind, ContextId accessor, std::shared_ptr<T>& out) const {
        std::shared_ptr<void> raw;
        const HandleStatus status = resolve_checked(handle, kind, accessor, type_tag_of<T>(), raw);
        if (status == HandleStatus::Ok) out = std::static_pointer_cast<T>(std::move(raw));
        return status;
    }
    // The owner of an open handle (kDetached when detached).
    HandleStatus owner_of(const Value& handle, HandleKind kind, ContextId& owner) const;
    // The lifecycle of an open handle's resource.
    HandleStatus lifecycle(const Value& handle, HandleKind kind, LifeState& state, TerminationCause& cause) const;

    // close: script-facing, idempotent. Closing a closed handle is Ok; the
    // caller must be the owner or nested in it. Starts cleanup (reason
    // Explicit) and returns without waiting for the resource to settle.
    // CleanupFailed when the hook settled it Failed/Lost synchronously
    // (`failure` filled; it is also in the owner's CloseReport).
    HandleStatus close(const Value& handle, HandleKind kind, ContextId accessor, CleanupFailure* failure = nullptr);
    // release: host-facing, strict. As close() but the caller names the exact
    // owner (NotOwned otherwise) and a closed handle is Closed.
    HandleStatus release(const Value& handle, HandleKind kind, ContextId expected_owner,
                         CleanupFailure* failure = nullptr);
    // detach (host, strict): the named owner gives the resource up; nothing
    // cleans it at context end; shutdown() releases its bookkeeping.
    HandleStatus detach(const Value& handle, HandleKind kind, ContextId expected_owner);
    // detach_as (script-facing): the caller must be the owner or nested in it;
    // detaching a detached handle is Ok.
    HandleStatus detach_as(const Value& handle, HandleKind kind, ContextId accessor);
    // transfer (host-internal only, D-035; never exposed to natives): moves
    // ownership to another open context in one step. Out of the build family,
    // or adoption of a detached resource: TransferForbidden. A destination
    // that has ended or is cancelling: ContextClosed / UnknownContext, and
    // with ReclaimOnRefusal the resource is cleaned up at once.
    HandleStatus transfer(const Value& handle, HandleKind kind, ContextId expected_owner, ContextId new_owner,
                          TransferPolicy policy = TransferPolicy::KeepOnRefusal);

    // CP16 reports that a live resource is terminating for `cause` (Deadline,
    // Cancel, Exited) while its handle stays open. The first call while the
    // lifecycle is Active gets the ticket (info.started); later calls, and
    // calls after cleanup began, do not: termination happens exactly once.
    HandleStatus begin_termination(const Value& handle, HandleKind kind, TerminationCause cause,
                                   CompletionTicket& ticket, TerminationStart* info = nullptr);

    // Counted references (CreateOptions::counted). retain adds one; drop
    // removes one and, for the last, starts cleanup (reason Abandoned) without
    // blocking: the hook schedules termination and returns.
    HandleStatus retain(const Value& handle);
    HandleStatus drop(const Value& handle);

    // Closes every context still open (roots newest first), then releases
    // every detached handle (reason Shutdown), then waits for every
    // outstanding obligation. Idempotent.
    CloseReport shutdown();

    TableStats stats() const;
    const KindSpec* kind_spec(HandleKind kind) const;
    std::uint32_t capacity() const { return store_.capacity(); }
    const std::string& invalid_setting() const { return invalid_setting_; }
    std::uint32_t tag() const { return store_.tag(); }
    bool set_free_generation_for_test(std::uint32_t index, std::uint32_t generation);

private:
    friend class CompletionTicket;
    struct KindEntry { bool registered = false; KindSpec spec; };
    struct Started {   // a slot given up, with what its hook must be told
        Taken taken;
        ReleaseReason reason;
        LifeState state;
        TerminationCause cause;
        bool ticket;
    };

    HandleStatus resolve_checked(const Value& handle, HandleKind kind, ContextId accessor, const void* type_tag,
                                 std::shared_ptr<void>& out) const;
    OwnerList* list_of(ContextId owner);
    std::size_t* obligations_of(ContextId owner);
    std::vector<CleanupFailure>& failures_of(ContextId owner);
    // Gives up a live slot (lock held): lifecycle bookkeeping, in-flight mark.
    Started begin_cleanup_locked(std::uint32_t index, ReleaseReason reason);
    // Runs the hook (lock released around it) and settles a synchronous
    // outcome. Returns true and fills `failure` when it settled Failed/Lost.
    bool run_cleanup(Started& started, std::unique_lock<std::mutex>& lock, CleanupFailure* failure);
    void finish_locked(ContextId owner);
    HandleStatus close_locked(std::uint32_t index, std::unique_lock<std::mutex>& lock, CleanupFailure* failure);
    void close_one(ContextId id, CloseReport& report, std::unique_lock<std::mutex>& lock);
    // Settles a lifecycle (CompletionTicket::complete). Exactly once.
    bool settle(std::uint64_t record, CleanupOutcome&& outcome);

    mutable std::mutex mu_;
    std::condition_variable idle_;
    std::array<KindEntry, 256> kinds_;
    SlotStore store_;
    ContextTree contexts_;
    LifecycleStore records_;
    std::string invalid_setting_;
    OwnerList detached_;
    std::size_t detached_in_flight_ = 0, detached_obligations_ = 0, obligations_ = 0;
    std::vector<CleanupFailure> detached_failures_;
    std::uint64_t created_ = 0, cleaned_ = 0, settled_ = 0;
};

} // namespace nift::rt::handles
