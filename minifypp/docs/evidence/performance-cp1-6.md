# Performance campaign CP1–CP6

This campaign begins from `4c694d4`, the first low-risk syntax-storage reserve change layered on the certified `9ba9b4f` production baseline.

## CP1 — baseline and acceptance rules

The campaign baseline is frozen in `performance-campaign-baseline.md`. Correctness/post-validation failures are automatic rejections; performance claims require repeated measurements and same-run controls when using the external benchmark.

## CP2 — reproducible API profiling harness

`make profile-js FILES="..." RUNS=5` builds a C++ API harness and emits one CSV row per raw run for default, structured and aggressive modes. It performs one unreported warm-up and retains output byte counts so a timing experiment cannot silently change the output contract.

## CP3 — stage attribution

Setting `MINIFY_PROFILE_STAGES=1` reports per-invocation timings for scanner/emission, concrete syntax construction, scope graph construction, reference resolution and semantic-fact construction. The instrumentation is opt-in and prints to stderr, leaving normal API output unchanged.

## CP4 — bounded allocation growth

Predictable outer vectors/stacks reserve conservative capacity before large structured analyses. On a 231 KiB generated broad-scope probe, repeated stage-level samples showed median concrete-syntax time fall from about **15.1 ms to 12.5 ms** and reference-resolution time from about **10.1 ms to 8.8 ms**. Scope construction was essentially flat in that probe. These are diagnostic results on the shared host, not release benchmark claims.

## CP5 — indexed var/function redeclarations

Scope construction no longer rescans every earlier binding in a scope to coalesce `var`/function redeclarations. A per-scope name index retains the existing coalescing semantics. On the 317 KiB redeclaration stress probe, median reported `scope_graph` time fell from about **4.05 ms to 3.45 ms** (~15%). Smoke and Node semantic differential gates pass.

## CP6 — reference-resolution cache trial rejected

A broad per-scope lexical-resolution cache was implemented and measured, then removed. The additional hash-map population/allocation did not establish a stable end-to-end win on broad, redeclaration-heavy or repeated-outer-reference probes. The active production resolver is therefore the CP5 resolver. Future attempts should use a more compact binding-ID strategy or selectively cache only profile-proven hot scopes/names.

This rejection is intentional: the performance campaign does not retain plausible changes merely because they are theoretically faster.

## Validation state

At the CP6 endpoint:

- `make` passes;
- `make test-smoke` passes;
- the Node semantic differential gate passes;
- the full scope-semantics script was started in this constrained environment but exceeded the command execution window while compiling/running its broader workload, so it is not claimed green here;
- final external 12-fixture certification remains required before these changes are treated as a benchmark win.
