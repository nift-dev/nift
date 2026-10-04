#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nift::detail {

enum class DiagnosticDisposition { Fatal, Recoverable };

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
        NIFT_DIAGNOSTIC_CASE(NativeSyntaxError, "native.syntax_error", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeTranslationError, "native.translation_error", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeNameError, "native.name_error", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeInvalidArgument, "native.invalid_argument", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeInvalidOperation, "native.invalid_operation", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeInvalidControl, "native.invalid_control", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeMutationDenied, "native.mutation_denied", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeArithmeticError, "native.arithmetic_error", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeLimitExceeded, "native.limit_exceeded", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeInvalidHandle, "native.invalid_handle", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeResourceLifecycle, "native.resource_lifecycle", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeResourceOwnerMismatch, "native.resource_owner_mismatch", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeResourceTransferDenied, "native.resource_transfer_denied", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeUnsupportedValue, "native.unsupported_value", Fatal);
        NIFT_DIAGNOSTIC_CASE(NativeSystemServiceFailed, "native.system_service_failed", Fatal);
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
        NIFT_DIAGNOSTIC_CASE(InternalResourceExhausted, "internal.resource_exhausted", Fatal);
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
    std::string message;
    DiagnosticOrigin origin;
    std::vector<DiagnosticFrame> frames;
};

inline Diagnostic make_diagnostic(DiagnosticCode code, std::string message,
                                  DiagnosticOrigin origin = {}) {
    return {code, std::move(message), std::move(origin), {}};
}

inline std::string project_diagnostic(const Diagnostic& diagnostic) {
    std::string projected;
    for (auto frame = diagnostic.frames.rbegin(); frame != diagnostic.frames.rend(); ++frame)
        projected += frame->compatibility_prefix;
    projected += diagnostic.message;
    return projected;
}

}  // namespace nift::detail
