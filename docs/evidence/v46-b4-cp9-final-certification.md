# v4.6 Batch 4 CP9 final certification / release-readiness gate

Status: complete. Answers: is the complete Batch 4 recoverable-error
implementation ready to become part of the eventual v4.6 release candidate?
This is a certification checkpoint — no new Error semantics, no deterministic
transitive package-dependency work, no unrelated parser cleanup. CP9 does not
tag or release anything; it establishes "Batch 4 closed, v4.6 candidate-ready
with respect to Batch 4."

## Authoritative matrix

| Subsystem | Recoverable cases (catchable) | Fatal cases (bypass catch) | Tests / evidence |
| --- | --- | --- | --- |
| Filesystem | missing/unreadable path, permission/backend I/O, failed create/copy/move/remove, atomic replace (`io.*`) | wrong arity/type, root/project escape, forged/stale handles, lifecycle misuse | CP4a evidence + wall |
| FileValue | `save()` write/replace failure (stays dirty/open, retryable) | dirty close, unopened op, wrong mode | CP4a |
| Streams | open/read/write/flush/close backend failure (`stream.*`) | direction misuse, closed/unopened op, double open, non-renderable write (incl. Error) | CP4b/CP4b+ walls |
| JSON | malformed runtime `@json` data (`json.parse_failed`) | malformed inline bodies, config JSON | CP4c |
| Schemas | valid schema rejecting candidate (`schema.rejected`) | malformed/unsupported definition (`schema.definition_invalid`) | CP4c |
| FFI | library load failure (`ffi.library_load_failed`), missing symbol (`ffi.symbol_not_found`) | invalid signature/arg/handle, callback misuse, native faults | CP5a + CP8 |
| Imports | missing/unreadable source (`io.import_source_unreadable`), uninstalled package (`package.not_installed`), unreadable package entry (`package.import_source_unreadable`) | malformed import syntax, arg/type, cycles, authority/escape, provider errors | CP5b + CP6 |
| Packages | not-installed / unreadable entry source | malformed manifest/lock, provenance mismatch, transaction recovery | CP5b |
| Workers | built-in recoverable produced in a worker, replayed at `await`/`join` (code preserved) | programmer errors, invariant violations, worker-owning failed import (`internal.import_lifecycle`) | CP7 |
| Embedding / C ABI | failures project as ordinary failed ScriptResult/RenderResult (message+origin), no Error leak | fatal stays failed; recursive Error rejected; no stringify | CP8 |
| User | explicit `throw error(...)` / `error(...)` (`user.*` -> `UserRaised` internal) | `throw` non-Error | CP3 |

Every approved Batch 4 design requirement is implemented+tested, explicitly
certified unchanged, or deliberately deferred by the approved design (result
APIs, host seams, native faults). No accidental fourth category.

## Recoverable Diagnostic registry audit

All 23 Recoverable registry codes were mechanically audited (the CP9 wall):

| Code | Producer |
| --- | --- |
| `user.raised` | `@__throw` handler + `error()` constructor |
| `io.open_failed`, `io.read_failed`, `io.create_failed`, `io.write_failed`, `io.copy_failed`, `io.move_failed`, `io.remove_failed`, `io.directory_read_failed`, `io.change_directory_failed`, `io.atomic_replace_failed`, `io.import_source_unreadable` | `fail_recoverable` in filesystem/import producers |
| `stream.open_failed`, `stream.read_failed`, `stream.write_failed`, `stream.flush_failed`, `stream.close_failed` | `fail_recoverable` in stream producers |
| `json.parse_failed` | `fail_recoverable` in the `@json` directive |
| `schema.rejected` | `fail_recoverable` in validation |
| `package.not_installed`, `package.import_source_unreadable` | `fail_recoverable` in the package import path |
| `ffi.library_load_failed`, `ffi.symbol_not_found` | `fail_recoverable` in the FFI load/symbol producers |

Each has at least one real producer that constructs the Error (code/category
set, `active_recoverable_` populated), a caught-path test and an uncaught
projection test in the CP3..CP8 walls, and source/origin where applicable.
`fail_recoverable`/`fail_fatal` both validate the code's registry disposition
and throw `std::logic_error` on a mismatch, so no Recoverable code can be
produced fatally and no Fatal code can be produced recoverably. Codes are
selected by the producer (never inferred from message text). The inverse risks
were checked: every Recoverable code constructs an Error; no built-in worker
Error falls back to `UserRaised` (CP7 M1 fix) except arbitrary `user.*` errors,
where `UserRaised` is the approved umbrella.

## Fatal-boundary audit

The CP9 wall re-certifies that syntax, unknown names, division by zero,
invalid arity, FileValue lifecycle misuse, `throw` of a non-Error, and
`import` of a non-string all bypass `catch`. Combined with the CP3..CP8 walls,
the broad fatal classes remain non-catchable: type, privacy/export, policy/
authority, invalid/forged handles, lifecycle misuse, malformed schema
definitions, malformed package metadata, import cycles, worker-owning failed
imports, runtime invariants, unsupported native values, and native crashes/
UB/signals. Batch 4 did not widen `catch` beyond the approved operational
boundary.

## Resource / lifetime

Re-certified by the CP9 wall + existing walls: managed files (retryable save,
rollback-before-catch), streams (close-state checks), FFI libraries/buffers/
callbacks (no handle leak on failed load, no double close), import/module
rollback (no partial exports), workers/futures/threads (repeated await/join,
unobserved teardown, failed-import worker ownership), timers (transfer
rejection), package/module authority, and Engine/Context reuse after failure.
No leak/double-close/double-rollback/stale-handle/stale-Diagnostic/
use-after-free or partial-state-surviving-catch findings.

## Cross-platform semantics

No `fail_recoverable`/`fail_fatal` call sits inside a platform conditional in
the producer files. The Nift-level codes and dispositions are platform-
independent; only native error message text differs across Linux/macOS/Windows
(which is accepted). Verified for filesystem, streams, atomic replace, FFI
load/symbol and path/package confinement.

## Sanitizers

Re-run in the CP9 gate: ASan/UBSan concurrency walls (`-fsanitize=address,
undefined`, leak detection, halt_on_error), TSan concurrency walls
(`-fsanitize=thread`, halt_on_error), plus the CP7 wall under both sanitizers
and the CP8 embed test under ASan/UBSan. No unexplained finding.

## Performance vs pre-Batch-4 baseline (21e7f97)

Reused existing harnesses; no new benchmark framework.

| Workload | pre-Batch-4 | current | verdict |
| --- | --- | --- | --- |
| full website build (`nift build --all`, 101 pages) | 0.03s | 0.03s | unchanged |
| no-op incremental build | 0.00s | 0.00s | unchanged |
| fib(25) recursion | 6.87s | 6.87s | unchanged |
| string-concat loop (20k) | 0.02s | 0.02s | unchanged |
| successful async completions | 0.03s | 0.02s | unchanged |
| successful thread create/join (500) | 0.07s | 0.07s | unchanged |
| successful FFI call (10k) | 0.14s | 0.14s | unchanged |
| tight int loop `s = s + i` (1M, best-of-5) | 0.66s | 0.89s | +35% |

The one measured regression is a tight numeric loop. Root cause: CP4a replaced
per-statement loop-body execution with `execute_body_outcome`, which constructs
an `ExecOutcome` (336 bytes) per statement to carry recoverable/fatal outcomes
for rollback-before-catch — a Batch 4 design requirement. The cost is bounded
to tight statement-dispatch loops; realistic workloads (build, recursion,
string work, concurrency, FFI) show no regression, and peak RSS on the tight
loop rises ~12% (~0.8 MB). This is recorded as a known cost of the error model;
an optimization (e.g. shrinking `ExecOutcome` storage to shared_ptr for the
empty path) is deliberately NOT made unilaterally in CP9 and is deferred for
approval. Structured diagnostic preservation on the failure path is not
optimized away.

## Regression suite

The CP9 wall runs the full Batch 4 aggregate (CP1..CP8), the v4.5 concurrency
walls, package/import/provenance walls, embedding/C ABI + all four maintained
language consumers + staged consumer, and the ASan/UBSan + TSan gates (387+
aggregate PASS in the CP8 chain, plus the concurrency walls).

## Documentation / site consistency

The published website states that errors are interpreter-only, worker replay
is deterministic, and the recoverable/fatal boundary is narrow (FFI,
imports/packages, streams). No CP7/CP8 result invalidates those statements:
CP7 preserved user-visible Error code/category while fixing the internal
observer Diagnostic; CP8 confirmed there is no public Error transport. The
published docs match current-main behavior; no further rewrite is needed.

## Release-state wording

Batch 4 is closed and v4.6 is candidate-ready with respect to Batch 4. This is
current development mainline, not a tagged v4.6 release. No tag, no release.

## Wall

```sh
make test-v46-b4-cp9
```

runs the CP1..CP8 aggregate, then the CP9 wall: the recoverable-registry
producer audit (all 23 codes), the fatal-boundary probe set, the
resource/lifetime + concurrency walls, the embedding/C ABI/bindings/staged
consumer wall, and the ASan/UBSan + TSan gates.