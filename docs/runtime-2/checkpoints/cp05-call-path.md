# CP05 — Call-path hygiene

Status: see `../progress.md`.

## Objective
Remove per-call overhead from user-function calls without changing behaviour
(D-006: no behaviour change for interim performance; the prepared/loop
evaluator divergences are untouched).

## Profile (callgrind, runtime-2 `a3e15b5`)
Flat call inside a loop (`s = add(s, i)`, 20,000 calls, 303.7M instructions):
`getenv` was the largest single self cost (9.0%, plus `strncmp` under it):
one `getenv("NIFT_P4_DISABLE")` per call while deciding whether the P4 fast
path applies. Top-level calls (legacy path) spent ~16% re-running
`translate_function_program` on the callee body for every call.
`trim_copy` of bodies was small (<2%, spread over unrelated callers) and was
left alone; scope-map reuse was not attempted (not needed for the gate and not
provably safe without a wider audit).

## Changes
- `src/FeatureFlags.h`: `NIFT_P4_DISABLE`, `NIFT_P4_TRACE`, `NIFT_P4_SHAPE`,
  `NIFT_P4_BRIDGE` read once per process (function-local statics). They are
  diagnostic switches that only enable tracing or turn off an equivalent fast
  path, and Nift never sets them. `NIFT_NO_PROCESS` and `NIFT_FS_ROOT`, which
  the CLI sets at run time, are still read live.
- `Parser::execute_native_program`: caches the translation of a source text
  for a caller-supplied `SourceView` (function bodies, nested blocks). The
  translator is a pure function of (text, view) and reads no parser state; the
  cache key is the view's mapping identity (`SourceView::key()`), the entry
  holds the view (so its pointers cannot be reused by other text) and the text
  is compared on every hit. Failed translations are not cached (the same
  diagnostic is produced every time). Fresh identity views (whole scripts) are
  never cached. Bounded at 4,096 entries (cleared when full).

## Compatibility
No behaviour change intended or observed (see Evidence). A script that
changes `NIFT_P4_*` with `setenv` during a run no longer toggles the P4 fast
path mid-run; P4 is semantically equivalent to the general path, so only
performance could differ.

## Gates and evidence
See `../evidence/cp05/README.md`.

## Residual risk
The translation cache trusts that `translate_function_program` stays a pure
function of (text, view). A future change that makes translation depend on
parser state must key the cache on that state or drop it.

## Review (same-model adversarial pass; not independent)

| # | Finding | Resolution |
| --- | --- | --- |
| 1 | Memory: a `fn` declared inside a loop or another function got a fresh span vector per evaluation, so pointer-keyed entries never hit and the cache grew (up to 386 MB RSS vs 11 MB) | Keys use span *contents*; a hit also needs identical text and an exactly equal mapping (`SourceView::same_mapping`); the cache is bounded by 4,096 entries and 16 MiB. Reproducers now 11.4 MB, same as the reference |
| 2 | `setenv("NIFT_P4_TRACE", ...)` mid-run ignored (flags latched per process; also hid host env changes between Engine renders) | Flags cached per environment generation; Nift's setenv/unsetenv builtins, hooks, CLI writer and every Parser construction bump it. Reproducer matches the reference |
| 3 | Reference numbers for `call_top`/`fib_top` did not reproduce | Corrected below: the gated flat call (in a loop) is −10.5%; top-level calls −4.6%, `fib` −5.9% |
| — | Translator purity, eviction, ASan/UBSan (781 scripts), TSan (63 runs), rt-diff 9,514 entries | Held up |

## Corrected measurements (callgrind instructions per op, reference = runtime-2 a3e15b5, after review fixes)

| Op | Reference | CP05 | Change |
| --- | ---: | ---: | ---: |
| flat call in a loop (gate ≥10%) | 14,998 | 13,424 | −10.5% |
| flat call at top level | 145,711 | 139,069 | −4.6% |
| `fib` call at top level | 168,181 | 158,212 | −5.9% |

The earlier top-level figures in this record were wrong (unreproducible
reference numbers). Savings also scale with environment size (`getenv` scans
the environment). The remaining call cost is architectural (textual
re-evaluation per call); per D-006 the rest is left to the VM.

