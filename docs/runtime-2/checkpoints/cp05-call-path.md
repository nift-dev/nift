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
