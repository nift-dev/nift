# v4.6 Batch 4 CP5b import-source recoverable failures

Status: complete. Converts only the approved operational import-source
acquisition failures to recoverable Errors at their existing producer
branches. No import/module/frame/rollback redesign; CP6 established those
semantics and CP5b selects codes.

## Registry (confirmed from CP2)

| Code | Disposition | Meaning |
| --- | --- | --- |
| `io.import_source_unreadable` | recoverable | ordinary non-package import source cannot be acquired |
| `package.import_source_unreadable` | recoverable | installed package metadata valid but its selected source cannot be acquired |
| `package.not_installed` | recoverable | package directory or manifest is absent, not malformed |
| `package.manifest_invalid` | fatal | installed package metadata malformed / entry escapes |
| `host.provider_error` | fatal | host/provider internal error (unchanged) |

No new import codes were invented.

## Converted producers (`ParserScript.cpp`, `execute_import_file`)

- **Plain-file source read** (the `read_shared_source` branch): a missing or
  otherwise-unreadable source now routes through `fail_recoverable` with
  `io.import_source_unreadable` (or `package.import_source_unreadable` when a
  package provenance is active), building the Error value and the `import: `
  frame. `HostStatus::Error` (provider/internal) is split out and stays fatal
  `host.provider_error`. Previously this branch only set a Recoverable-code
  diagnostic without an Error value, so the failure was effectively fatal.
- **Package not installed**: when `.nift/packages/<name>/manifest.json` is
  absent (package directory or manifest missing), the import is recoverable
  `package.not_installed`.
- **Package manifest**: present but malformed (parse failure, entry escaping
  the package dir) stays fatal `package.manifest_invalid`.
- **Package entry acquisition**: a valid manifest whose declared entry file is
  missing/unreadable fails at the source-read branch as recoverable
  `package.import_source_unreadable`.

The previous `load_package` call conflated not-installed with invalid-manifest;
it is now decomposed in the import path only (the shared `load_package` helper
is unchanged for other callers).

## Kept fatal (verified to bypass `catch`)

Malformed import syntax, invalid argument type, import cycles, malformed
package metadata, entry-escape/containment, authority/path escape, and
`HostStatus::Error` provider failures. The worker-owning failed import rule is
unchanged: any failed import that created a worker is promoted to fatal
`internal.import_lifecycle` even when the underlying failure (e.g. a missing
nested import source) is recoverable.

## CP6 invariants preserved

- Rollback-before-catch: an outer module that creates parser-owned state and
  then hits a recoverable missing inner import leaves no partial
  exports/types/variables for the caller's `catch`.
- One `import:` frame per boundary; nested failures retain the full
  `import: import: ...` frame stack; code/message/frame responsibilities stay
  separate (the Error message is not the flattened frame stack).
- Recoverable failures keep the defining/source-acquisition origin with the
  import frame on the diagnostic.
- Relative import ownership, FileBacked/InMemory provenance, and the REPL
  modern-`import` behavior from CP6 are unchanged.

## REPL

`try { import("./missing.f") } catch(e) { ... }` is catchable in the REPL; a
successful import immediately afterward installs its exports, and ordinary
command fallback still works. No stale diagnostic/Error/frame/transaction
state.

## Public boundary

Uncaught recoverable import-source failures project through the existing
failed `ScriptResult`/`RenderResult`; Error values remain interpreter-only; no
C ABI / binding layout change.

## Wall

```sh
make test-v46-b4-cp5b
```

runs the full Batch 4B/CP5a/CP6 aggregate gate, then the focused CP5b wall. The
wall covers direct/nested missing source caught and uncaught (code/category/
origin/message/frame stack), package not-installed vs source-unreadable vs
malformed-manifest, fatal stays fatal (syntax/type/cycle/malformed manifest),
rollback-before-catch, worker-owning failed import promotion to fatal, REPL
recovery, and relative import ownership.

The established Batch 3 provenance/package/import walls, the CP6 wall, the
CP5a FFI wall, and the Batch 4B aggregate remain green.