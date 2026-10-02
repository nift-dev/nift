# v4.6 Batch 4 CP5a FFI loader/symbol recoverable failures

Status: complete. Converts only the approved safe FFI loader/symbol
operational failures to recoverable Errors. No native-fault recovery; no
broadening of catchable FFI surface.

## Audit matrix

| Operation | Current behavior | Intended disposition | Code |
| --- | --- | --- | --- |
| `ffi_open` load missing/unloadable library | contained loader failure | recoverable | `ffi.library_load_failed` |
| `ffi_call` missing symbol on a valid handle | contained loader failure | recoverable | `ffi.symbol_not_found` |
| `ffi_close` unload failure | error, no retry contract | fatal (deferred) | n/a |
| invalid signature/type (malformed signature, unsupported type, mixed float/int, >6 args) | error | fatal | n/a |
| wrong argument count | error | fatal | n/a |
| invalid/forged/closed library handle | error | fatal | n/a |
| `ffi_callback` contract violations (non-callable, unsupported signature) | error | fatal | n/a |
| buffer/C-string lifetime misuse | error | fatal | n/a |
| native crashes / signals / SEH | never contained by Nift | non-recoverable | n/a |
| foreign C++ exceptions | contained as fatal | fatal | `internal.*` |

Disposition is selected directly at the loader/symbol branch with an explicit
`DiagnosticCode`; it is never inferred from loader message text.

## Converted producers

- `ffi_open`: `dlopen`/`LoadLibraryW` returning null becomes recoverable
  `ffi.library_load_failed` (POSIX and Windows). The original message is
  preserved (`ffi_open: <platform loader detail>`). The branch returns before
  any registry entry is created, so a failed load registers no handle.
- `ffi_call` symbol resolution: `dlsym` error / `GetProcAddress` null becomes
  recoverable `ffi.symbol_not_found`, preserving
  `ffi_call: symbol lookup failed: <detail>` and `ffi_call: symbol not found:
  <symbol>`. The failure returns before any registry mutation, so a failed
  lookup leaves the library registry unchanged (verified by a subsequent valid
  call on the same handle).

## Boundary kept fatal

Malformed declarations/signatures, unsupported types, wrong arity, forged or
closed handles, callback contract violations, buffer misuse, `ffi_close`
unload failure (deferred unload semantics, no reviewed retry contract), double
close, and every native-fault/foreign-exception path remain fatal and bypass
`catch`. `ffi_buffer`/`ffi_bytes`/`ffi_snapshot_bytes`/`ffi_sizeof`/
`ffi_struct` are unchanged (all fatal programmer/type errors).

## Lifetime and rollback

- Failed load registers nothing and allocates nothing to leak.
- Failed symbol lookup does not mutate the FFI library registry.
- Caught failures do not leak native handles; successful libraries keep their
  existing lifetime semantics.
- Import rollback of a module that successfully loaded an FFI library and then
  failed removes/unloads the library exactly once (verified: a later re-open
  works, no double unload). A failed `ffi_open` inside an import adds nothing
  to roll back.

## Cross-platform contract

POSIX (`dlopen`/`dlsym`) and Windows (`LoadLibraryW`/`GetProcAddress`) both
produce `ffi.library_load_failed` / `ffi.symbol_not_found` with category `ffi`,
recoverable disposition, and the same resource-state behavior. The literal
OS loader detail in the message is platform-specific by design and is not a
language contract.

## Worker interaction

Recoverable FFI failures propagate through the existing worker machinery:
`await` and `join` replay the same immutable Error (`ffi.library_load_failed`
from a future, `ffi.symbol_not_found` from a thread), and repeated observation
replays it consistently. Full worker hardening remains CP7.

## Public ABI boundary

No new public Error value, no C ABI layout change, no binding result-model
change. An uncaught recoverable FFI failure projects through the existing
failed `ScriptResult`/`RenderResult`; a successful public value graph
containing Error is still rejected.

## Performance

The change touches only the failure branches of `ffi_open` and the `ffi_call`
symbol lookup; the successful dispatch path is unchanged (it already called
`dlerror()` before/after `dlsym`). Successful FFI calls therefore have no new
classification, allocation, registry scan, or loader call. The existing v4.5
FFI walls cover the successful hot path.

## Wall

```sh
make test-v46-b4-cp5a
```

runs the full Batch 4B aggregate gate, then the focused CP5a wall. The wall
covers missing-library/symbol caught and uncaught (code/category/origin/
message), no-mutation-after-failed-lookup, fatal-stays-fatal (signature/type/
arity/handle/callback/double-close), successful and repeated load/use/unload,
future/await and thread/join recoverable propagation with repeated observation,
and import rollback of FFI resources.

The established FFI walls (`v45_ffi_scalar`, `v45_ffi_contract`,
`cp20_bytes_ffi`) and the full aggregate gate remain green.