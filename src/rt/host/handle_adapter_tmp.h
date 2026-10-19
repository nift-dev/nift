#pragma once
// ===========================================================================
// TEMPORARY SCAFFOLDING - DELETE WHEN CP17's HANDLE TABLE IS INTEGRATED.
// ===========================================================================
// CP16 must use CP17's real handle representation (owner, 2026-10-19).
// CP17's interface (src/rt/handles/, branch runtime-2-cp17-dev @11e98d2) is
// not on runtime-2 yet, so the process hook is exercised through this thin
// stand-in that copies CP17's vocabulary exactly (ReleaseReason values,
// CleanupEvent fields, CleanupOutcome, KindSpec.cleanup, open/close context,
// create/release/detach, counted references reduced to "drop"). The swap is
// mechanical: replace `tmp_handles::` with `handles::`, `TemporaryTable` with
// `HandleTable`, and delete these two files.
//
// Ownership and cleanup POLICY belong to CP17's table. The host layer only
// registers its per-kind hook (process_cleanup.h); it keeps no ownership
// mechanism of its own. No production code path may depend on this file.
// ===========================================================================
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nift::rt::host::tmp_handles {

using ContextId = std::uint64_t;
inline constexpr ContextId kDetached = 0;
using HandleId = std::uint64_t;  // stands in for CP17's rt::Value handle

enum class ContextKind : std::uint8_t { Script, PageRender, BuildHook, Build, AutoRebuild };
enum class ReleaseReason : std::uint8_t { Explicit, Abandoned, ContextEnd, Shutdown };

struct CleanupEvent {
    HandleId handle;
    ContextId owner;
    ReleaseReason reason;
    const std::shared_ptr<void>& resource;
};

struct CleanupOutcome {
    bool ok = true;
    std::string code;
    std::string message;
    static CleanupOutcome success() { return {}; }
    static CleanupOutcome failure(std::string code, std::string message) { return CleanupOutcome{false, std::move(code), std::move(message)}; }
};

using CleanupHook = std::function<CleanupOutcome(const CleanupEvent&)>;

// TEMPORARY test-only stand-in for handles::HandleTable (one kind: Process).
class TemporaryTable {
public:
    explicit TemporaryTable(CleanupHook process_hook) : hook_(std::move(process_hook)) {}
    ~TemporaryTable() { shutdown(); }
    ContextId open_context(ContextKind kind, ContextId parent);
    bool build_family(ContextId id) const;
    HandleId create(ContextId owner, std::shared_ptr<void> resource);
    std::shared_ptr<void> resolve(HandleId id) const;   // null: stale/unknown
    bool release(HandleId id, ContextId expected_owner); // Explicit
    bool drop(HandleId id);                             // last reference: Abandoned
    bool detach(HandleId id, ContextId expected_owner);  // refused in the build family
    std::vector<CleanupOutcome> close_context(ContextId id);  // newest first, ContextEnd
    void shutdown();                                    // detached handles: Shutdown

private:
    struct Entry { HandleId id; ContextId owner; std::shared_ptr<void> resource; };
    struct Context { ContextId id; ContextKind kind; ContextId parent; bool open; };
    CleanupOutcome run(Entry entry, ReleaseReason reason);
    CleanupHook hook_;
    std::vector<Entry> entries_;
    std::vector<Context> contexts_;
    HandleId next_handle_ = 1;
    ContextId next_context_ = 1;
};

} // namespace nift::rt::host::tmp_handles
