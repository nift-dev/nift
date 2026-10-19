#pragma once
// The CP17 handle table: typed, generation-checked handles owned by execution
// contexts, with per-kind cleanup hooks, bounded slot reuse and a thread-safe
// interface for the Host layer (CP16) and natives. Design record:
// docs/runtime-2/checkpoints/cp17-typed-handles.md.
//
// Identity vs ownership. A handle Value only *identifies* a resource. Each
// live resource has exactly one owner (an execution context, or kDetached)
// and one cleanup policy (its kind's hook, run exactly once). Copies of the
// Value and counted references (HandleRef) never own anything. Ownership
// changes only by detach() or transfer(), each one locked step that names the
// expected current owner, so racing changes cannot both succeed.
//
// Concurrency. Every operation is linearizable under one mutex; cleanup hooks
// run without it. A resource is taken out of its slot (the handle becomes
// unresolvable) exactly once, by whichever of release / last drop / context
// close / shutdown gets there first, and only that caller runs the hook.
// close_context() returns only after every hook for a handle of the closed
// contexts has finished, including hooks started concurrently by other threads.
#include "rt/handle.h"
#include "rt/handles/context_tree.h"
#include "rt/handles/slot_store.h"
#include "rt/handles/types.h"

#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace nift::rt::handles {

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
    // Ends `id` and its descendants: descendants first (reverse open order),
    // each context's handles newest first, one hook call per handle with
    // reason ContextEnd; failing hooks never stop the cleanup of the rest.
    CloseReport close_context(ContextId id);
    // Whether `id` is a build-family context (Build, BuildHook, PageRender,
    // AutoRebuild, or nested in one), where detach() is DetachForbidden and
    // transfer() out of the build is TransferForbidden. Lets a native refuse
    // a detached launch before starting anything. Works on open and closing
    // contexts; ContextClosed / UnknownContext otherwise.
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
    // The owner of a live handle (kDetached when detached).
    HandleStatus owner_of(const Value& handle, HandleKind kind, ContextId& owner) const;

    // Ownership operations; each names the owner it expects (NotOwned otherwise).
    // release: the owner ends the resource now (reason Explicit). A counted
    // handle with other references is InUse; its creator's reference is
    // consumed. CleanupFailed (with `failure`) when the hook failed; the slot
    // is reclaimed either way.
    HandleStatus release(const Value& handle, HandleKind kind, ContextId expected_owner,
                         CleanupFailure* failure = nullptr);
    // detach: the context gives the resource up; nothing cleans it at context
    // end (shutdown() still releases the handle with reason Shutdown).
    HandleStatus detach(const Value& handle, HandleKind kind, ContextId expected_owner);
    // transfer: moves ownership to another open context in one step. Moving
    // out of the build family is TransferForbidden; adopting a detached
    // resource is TransferForbidden too.
    HandleStatus transfer(const Value& handle, HandleKind kind, ContextId expected_owner, ContextId new_owner);

    // Counted references (CreateOptions::counted). retain adds one; drop
    // removes one and cleans up (Abandoned) when it was the last. Hook
    // failures are queued for the owner's CloseReport (detached: shutdown()).
    HandleStatus retain(const Value& handle);
    HandleStatus drop(const Value& handle);

    // Closes every context still open (roots newest first), then releases
    // every detached handle (reason Shutdown). Idempotent.
    CloseReport shutdown();

    TableStats stats() const;
    const KindSpec* kind_spec(HandleKind kind) const;
    std::uint32_t tag() const { return store_.tag(); }
    bool set_free_generation_for_test(std::uint32_t index, std::uint32_t generation);

private:
    struct KindEntry { bool registered = false; KindSpec spec; };

    // resolve() with an optional type check (type_tag null: none).
    HandleStatus resolve_checked(const Value& handle, HandleKind kind, ContextId accessor, const void* type_tag,
                                 std::shared_ptr<void>& out) const;
    OwnerList* list_of(ContextId owner);
    // Takes a live slot (lock held), marks the hook in flight for its owner.
    Taken take_locked(std::uint32_t index);
    // Runs the hook (lock not held); returns false and fills `failure` on failure.
    bool run_hook(const Taken& taken, ReleaseReason reason, CleanupFailure& failure) const;
    // Ends the in-flight mark taken by take_locked (lock held).
    void finish_locked(ContextId owner);
    void close_one(ContextId id, CloseReport& report, std::unique_lock<std::mutex>& lock);

    mutable std::mutex mu_;
    std::condition_variable idle_;
    std::array<KindEntry, 256> kinds_;
    SlotStore store_;
    ContextTree contexts_;
    OwnerList detached_;
    std::size_t detached_in_flight_ = 0;
    std::vector<CleanupFailure> detached_failures_;
    std::uint64_t created_ = 0, cleaned_ = 0;
};

} // namespace nift::rt::handles
