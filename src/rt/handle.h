#pragma once
// Typed handles: the CP09 single-kind registry plus the status vocabulary
// shared with the CP17 handle table (rt/handles/table.h).
//
// A handle Value is (HandleKind, id, generation). It is never a string, so
// data can never forge one (removes legacy defect D3). A registry maps ids to
// resources; releasing a slot bumps its generation, so a stale handle fails to
// resolve instead of reaching a different resource. A slot whose generation
// has reached UINT32_MAX is retired on release (never reused), so generations
// never wrap and a stale handle can never match a later resource.
//
// HandleRegistry is the minimal single-threaded, owner-less building block
// kept for CP09's tests and simple single-isolate uses. Host resources with an
// execution-context owner and cleanup hooks (files, streams, processes) use
// rt::handles::HandleTable (CP17).
#include "rt/value.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace nift::rt {

// Why a handle operation failed. Every non-Ok status has one specific error
// code (handle_status_code) and an error class saying how the VM reports it.
enum class HandleStatus : std::uint8_t {
    Ok,
    NotAHandle,         // the value is not a handle at all
    KindMismatch,       // a handle of another kind than the operation expects
    Foreign,            // issued by another table (another run or isolate)
    Forged,             // never issued: unknown slot, or a generation not yet issued
    Closed,             // issued once, closed or reclaimed since (D-035: valid-but-closed,
                        // never misread as forged; the slot may hold a newer resource)
    TypeMismatch,       // resolve_as<T> with a T other than the kind's registered type
    NotOwned,           // the caller is not the owner the operation requires
    NotCounted,         // retain/drop on a handle created without reference counting
    ContextClosed,      // the context has ended or is ending
    UnknownContext,     // the context id was never issued by this table
    UnknownKind,        // the kind has not been registered
    KindRegistered,     // the kind is already registered
    NullResource,       // create() with a null resource
    DetachForbidden,    // detach inside a build context (D-030)
    DetachFailed,       // detachment could not be established (D-039): Nift already
                        // started terminating the resource, the context is ending or
                        // cancelling, or the caller does not own it (a reportable
                        // operation failure, never Internal)
    TransferForbidden,  // a transfer that would move a resource out of build ownership
    LimitExceeded,      // the table's capacity (NIFT_HANDLE_LIMIT) is reached (D-020/D-028: loud)
    InvalidSetting,     // NIFT_HANDLE_LIMIT has an invalid value (reported, never replaced)
    CleanupFailed,      // close/release: the resource settled Failed, Lost or
                        // CleanupIncomplete synchronously; the CleanupFailure carries
                        // the specific code (D-035: exit 1); the slot is reclaimed anyway
    InvalidCause,       // begin_termination with a cause that is not Exited/Deadline/Cancel
    ReentrantClose,     // close_context/shutdown from inside a cleanup hook of this table
};

// How a failure surfaces (D-035): Recoverable = a catchable program error
// (`runtime.handle_closed`: the handle was valid and has been closed or
// reclaimed); Internal = a VM/compiler/host invariant broke (forged, foreign or
// wrong-kind handles: the compiler admits handle values only where they cannot
// be forged; exit 70, "never guessed"); Limit = resource exhaustion
// (`limit.exceeded`, never an ordinary exception); Error = a fatal program
// error with a `runtime.*` code.
enum class HandleErrorClass : std::uint8_t { None, Recoverable, Internal, Limit, Error };

const char* handle_status_code(HandleStatus status);   // e.g. "runtime.handle_closed"
const char* handle_status_text(HandleStatus status);   // e.g. "handle is closed"
HandleErrorClass handle_error_class(HandleStatus status);

class HandleRegistry {
public:
    explicit HandleRegistry(HandleKind kind) : kind_(kind) {}
    static constexpr std::uint32_t kMaxGeneration = 0xFFFFFFFFu;
    // Stores `resource` and returns its handle. A null resource is rejected:
    // the result is Null and no slot is used.
    Value add(std::shared_ptr<void> resource);
    // Returns the resource, or null for a closed, foreign or forged handle.
    std::shared_ptr<void> resolve(const Value& handle) const;
    // Classifies `handle` the way resolve() treats it, with a specific reason
    // (NotAHandle, KindMismatch, Forged, Closed or Ok).
    HandleStatus check(const Value& handle) const;
    // Releases the slot; later resolves of the same handle fail.
    bool release(const Value& handle);
    std::size_t live() const { return live_; }
    std::size_t retired() const { return retired_; }
    // Test hook: sets a free slot's generation so the retirement path can be
    // tested without 2^32 add/release cycles. Returns false for a live or
    // unknown slot.
    bool set_free_slot_generation_for_test(std::uint64_t id, std::uint32_t generation);

private:
    struct Slot { std::shared_ptr<void> resource; std::uint32_t generation = 0; bool retired = false; };
    HandleKind kind_;
    std::vector<Slot> slots_;
    std::vector<std::uint64_t> free_;
    std::size_t live_ = 0;
    std::size_t retired_ = 0;
};

} // namespace nift::rt
