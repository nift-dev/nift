# Nift v4.6.0

Development has advanced to v4.6.0 after the completed v4.5.0 release.

- Native `epoch()` returns the current Unix epoch as an integer number of
  milliseconds. `sleep(ms)` blocks the current script execution thread for a
  non-negative signed 64-bit integer duration and returns `null`.
- `timer()` provides an opaque, identity-preserving monotonic stopwatch with
  start, pause/resume, stop, reset, elapsed-millisecond, and state operations.
  Timer handles remain Parser-local and are rejected at rendering, embedding,
  serialization, and thread/future transfer boundaries.
- `secure_random_bytes(n)` returns up to 10,000,000 bytes from the operating
  system CSPRNG, with no fallback pseudorandom generator.
- Runtime output now uses execution-scoped stdout and stderr sinks. `err(value)`
  writes one atomic line to stderr with the same value rules as `print(value)`,
  while embedding results capture both channels separately from values,
  diagnostics, and rendered content. The additive C ABI is now 1.3.
- Relative imports are now owned by their defining source module. Exported
  package functions, methods, lambdas, callbacks, nested modules, and
  transitive re-exports retain that owner, and a missing package-local sibling
  no longer falls through to a consumer-project decoy. Bare package imports
  and absolute/non-relative paths keep their distinct behavior. Package-relative
  paths are canonically confined; module environments retain frozen lock
  provenance and delayed imports use short validation locks, rejecting stale
  replaced ownership without deadlocking synchronous child package commands.
  Worker parsers deep-copy module graphs so nested worker imports cannot mutate
  parent or sibling state.
- New `module_path()` / `module_path(relative)` and `package_path()` /
  `package_path(relative)` APIs expose explicit absolute normalized resource
  paths without changing existing filesystem resolution. They retain CP8 module
  ownership across exported callables, methods, lambdas, callbacks, re-exports,
  and workers; reject empty, rooted, and escaping arguments; canonically confine
  package-owned results; and report controlled errors when no file or package
  owner exists instead of falling back to the process CWD. Filesystem authority
  is canonicalized once per parser/invocation and copied to workers, so relative
  `--fs-root` values remain anchored across `cd()`; package-owned path checks use
  scoped provenance validation and return path strings without a lasting lease.

### Language and runtime

- **First-class immutable `bytes`.** `bytes(...)` constructs an immutable,
  shared-backed byte value from an array of byte integers; `length`/`size`,
  `empty`, integer indexing (`b[i]` / `b?[i]`), `slice`, `+` concatenation and
  `==`/`!=` equality are supported, with strict UTF-8 `decode("utf-8")`
  (`encode("utf-8")` on strings) and controlled errors for invalid UTF-8,
  out-of-range indexing, non-byte elements, and non-byte operations. Bytes
  preserve shared identity across assignment, copy/deepcopy, closures, nested
  aggregates, mutexes, threads and async transfer. Text/value serialization and
  JSON reject bytes; conversion is explicit.
- **Byte I/O.** `open_bytes(path)` / stream and managed-file byte reads plus
  exact raw byte writes are additive; existing text I/O is unchanged.
- **Recoverable operational errors.** External/operational failures now
  propagate to a caller that deliberately handles them: FFI loader and symbol
  lookup failures, import-source acquisition failures, approved filesystem and
  `FileValue` backend failures, stream backend failures, and JSON parse/schema
  rejections are recoverable, while programmer/invariant/policy failures remain
  fatal. This is a runtime error-handling contract, not a general exception
  system.
- **Structured diagnostic outcomes.** Failures carry structured diagnostic
  outcomes (code, disposition, source origin, frames) instead of only a message
  string, improving error reporting across script, template, import and
  embedding paths.
- **Filesystem type inspection primitives.** `exists(path)`, `is_file(path)`,
  `is_dir(path)`, and `stat(path)` are available; symlinks are followed, missing
  paths return `false`, and permission/metadata failures return a controlled
  recoverable error rather than a wrong answer.
- **Exact-number (StrNumber) semantics.** Runtime numeric values retain an exact
  decimal spelling when conversion to `double` would lose its JSON-number
  semantics, so large integers and exact decimals round-trip and compare
  correctly.
- **Stream insertion and extraction operators.** `<<`/`>>` style insertion and
  extraction for the supported stream surface, with recoverable backend
  failures and a complete stream lifecycle API.

### Packages, imports and modules

- **Deterministic package graph.** A v2 package-graph lock representation, a
  deterministic transitive graph resolver, and package inspection/certification
  commands; the resolved graph is reproducible and inspectable.
- Relative import ownership (above) and explicit module/package resource paths
  (above) round out the import/module behavior.

### Concurrency

- **Worker/concurrency hardening.** Worker parsers deep-copy module graphs so
  nested worker imports cannot mutate parent or sibling state; concurrency and
  worker behavior were re-certified.

### Embedding / C ABI

- The C ABI is now **1.3** (1.2 added copied Engine/Context byte inputs and
  result-owned top-level immutable byte views; 1.3 added the execution-scoped
  output sinks). Checked JSON extraction is lazy and synchronized.

### Bug fixes and hardening

- Diagnostics for very long (generated/minified) source lines now render a
  bounded excerpt with the true `file:line:column`, instead of emitting the
  whole line and a caret padded to the original column.
- Windows process spawn now quotes empty-string arguments correctly; MSVC
  portability for recoverable-error origin sources; Windows reparse containment.
- Parser robustness: object literals with embedded unescaped quotes,
  object-literal string-concat values evaluated as expressions, bytes-index
  binding in loop bodies, and struct-method dispatch via the instance's own
  definition (including exported module struct types).
- Package integrity: strict package-metadata validation, transactional package
  updates, v2 graph-lock recovery, package self-cycle rejection, and package
  import frame labels pinned to the attempted package root.
- Error persistence boundary closed (Error values cannot be persisted); timer
  transfer boundaries preserved; script resource rollback scoping; HTML comment
  state preserved across template parsing; REPL executable paths may contain `(`.
