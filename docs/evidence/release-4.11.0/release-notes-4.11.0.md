# Nift v4.11.0

Nift v4.11 improves incremental build correctness after targeted builds and
subprocess reliability across Linux, macOS and Windows. C ABI remains **1.3**.

## Incremental correctness

- Targeted builds no longer erase stale-state history for omitted consumers of
  shared dependencies. Hash and hybrid modes retain dependency snapshots per
  consumer, so rebuilding one page does not make another incorrectly appear fresh.
- Nift-native dependency reads snapshot the exact bytes consumed. Generated
  dependencies and targeted prerequisite closure retain their build ordering and
  invalidation behavior.
- Older v4.10 hash/hybrid metadata upgrades conservatively: affected consumers
  rebuild once to establish snapshots, then return to normal no-op behavior.
  Modified mode remains timestamp-based.

Inputs must remain stable while a consumer executes. Detectable changes to declared
inputs during opaque external builds reject certification; arbitrary external
change-and-restore (ABA) mutations during opaque reads are outside this contract.
FileValue reads require explicit dependency declarations to invalidate consumers.

## Process and shell hardening

- POSIX argument, environment and executable-search preparation occurs before
  `fork`, improving launch safety in multithreaded builds and scripts. Partial
  pipelines and job launches clean up started children and owned descriptors;
  capture setup failures remove temporary files. Close-on-exec protection and
  checked redirect setup prevent silent setup errors.
- Windows subprocess environment overrides use a child-specific Unicode block
  without changing the parent environment, including absent versus empty values.
  Explicit handle inheritance excludes unrelated inheritable handles.
- Failed redirects fail closed instead of silently using parent stdio. Shared
  stderr capture preserves pipeline output, including MSYS shell compatibility.
  Unicode executable, working-directory, environment and temporary capture paths
  have native regression coverage, with repeated partial-failure cleanup checks.

Windows interactive job control remains unsupported. Launch cleanup owns direct
children; it is not a universal descendant-process termination guarantee.

## Cross-platform reliability

Expanded native Linux/macOS/Windows process contracts, deterministic fault
injection and mutation guards protect the fixes. The independent Nift regression
suite passes **94/94 modules**, and the package regression suite passes **12/12**.
Incremental differential, migration, recovery and scaling guards protect the
per-consumer state model. These are scoped test results, not a formal safety proof.

## Trusted project code

Templates, scripts, packages, imports and hooks are trusted project code. Nift is
not a sandbox for hostile repositories. Filesystem/process controls restrict
selected Nift APIs; external commands and native FFI retain host capabilities.
Dependency hashes are change detectors, not cryptographic integrity checks.

No new official benchmark results, object-indexing architecture or Jsonic++
changes are included in this release.
