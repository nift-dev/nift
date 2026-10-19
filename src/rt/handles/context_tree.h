#pragma once
// Execution-context bookkeeping of the CP17 handle table: the context tree,
// each context's owned-handle list and lifecycle state, ownership access
// rules and the deterministic close order. Not thread-safe: HandleTable calls
// it with its lock held.
#include "rt/handles/slot_store.h"
#include "rt/handles/types.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace nift::rt::handles {

// Open: normal. Cancelling: cancel_context() was called (a superseded --auto
// pass, a cancelled worker): no new handles, children, detaches or inbound
// transfers; existing handles stay until close. Closing: close in progress.
enum class ContextState : std::uint8_t { Open, Cancelling, Closing };

struct ExecContext {
    ContextKind kind = ContextKind::Script;
    ContextId parent = kDetached;      // kDetached = root
    ContextState state = ContextState::Open;
    bool build_family = false;         // a build context or nested in one: no detach
    std::vector<ContextId> children;   // in open order
    OwnerList handles;                 // owned handles in creation order
    std::size_t in_flight = 0;         // cleanup hooks running for handles taken from this context
    std::size_t obligations = 0;       // closed handles whose resources are not settled yet
    std::vector<CleanupFailure> failures;  // Failed/Lost settles, reported at close
};

class ContextTree {
public:
    // Opens a context nested in `parent` (kDetached for a root).
    HandleStatus open(ContextKind kind, ContextId parent, ContextId& out);
    // The open or closing context `id`, or null (status says why).
    ExecContext* find(ContextId id, HandleStatus& status);
    ExecContext* find(ContextId id) { HandleStatus ignored; return find(id, ignored); }
    const ExecContext* find(ContextId id, HandleStatus& status) const {
        return const_cast<ContextTree*>(this)->find(id, status);
    }
    // Ok when `accessor` is `owner` or nested in it, or the handle is
    // detached, or the accessor is kAnyAccessor. Otherwise NotOwned.
    HandleStatus may_access(ContextId accessor, ContextId owner) const;
    // Marks `id` and every open descendant Closing and returns the order in
    // which they are cleaned: descendants before ancestors, later-opened
    // siblings first (reverse of open order, like destructors).
    std::vector<ContextId> begin_close(ContextId id);
    // Marks `id` and every open descendant Cancelling. ContextClosed when
    // `id` is already closing.
    HandleStatus cancel(ContextId id);
    // Forgets a fully cleaned context (detaching it from its parent).
    void erase(ContextId id);
    std::vector<ContextId> roots() const { return roots_; }
    std::size_t size() const { return contexts_.size(); }

    static bool is_build_kind(ContextKind kind) { return kind != ContextKind::Script; }

private:
    void collect(ContextId id, std::vector<ContextId>& order);

    std::unordered_map<ContextId, ExecContext> contexts_;
    std::vector<ContextId> roots_;   // in open order
    ContextId next_ = 1;             // ids are never reused
};

} // namespace nift::rt::handles
