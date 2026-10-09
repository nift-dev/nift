# CP410-A: callback descriptors and invocation overlays

Starting HEAD/origin: `b1267a5b1a2224ec3a230d2640d2e0ad9314992c`, clean. ABI remains 1.3. Production publication and hosted certification are recorded separately after commit.

## Retained implementation

Lambda instances publish complete immutable capture maps. Collection lambda callbacks use a parameter overlay and hydrate private descriptors for supported expression plans; sync never changes published metadata. Unsupported/non-numeric expressions, blocks, nested factories and other compatibility bodies materialize untouched captures before invoking the existing evaluator. Parameters and hydrated entries win during materialization. Ordinary mutable Parser scopes and named-function frames remain unchanged. Instances retain fresh identities and existing parser lifetime.

The optional weak last-snapshot memo follows an independently measured overlay-only attempt. It compares the exact effective name set/cardinality and each value/slot/root pointer **and ownership**, type, mutability/readonly/invocation flags, validity and full path. Inner-scope shadows are resolved explicitly. It retains no owning memo, hashes alone, mutation epochs or raw interior-pointer cache. Async workers clone complete environments into new immutable maps; existing transferability checks still inspect unused captures.

## Results

Same local N=16k fixtures, complete process including input construction and teardown. These are not reruns of frozen official benchmarks.

| Shape | Instructions before | After overlay only | After overlay + exact reuse | Median CPU before / after | Peak RSS before / after KiB |
|---|---:|---:|---:|---:|---:|
| Identity, unused=0 | 442,821,117 | 413,420,197 | 369,259,842 | 0.084888 / 0.065469 s | 49,844 / 38,296 |
| Captured, unused=0 | 532,355,510 | 464,708,832 | 411,081,872 | 0.090960 / 0.068619 s | 52,378 / 38,348 |
| Identity, unused=100 | 3,008,310,286 | 1,724,385,372 | 717,796,746 | 0.428135 / 0.081950 s | 289,310 / 38,354 |
| Captured, unused=100 | 3,096,076,914 | 1,766,535,530 | 762,760,998 | 0.426935 / 0.086132 s | 291,608 / 38,536 |

Ordinary identity CPU improves 1.30×, instructions 16.6%; captured CPU 1.33×, instructions 22.8%. Wide identity/captured CPU improves 5.22×/4.96× and instructions 76.1%/75.4%. This meets the conditional acceptance range through broad callback scaling, major retained-memory gains and teardown improvements; **it does not meet a 2× ordinary-sort target**.

Identity allocation count is 512,480 → 352,486 without added unused bindings, and 3,809,294 → 353,403 at unused=100. Cumulative bytes are 86,496,994 → 66,273,677 and 576,873,957 → 66,425,960. All four Memcheck processes report zero errors and zero heap at exit.

Inclusive Parser teardown falls 45,890,962 → 27,862,095 instructions at unused=0 and 418,974,639 → 27,950,361 at unused=100. These inclusive costs must not be added to overlapping call costs.

The full 240-profile matrix covers 2k/4k/8k/16k × unused 0/5/16/50/100 × identity/captured sort/map/filter/group/closure. Every candidate output equals baseline exactly. `matrix-summary.json` gives individual reductions; matrix fixtures share a `v` binding even for identity, so use the separate headline fixtures for official-equivalent identity comparisons. At 8k, ordinary map/filter/group/closure improve instructions approximately 20–22%; unused=100 improves approximately 83–84%.

Ordinary controls remain neutral: loops −0.25%, scalar calls −0.12%, named no-argument calls −0.16%, closure calls +0.38%, callback control +0.19%, BFS −0.07%. No scope-wide wrapper is retained.

## Missing-name and semantic boundary

Overlay/parameter/capture/hydration/miss/fallback counters exist only in the dedicated test build. Missing captured membership still falls back to caller/global scopes. At x1/x8/x32 repeated late-bound reads instructions improve 6.5%/26.7%/33.0%. At x128/x256/x1024 the unchanged 64-node preparation guard selects compatibility evaluation and materializes first; whole-process instructions remain effectively neutral. There is no invented promotion threshold or enlarged preparation guard. `misses-instructions.json` and the permanent counter guard distinguish these paths explicitly.

The new 23 exact contracts preserve complete stderr origins, factory effects, live captures, parameter shadowing, captured-name declaration errors, late membership, nested escaping closures, invalid/reallocated root aliases, readonly behavior, all-visible timer rejection and non-numeric fallback. Existing 31 sort contracts and 470 numeric/fallback pairs also pass. Fresh-instance preparation counters retain 100,000 sort identities.

## Certification

All local groups in `safety/progress.log` pass: focused exact contracts; native/embedding/bindings; GCC and Clang warnings; binding warnings; 93 NRS modules; 12 PRS modules; ASan/UBSan/LSan lifetime and Deep suites; additional capture/root/async checks; sanitized overlay/object/sort contracts; core memory; 1,219 parser-fuzz cases (232 builds, 987 controlled errors). New parity/counter targets are included in normal tests and the cross-platform wall and pass independently.

Default/migration/rewrite/redesign scaffolds preserve exact generated public/document hashes across init/status/full build/incremental build. All 30 frozen official and 51 frozen expanded-shell evidence files remain byte-identical. Jsonic++ and Minify++ remain clean; no pins or synchronization changes exist. No Labs changes, Linodes or official reruns were made.

## Residual costs and next work

Exact descriptor comparison remains O(effective capture width): 345,974,852 inclusive instructions in the wide headline fixture, compared with 9,153,816 at ordinary width. Fresh instance metadata, callback argument/value ownership, registry allocation, decorated stable-sort moves and input construction remain. Registered identity reclamation and general scope replacement stay deferred. Unsupported bodies still materialize complete private maps; this tranche deliberately limits overlays to the already-certified expression executor.

The overlay-only candidate was insufficient for ordinary sort and is archived as an intermediate experiment. The older generic scope wrapper remains dropped. Reprofiled current state supports moving next to the general traversal shared-prefix implementation; immutable callable plan sharing is not front-loaded.
