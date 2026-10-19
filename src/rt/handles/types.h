#pragma once
// Vocabulary of the CP17 handle table: execution contexts, release reasons,
// per-kind specs with cleanup hooks, and cleanup reports. No RuntimeValue.
#include "rt/handle.h"
#include "rt/value.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nift::rt::handles {

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

// Why a handle's resource is being cleaned up. A kind's hook sees exactly one
// event per handle, whatever the reason.
enum class ReleaseReason : std::uint8_t {
    Explicit,    // release() by the owner
    Abandoned,   // the last counted reference was dropped
    ContextEnd,  // the owning context ended with the handle still owned
    Shutdown,    // the table shut down (detached handles; contexts left open)
};
const char* release_reason_name(ReleaseReason reason);

struct CleanupEvent {
    HandleKind kind;
    Value handle;                             // the (already unresolvable) handle
    ContextId owner;                          // kDetached for detached handles
    ReleaseReason reason;
    const std::shared_ptr<void>& resource;    // the hook may copy it to finish asynchronously
};

struct CleanupOutcome {
    bool ok = true;
    std::string code;     // specific code; empty -> internal.handle_cleanup_failed
    std::string message;
    static CleanupOutcome success() { return {}; }
    static CleanupOutcome failure(std::string code, std::string message) {
        return CleanupOutcome{false, std::move(code), std::move(message)};
    }
};

// Runs without the table lock held. It may resolve, create (in an open
// context), release or drop other handles, but must not close the context
// that owns the handle being cleaned (that close is waiting for this hook).
// An exception escaping a hook is caught and reported as a cleanup failure.
using CleanupHook = std::function<CleanupOutcome(const CleanupEvent&)>;

struct KindSpec {
    std::string name;                 // "file", "stream", "process" ... (diagnostics only)
    CleanupHook cleanup;              // empty: the resource is just dropped
    // Code reported for DetachForbidden on this kind (CP16 registers
    // "process.detach_in_build"); empty: "runtime.detach_in_build".
    std::string detach_forbidden_code;
    const void* type_tag = nullptr;   // set by register_typed_kind<T>
};

// A failed cleanup, reported loudly: from release(), in the owner's
// CloseReport (context end and abandoned handles), or by shutdown().
struct CleanupFailure {
    Value handle;
    HandleKind kind = HandleKind::None;
    ContextId owner = kDetached;
    ReleaseReason reason = ReleaseReason::Explicit;
    std::string code;
    std::string message;
};

struct CloseReport {
    HandleStatus status = HandleStatus::Ok;   // Ok, ContextClosed or UnknownContext
    std::size_t cleaned = 0;                  // handles cleaned by this close
    std::size_t contexts = 0;                 // contexts ended (the context and its descendants)
    std::vector<CleanupFailure> failures;     // abandoned-handle failures first, then cleanup order
};

struct CreateOptions {
    // Counted handles carry a reference count starting at 1 (the creator's
    // reference); dropping the last one cleans the resource up (Abandoned).
    // Uncounted handles live until released by their owner or their owning
    // context ends; references to them are plain Value copies.
    bool counted = false;
};

struct TableOptions {
    // Maximum number of slots (live + free + retired). Creating past it is
    // LimitExceeded (`limit.exceeded`). At most 2^32 - 1.
    std::uint32_t capacity = 1u << 20;
};

struct TableStats {
    std::size_t live = 0, slots = 0, retired = 0, detached = 0, contexts = 0;
    std::uint64_t created = 0, cleaned = 0;
};

template <class T> const void* type_tag_of() {
    static const char tag = 0;
    return &tag;
}

} // namespace nift::rt::handles
