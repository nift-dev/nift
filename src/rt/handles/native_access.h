#pragma once
// Native-side handle interface: how a built-in receives handle arguments and
// returns new handles, over rt::Value only (no RuntimeValue, no strings).
//
// CP16 adds a `handles::HandleAccess handles;` member to natives::Context.
// Status mapping for natives (handle_error_class, D-035):
//   Recoverable -> a catchable error with the code (`runtime.handle_closed`)
//   Internal    -> natives::Status::Internal (exit 70: the compiler admits handle
//                  values only where they cannot be forged, so this is a defect)
//   Limit       -> the Limit status CP16 adds (limit.exceeded, names NIFT_HANDLE_LIMIT)
//   Error       -> natives::Status::Error (fatal program error with the code)
// Scripts can close and detach handles; ownership transfer is host-internal
// and deliberately has no native entry point (D-035).
#include "rt/handles/table.h"

#include <string>
#include <string_view>

namespace nift::rt::handles {

struct HandleAccess {
    HandleTable* table = nullptr;
    ContextId context = kDetached;   // the execution context the native runs in
};

// "<builtin>: <reason> (<kind> handle <slot>/<generation>, <code>)".
void describe_handle_failure(HandleStatus status, std::string_view builtin, HandleKind kind, const Value& handle,
                             const HandleTable* table, std::string& error);

// Resolves argument `arg` as a `kind` handle whose resource is a T, for the
// native's context. On failure `error` is set and `out` left unchanged.
template <class T>
HandleStatus native_handle_arg(const HandleAccess& access, std::string_view builtin, const Value& arg,
                               HandleKind kind, std::shared_ptr<T>& out, std::string& error) {
    if (!access.table) {
        describe_handle_failure(HandleStatus::UnknownContext, builtin, kind, arg, nullptr, error);
        return HandleStatus::UnknownContext;
    }
    const HandleStatus status = access.table->resolve_as<T>(arg, kind, access.context, out);
    if (status != HandleStatus::Ok) describe_handle_failure(status, builtin, kind, arg, access.table, error);
    return status;
}

// close(h): idempotent (closing a closed handle is Ok). CleanupFailed when the
// resource settled Failed, Lost or CleanupIncomplete synchronously: `error`
// then carries the failure's own code and message, `failure` (optional) the
// details; class Error (D-035: exit 1). It is also in the context's report.
HandleStatus native_handle_close(const HandleAccess& access, std::string_view builtin, const Value& arg,
                                 HandleKind kind, std::string& error, CleanupFailure* failure = nullptr);
// detach(h): refused inside builds with the kind's code.
HandleStatus native_handle_detach(const HandleAccess& access, std::string_view builtin, const Value& arg,
                                  HandleKind kind, std::string& error);

// Creates a handle for `resource`, owned by the native's context, into `out`.
HandleStatus native_handle_result(const HandleAccess& access, std::string_view builtin, HandleKind kind,
                                  std::shared_ptr<void> resource, Value& out, std::string& error,
                                  CreateOptions options = {});

} // namespace nift::rt::handles
