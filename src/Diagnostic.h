#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nift::detail {

// Identity (DiagnosticCode), severity and catchability are separate
// properties (D-022). `DiagnosticDisposition` is only the *default*
// catchability a raise through fail_recoverable/fail_fatal gets; whether a
// particular raise was catchable is recorded on the Diagnostic itself
// (`catchable`). Adding or renaming a code never changes what a program can
// catch. Codes describe what went wrong, not which component detected it
// (D-025; taxonomy in docs/runtime-2/checkpoints/cp13-diagnostics-design.md).
enum class DiagnosticDisposition { Fatal, Recoverable };

enum class DiagnosticSeverity { Error, Warning, Note };

enum class DiagnosticCode {
    NativeSyntaxError,
    NativeTranslationError,
    NativeNameError,
    NativeInvalidArgument,
    NativeInvalidOperation,
    NativeInvalidControl,
    NativeMutationDenied,
    NativeArithmeticError,
    NativeLimitExceeded,
    NativeInvalidHandle,
    NativeResourceLifecycle,
    NativeResourceOwnerMismatch,
    NativeResourceTransferDenied,
    NativeUnsupportedValue,
    NativeSystemServiceFailed,
    PolicyRestrictedOperation,
    PolicyAuthorityDenied,
    PolicyPathEscape,
    PolicyPrivacyViolation,
    JsonConfigurationInvalid,
    SchemaDefinitionInvalid,
    PackageManifestInvalid,
    PackageLockInvalid,
    PackageProvenanceMismatch,
    PackageTransactionRecoveryRequired,
    ProcessDirectCommandFailed,
    FfiInvalidSignature,
    FfiInvalidArgument,
    FfiInvalidHandle,
    FfiCallbackFailed,
    HostProviderError,
    InternalHostException,
    InternalUnexpectedException,
    InternalInvariantViolation,
    InternalImportLifecycle,
    InternalIdentityExhausted,
    InternalResourceExhausted,
    InternalLegacyFailure,
    UserRaised,
    IoOpenFailed,
    IoReadFailed,
    IoCreateFailed,
    IoWriteFailed,
    IoCopyFailed,
    IoMoveFailed,
    IoRemoveFailed,
    IoDirectoryReadFailed,
    IoChangeDirectoryFailed,
    IoMetadataFailed,
    IoAtomicReplaceFailed,
    IoImportSourceUnreadable,
    StreamOpenFailed,
    StreamReadFailed,
    StreamWriteFailed,
    StreamFlushFailed,
    StreamCloseFailed,
    JsonParseFailed,
    SchemaRejected,
    PackageNotInstalled,
    PackageImportSourceUnreadable,
    FfiLibraryLoadFailed,
    FfiSymbolNotFound,
    // CP13 (D-022 #2, D-025): specific language run-time codes, replacing
    // internal.legacy_failure for ordinary run-time errors. Appended; all
    // default to Fatal, exactly as these errors are today.
    RuntimeUndefinedCallable,
    RuntimeTypeError,
    RuntimeIndexOutOfBounds,
    RuntimeMemberNotFound,
    RuntimeArityError,
    RuntimeIntegerOverflow,
    RuntimeDivisionByZero,
    // CP17 (D-035): typed-handle codes. Appended; all Fatal except
    // runtime.handle_closed, which is catchable by owner decision.
    RuntimeHandleClosed,
    InternalHandleNotAHandle,
    InternalHandleKindMismatch,
    InternalHandleForeign,
    InternalHandleForged,
    InternalHandleTypeMismatch,
    InternalHandleNotOwned,
    InternalHandleNotCounted,
    InternalHandleContextClosed,
    InternalHandleUnknownContext,
    InternalHandleUnknownKind,
    InternalHandleKindRegistered,
    InternalHandleNullResource,
    InternalHandleCleanupFailed,
    InternalHandleCleanupThrew,
    InternalHandleCleanupUnresolved,
    InternalHandleCleanupLost,
    InternalHandleCleanupIncomplete,
    RuntimeDetachInBuild,
    ProcessDetachInBuild,
    RuntimeDetachFailed,
    RuntimeTransferOutOfBuild,
    RuntimeCleanupFailed,
    InternalHandleInvalidCause,
    InternalHandleReentrantClose,
    Count,
};

struct DiagnosticCodeInfo {
    std::string_view text;
    DiagnosticDisposition disposition;
};

inline DiagnosticCodeInfo diagnostic_code_info(DiagnosticCode code) {
    using D = DiagnosticDisposition;
    switch (code) {
#define NIFT_DIAGNOSTIC_CASE(name, text, disposition) \
        case DiagnosticCode::name: return {text, D::disposition}
        NIFT_DIAGNOSTIC_CASE(NativeSyntaxError, "syntax.malformed_expression", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeTranslationError, "syntax.invalid_statement", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeNameError, "runtime.undefined_name", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeInvalidArgument, "runtime.invalid_argument", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeInvalidOperation, "runtime.invalid_operation", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeInvalidControl, "runtime.invalid_control", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeMutationDenied, "runtime.mutation_denied", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeArithmeticError, "runtime.arithmetic_error", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeLimitExceeded, "limit.exceeded", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeInvalidHandle, "runtime.invalid_handle", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeResourceLifecycle, "runtime.resource_lifecycle", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeResourceOwnerMismatch, "runtime.resource_owner_mismatch", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeResourceTransferDenied, "runtime.resource_transfer_denied", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeUnsupportedValue, "runtime.unsupported_value", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeSystemServiceFailed, "host.service_failed", Fatal);
        NIFT_DIAGNOSTIC_CASE(PolicyRestrictedOperation, "policy.restricted_operation", Fatal);
        NIFT_DIAGNOSTIC_CASE(PolicyAuthorityDenied, "policy.authority_denied", Fatal);
        NIFT_DIAGNOSTIC_CASE(PolicyPathEscape, "policy.path_escape", Fatal);
        NIFT_DIAGNOSTIC_CASE(PolicyPrivacyViolation, "policy.privacy_violation", Fatal);
        NIFT_DIAGNOSTIC_CASE(JsonConfigurationInvalid, "json.configuration_invalid", Fatal);
        NIFT_DIAGNOSTIC_CASE(SchemaDefinitionInvalid, "schema.definition_invalid", Fatal);
        NIFT_DIAGNOSTIC_CASE(PackageManifestInvalid, "package.manifest_invalid", Fatal);
        NIFT_DIAGNOSTIC_CASE(PackageLockInvalid, "package.lock_invalid", Fatal);
        NIFT_DIAGNOSTIC_CASE(PackageProvenanceMismatch, "package.provenance_mismatch", Fatal);
        NIFT_DIAGNOSTIC_CASE(PackageTransactionRecoveryRequired, "package.transaction_recovery_required", Fatal);
        NIFT_DIAGNOSTIC_CASE(ProcessDirectCommandFailed, "process.direct_command_failed", Fatal);
        NIFT_DIAGNOSTIC_CASE(FfiInvalidSignature, "ffi.invalid_signature", Fatal);
        NIFT_DIAGNOSTIC_CASE(FfiInvalidArgument, "ffi.invalid_argument", Fatal);
        NIFT_DIAGNOSTIC_CASE(FfiInvalidHandle, "ffi.invalid_handle", Fatal);
        NIFT_DIAGNOSTIC_CASE(FfiCallbackFailed, "ffi.callback_failed", Fatal);
        NIFT_DIAGNOSTIC_CASE(HostProviderError, "host.provider_error", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHostException, "internal.host_exception", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalUnexpectedException, "internal.unexpected_exception", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalInvariantViolation, "internal.invariant_violation", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalImportLifecycle, "internal.import_lifecycle", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalIdentityExhausted, "internal.identity_exhausted", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalResourceExhausted, "limit.resource_exhausted", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalLegacyFailure, "internal.legacy_failure", Fatal);
        NIFT_DIAGNOSTIC_CASE(UserRaised, "user.raised", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoOpenFailed, "io.open_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoReadFailed, "io.read_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoCreateFailed, "io.create_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoWriteFailed, "io.write_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoCopyFailed, "io.copy_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoMoveFailed, "io.move_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoRemoveFailed, "io.remove_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoDirectoryReadFailed, "io.directory_read_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoChangeDirectoryFailed, "io.change_directory_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoMetadataFailed, "io.metadata_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoAtomicReplaceFailed, "io.atomic_replace_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(IoImportSourceUnreadable, "io.import_source_unreadable", Recoverable);
        NIFT_DIAGNOSTIC_CASE(StreamOpenFailed, "stream.open_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(StreamReadFailed, "stream.read_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(StreamWriteFailed, "stream.write_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(StreamFlushFailed, "stream.flush_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(StreamCloseFailed, "stream.close_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(JsonParseFailed, "json.parse_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(SchemaRejected, "schema.rejected", Recoverable);
        NIFT_DIAGNOSTIC_CASE(PackageNotInstalled, "package.not_installed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(PackageImportSourceUnreadable, "package.import_source_unreadable", Recoverable);
        NIFT_DIAGNOSTIC_CASE(FfiLibraryLoadFailed, "ffi.library_load_failed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(FfiSymbolNotFound, "ffi.symbol_not_found", Recoverable);
        NIFT_DIAGNOSTIC_CASE(RuntimeUndefinedCallable, "runtime.undefined_callable", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeTypeError, "runtime.type_error", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeIndexOutOfBounds, "runtime.index_out_of_bounds", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeMemberNotFound, "runtime.member_not_found", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeArityError, "runtime.arity_error", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeIntegerOverflow, "runtime.integer_overflow", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeDivisionByZero, "runtime.division_by_zero", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeHandleClosed, "runtime.handle_closed", Recoverable);
        NIFT_DIAGNOSTIC_CASE(InternalHandleNotAHandle, "internal.handle_not_a_handle", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleKindMismatch, "internal.handle_kind_mismatch", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleForeign, "internal.handle_foreign", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleForged, "internal.handle_forged", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleTypeMismatch, "internal.handle_type_mismatch", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleNotOwned, "internal.handle_not_owned", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleNotCounted, "internal.handle_not_counted", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleContextClosed, "internal.handle_context_closed", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleUnknownContext, "internal.handle_unknown_context", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleUnknownKind, "internal.handle_unknown_kind", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleKindRegistered, "internal.handle_kind_registered", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleNullResource, "internal.handle_null_resource", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleCleanupFailed, "internal.handle_cleanup_failed", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleCleanupThrew, "internal.handle_cleanup_threw", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleCleanupUnresolved, "internal.handle_cleanup_unresolved", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleCleanupLost, "internal.handle_cleanup_lost", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleCleanupIncomplete, "internal.handle_cleanup_incomplete", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeDetachInBuild, "runtime.detach_in_build", Fatal);
        NIFT_DIAGNOSTIC_CASE(ProcessDetachInBuild, "process.detach_in_build", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeDetachFailed, "runtime.detach_failed", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeTransferOutOfBuild, "runtime.transfer_out_of_build", Fatal);
        NIFT_DIAGNOSTIC_CASE(RuntimeCleanupFailed, "runtime.cleanup_failed", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleInvalidCause, "internal.handle_invalid_cause", Fatal);
        NIFT_DIAGNOSTIC_CASE(InternalHandleReentrantClose, "internal.handle_reentrant_close", Fatal);
        case DiagnosticCode::Count: break;
#undef NIFT_DIAGNOSTIC_CASE
    }
    return {"internal.invariant_violation", D::Fatal};
}

struct DiagnosticOrigin {
    std::filesystem::path source;
    std::size_t line = 0;
    std::size_t column = 0;
    std::size_t source_length = 1;
    std::string source_line;
};

enum class DiagnosticFrameKind {
    Callable,
    Method,
    Lambda,
    Callback,
    Import,
    Template,
    Script,
    Future,
    Thread,
    Repl,
};

struct DiagnosticFrame {
    DiagnosticFrameKind kind = DiagnosticFrameKind::Script;
    std::string label;
    DiagnosticOrigin site;
    std::string compatibility_prefix;
};

struct Diagnostic {
    DiagnosticCode code = DiagnosticCode::InternalLegacyFailure;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    // Whether this raise could be caught by `try` (fail_recoverable -> true,
    // fail_fatal / Parser::fail -> false; the VM's catch rule). Never derived
    // from `code` alone.
    bool catchable = false;
    std::string message;
    DiagnosticOrigin origin;
    std::vector<DiagnosticFrame> frames;
};

inline Diagnostic make_diagnostic(DiagnosticCode code, std::string message,
                                  DiagnosticOrigin origin = {}) {
    return {code, DiagnosticSeverity::Error,
            diagnostic_code_info(code).disposition == DiagnosticDisposition::Recoverable,
            std::move(message), std::move(origin), {}};
}

inline std::string project_diagnostic(const Diagnostic& diagnostic) {
    std::string projected;
    for (auto frame = diagnostic.frames.rbegin(); frame != diagnostic.frames.rend(); ++frame)
        projected += frame->compatibility_prefix;
    projected += diagnostic.message;
    return projected;
}

}  // namespace nift::detail
