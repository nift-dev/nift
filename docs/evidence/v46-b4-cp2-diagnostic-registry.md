# v4.6 Batch 4 CP2 diagnostic registry

This document pins the v1 built-in diagnostic codes. Canonical strings are the
stable identity; C++ enum ordinals are internal and non-contractual. Message
text, platform error text, and compatibility prefixes are not code identity.

`internal.legacy_failure` is a CP2 migration sentinel. It is fatal, is not a
stable language-facing code, and must have no converted producer before CP4.

## Recoverable families

| Codes | Eligible branch |
| --- | --- |
| `user.raised` | Default code for an explicit user Error; custom validated `user.*` codes are represented separately in CP3. |
| `io.open_failed`, `io.read_failed`, `io.create_failed`, `io.write_failed` | Backend failure after path, authority, mode, type, and lifecycle validation. |
| `io.copy_failed`, `io.move_failed`, `io.remove_failed`, `io.directory_read_failed`, `io.change_directory_failed` | Backend operation failure after argument and policy validation. |
| `io.atomic_replace_failed` | Final atomic save replacement only; temporary-file write remains `io.write_failed`. |
| `io.import_source_unreadable` | Valid ordinary import whose source cannot be read. |
| `stream.open_failed`, `stream.read_failed`, `stream.write_failed`, `stream.flush_failed`, `stream.close_failed` | Backend failure on a valid stream operation; forged/closed handles remain fatal. |
| `json.parse_failed` | Malformed runtime JSON after successful source acquisition. |
| `schema.rejected` | Valid schema rejects a candidate value. |
| `package.not_installed` | Package directory or manifest is absent, not malformed. |
| `package.import_source_unreadable` | Installed package metadata is valid but its selected source is unreadable. |
| `ffi.library_load_failed`, `ffi.symbol_not_found` | Platform loader failure after argument, path, and handle validation. |

## Fatal families

| Codes | Category boundary |
| --- | --- |
| `native.syntax_error`, `native.translation_error`, `native.name_error` | Grammar, translation, and binding resolution. |
| `native.invalid_argument`, `native.invalid_operation`, `native.invalid_control` | Arity/type/value, invalid operation state, and invalid language control flow. |
| `native.mutation_denied`, `native.arithmetic_error`, `native.limit_exceeded` | Const/privacy mutation, arithmetic domain/overflow, and explicit limits. |
| `native.invalid_handle`, `native.resource_lifecycle`, `native.resource_owner_mismatch`, `native.resource_transfer_denied` | Forged handles, invalid lifecycle, wrong owner, and prohibited transfer. |
| `native.unsupported_value`, `native.system_service_failed` | Unsupported value projection and non-I/O native service failure. |
| `policy.restricted_operation`, `policy.authority_denied`, `policy.path_escape`, `policy.privacy_violation` | Nift policy rejection; never inferred from an OS error string. |
| `json.configuration_invalid`, `schema.definition_invalid` | Project/configuration JSON and malformed schema definitions. |
| `package.manifest_invalid`, `package.lock_invalid`, `package.provenance_mismatch`, `package.transaction_recovery_required` | Installed metadata, lock, provenance, and transaction integrity. |
| `process.direct_command_failed` | Reserved for direct-command failure; structured process-result APIs remain non-throwing. |
| `ffi.invalid_signature`, `ffi.invalid_argument`, `ffi.invalid_handle`, `ffi.callback_failed` | FFI contract failures. |
| `host.provider_error` | Explicit host provider Error under the current untrusted-recoverability contract. |
| `internal.host_exception`, `internal.unexpected_exception`, `internal.invariant_violation` | Contained native exceptions and implementation defects. |
| `internal.import_lifecycle`, `internal.identity_exhausted`, `internal.resource_exhausted` | Unsafe import rollback, identity exhaustion, and native resource exhaustion. |

`ffi.library_unload_failed` is excluded because unload failure and handle-state
semantics are deferred. `host.callable_exception` is excluded; a thrown host
C++ exception is `internal.host_exception`.

## Origin and projection

- The defining operation owns `DiagnosticOrigin` and snapshots source, line,
  column, span length, and source line.
- Callable, method, lambda, callback, import, template, script, future, thread,
  and REPL propagation append typed frames without replacing the origin.
- Compatibility prefixes such as `import: `, `future: `, and `thread: ` live on
  frames and are applied only by `project_diagnostic`.
- Platform details may extend `message`; they never select a code or
  disposition.
- Existing `BuildError`, public C++ results, C ABI results, and maintained
  bindings remain projections with unchanged public layouts.

## CP4 gate

Before automatic conversion starts, each converted producer must identify its
code directly at the branch. No producer may select disposition or code from a
message prefix. JSON/schema and package metadata loaders must expose typed
distinctions where their current boolean/string results conflate categories.
