# CP49-23 through CP49-50: second local performance tranche

Status: COMPLETE — seven bounded changes retained and certified locally.
The first tranche remains accepted. Final runtime commit: `3c0a5b22c24da50f749e1f385d84d52ce1a4543d`.
Starting source: `d9b85f22c4dab11d0ab68a8778a1471bf35ba488`.
Final production binary SHA-256: `790bbce6325b0eadce6d98fbb27527ccd4c43fa082b23d1ca3667e54022bc862`.
The subsequent documentation commit changes no runtime source.

The user reopened performance work from `d9b85f22c4dab11d0ab68a8778a1471bf35ba488`.
The saved starting binary is byte-identical to the accepted first-wave final.
[Binary identities](start.json) distinguish the original CP49 source baseline
`e291575f004a601a7d9cbb645ec7810e09bdc325`, first-wave final and second-wave start.
Version remains 4.9.0; C ABI remains 1.3.

The intervening official benchmark handoff was canceled before any smoke,
official or A/B run. The benchmark chat reports both campaign-created Linodes
deleted and independently verified by HTTP 404. No benchmark or Labs results
were published. The frozen `20261008-v480` series must remain unchanged;
official reruns are deferred until after the v4.9 release unless Nick changes
that instruction. No node or official series is required for this tranche.

## Fresh reset and methodology

Fresh Callgrind self/inclusive profiles and allocation traces cover all 31
original independent families at N=2,000 (empty control N=0). [Reset metrics](reset/metrics.json)
include seven post-warmup native CPU/wall samples, Memcheck allocation/free/byte
counts and five peak-RSS samples. All 31 heaps were fully freed with zero errors.
The reset uses the current accepted binary, not old percentages.

Profiles are optimized instruction counts, not hardware CPU samples. Recursive
inclusive instruction/allocation totals overlap and can exceed 100%; do not sum
them. Short process timing includes startup/setup and is noisy on this shared
host. Longer paired controls and built-in timer observations distinguish steady
execution from fixed startup. Instrumented diagnostic binaries are separate
from production; their timing/layout is not performance evidence.

Eleven independent controls add imported and aggregate calls, aggregate and
multikey sort, wide/deep/mixed JSON, and BFS queue/set/update/nested-index
components. They do not modify official inputs. Their process CPU and instruction
counts include setup. Their deliberately empty timer tail is only an output
normalizer and must not be described as a measured steady-state phase.

## Ranked bounded candidates

| Class | Candidate | Reason / decision |
|---|---|---|
| A | Bare argument location check | After preserving the live atomic-handle check, an ordinary bare binding needs no second AST parse or lookup. Logical nested-location aliases retain the canonical probe; allocate its scratch binding only for a resolved location. KEEP: measurable call-family instruction reductions. |
| A | Owned arguments to sole outcome handler | Retain a copy only when a secondary handler exists. Both named/native retries preserve untouched arguments. KEEP: aggregate calls, frequency and BFS benefit. |
| A | Sort-owned snapshot transfers | Evaluate keys with original values, then move the private snapshot into decoration and reserve exact output size. Preserve callback count, order and stable sorting. KEEP: aggregate sorts benefit; scalar improvement is small. |
| A | Reuse numeric lookup fingerprint | Prepared map/set read lookup already computed a canonical key. Its NaN marker can be tested directly without a second numeric conversion. KEEP: set visitation/BFS benefit. |
| B | Prepared defining-path ownership | Prepared callable retains an immutable owned path; active contexts share ownership. Other contexts retain their existing directly owned paths. Do not use a diagnostic document path as semantic authority. KEEP: noarg/scalar/recursive calls benefit. |
| A | Direct callback argument snapshots | Construct owned argument vectors directly instead of copying through const initializer-list temporaries. Preserve parameter binding, evaluation order and callback identity. KEEP: general aggregate transforms. |
| B | Live numeric object-member selector | Extend bounded syntax-only numeric plans for a direct member of an object binding, including arithmetic/comparison with such leaves. Synchronize and copy immediately; unsupported types/shapes/missing members retain the oracle. KEEP: aggregate grouping/sorting improve materially. |
| D | Shared-path allocation on every source context | First experiment adds allocation to non-call contexts. Superseded by prepared-only sharing; raw first experiment retained. |
| D | Reuse legacy map.set canonical key | No measurable benefit in current prepared workloads; fully reverted. The hot mutation path already has its own prepared implementation. |
| D | Parameter-scope reserve | Failed first-wave experiment; not retried. |
| C | Reusable frames, arenas, persistent slot caches, symbol-table storage replacement | Require lifetime/invalidation/shadowing decisions. No implementation. |
| C | Global callback hoisting / selective captures | Changes evaluation timing, identity or dynamically accessible capture assumptions. No implementation. |
| C | Persistent object indexes / RuntimeValue representation / VM or parser replacement | Changes storage/ownership or architecture. No implementation. |

## Why the remaining families differ from loops

The reset loop executes 12.97M instructions and allocates 4,435 times. Its
compatibility expression entry count is seven, all outside repeated loop work.
Prepared scalar calls add a scope, parameter binding/shared slot, source context,
argument handling and call/return control: 33.19M instructions and 32,489
allocations before this tranche. Recursion repeats those operations at each level.

The fresh sort-identity counter observes 2,000 instances and 10,000 capture
binding copies, despite one cached syntax parse and 2,000 prepared bodies.
Map-identity creates one instance and five capture copies. Sort adds decoration,
key ownership and stable comparisons. The frozen official workload called
`sort-index` is identity `sort_by(x => x)`; the local `sort-index` control is the
separate captured-array selector `x => a[x]`. These labels are not interchangeable.

Aggregate/member callbacks still use the compatibility body evaluator in every
iteration at reset. The `x.v` and `x.v % 16` shapes make that fallback concrete;
syntax caching alone does not remove compatibility string work. Variable-held
closures likewise enter outer compatibility dispatch despite prepared numeric
bodies. Their remaining cost is not repeated lambda syntax parsing.

BFS costs 111.86M instructions and 80,752 allocations at reset. The component
controls separate queue/index work, set visitation, repeated map updates and
nested arrays. JSON traversal/mutation profiles contain object-vector growth,
value copies and allocation/destruction; wide-object linear lookup remains a
storage-design constraint. Already typed Number comparison and reserved JSON
conversion capacities are present, so repeating those optimizations is not a
new candidate. StrNumber spelling/equality/overflow semantics remain canonical.

## Checkpoint coverage

| Checkpoints | Investigation/control |
|---|---|
| 23, 38, 47, 48 | Fresh 31-family reset; three-state rotating paired instruction/CPU/wall/allocation/bytes/RSS comparison with 11 extra controls. |
| 24–27, 37, 43 | Calls split into noarg/scalar/multi/locals/recursive/closure/callback/imported/aggregate; scope/call/capture counters, allocation traces, separate RV lifecycle instrumentation. Live lookup retained. |
| 25, 35 | Successful path/source-context copy costs isolated; defining-path candidate. SourceView documents/mappings/provenance semantics unchanged; 45 exact diagnostic oracle cases PASS. No speculative borrowed source strings. |
| 28–29 | Per-element sort instance/capture counters; identity/index/arithmetic plus aggregate/multikey sort. No callback factory or hoisting change. |
| 30–33, 36 | Frequency, unique/repeated map mutation, set visitation, sliding/index controls; canonical fingerprint reuse. Existing typed-number and exact StrNumber semantics inspected. |
| 34, 42 | JSON parse/convert/traverse/mutate/serialize plus wide/deep/mixed controls; live numeric direct-member candidate. No persistent JSON/object index. |
| 39–41 | Empty/startup control, longer paired call loops and separate recursive compatibility/AST fallback counters. No changes to official methodology. |
| 44–45 | Async/future and actual project load/status/incremental/fullbuild semantic controls PASS in the final native/sanitizer walls and real 20-page project controls. No concurrency rewrite. |
| 46, 49–50 | A/B candidates measured; failures reverted; C boundaries left for design review. No ABI/API/syntax/vendor/frozen/Labs/official measurement changes. |

## Validation and final recommendation

All gates below certify the final seven-change runtime. The 58 selector cases
compare exact stdout/stderr/exit against the unchanged starting oracle, including
nested location aliases, missing-member origins, live captures, aggregate
independence and stable ties. The outcome-handler unit exercises both named and
native consuming Unsupported handlers with secondary retries.

| Gate | Final result |
|---|---|
| Full native, embedding and maintained binding wall | PASS |
| Strict GCC and Clang first-party warnings | PASS, zero warnings |
| 45 canonical original-source diagnostic cases | PASS |
| Selector oracle, ordinary and lifetime sanitizer binaries | 58/58 PASS |
| ASan/UBSan/LSan lifetime wall, async/futures included | PASS |
| Root/path reference and six corruption reproducers under sanitizers | PASS |
| Deep memory/lifecycle wall | 57 phases, four rounds PASS |
| Deep parser fuzz/resource wall | 1,219 cases: 232 successful builds, 987 controlled errors PASS |
| NRS / PRS | 93/93 and 12/12 PASS |
| Static test/script integrity | 290 files PASS |
| Three-state Memcheck | 126 runs, zero errors and fully freed heaps |
| Real project load/status/full/incremental build | Three binaries, identical 23-file outputs and dependency rebuild PASS |
| Public ABI, version, frozen boundaries | ABI 1.3, version 4.9.0, 2,828 hashes unchanged |

[Boundary evidence](final/boundaries.json), [project controls](final/project-controls.json),
[deep lifecycle](final/core-memory.json) and [parser fuzz](final/parser-fuzz.json)
record the checks. Complete native and sanitizer logs remain in
`.build/cp49-wave2/final-*.log`. The initial sandbox wall failure was caused by
blocked ptrace; the unchanged starting binary reproduced it. The complete wall
passed with tracing permitted, without weakening tests. GCC's sanitizer O1
stable_sort warning also reproduces on the previous runtime; canonical strict
O2 GCC/Clang gates are clean.

Recommendation: stop this bounded tranche and review architecture/ownership/
dispatch options for the remaining dominant costs before further work. This is
not a claim that every possible small win has been exhausted. Reusable frames,
arenas, persistent symbol/member indexes, selective capture/hoisting and storage
or VM changes remain C decisions requiring Nick's review. No such change was
implemented. No official rerun, release, Labs publication or provisioning is
part of this closeout.

## Rejected unsafe guard and corrected oracle

The initial bare-identifier early return incorrectly treated nested aggregate
location aliases as ordinary values. The independent mutation-through-alias
control exposed `[[1]]` where the original runtime produces `[[1,9]]`. That
unguarded form is rejected. The corrected guard preserves canonical probing for
logical location aliases and atomic handles; scalar/ordinary value bindings
still avoid redundant parsing. Both array and object alias mutations, ordinary
aggregate value passing, and iteration capture snapshots are permanent controls.
All 58 cases then pass with exact baseline output/error/exit comparison.

Pre-correction full measurements and partially completed longer timings are
retained under `.build/cp49-wave2/unguarded-*` as rejected-candidate evidence.
They are excluded from final certification. The final corrected 42-family three-state and
longer paired comparisons supersede them and pass. Two new probe authoring errors (redeclaring
a loop index and assigning a JSON object through struct-member syntax) were
corrected in the harness; they were not product failures. The iteration capture
snapshot control preserves the original value `2`, verified by the original
runtime, rather than inventing new capture semantics.

## Final cross-campaign outcome

[Complete 42-family comparison](final/comparison.md) gives original CP49 →
first-wave final → second-wave final instructions, CPU, wall, allocations,
allocated bytes and peak RSS for every family. [Raw metrics](final/three-state.json),
[longer CPU controls](final/long-cpu.json), [dispatch counters](final/dispatch-counters.json)
and [value lifecycle counters](final/value-counters.json) provide the evidence.
Value counters use an isolated instrumented layout; they diagnose churn, not
production timing. All final measurements use the corrected alias guard.

| Family | Wave-2 instructions | Wave-2 allocations | Longer CPU before → after |
|---|---:|---:|---:|
| Noarg calls | −12.99% | −41.41% | 38.85 → 34.94 ms |
| Scalar calls | −26.34% | −43.08% | 70.75 → 48.16 ms |
| Multi-argument calls | −16.97% | −29.74% | 125.04 → 101.09 ms |
| Recursive calls | −10.92% | −31.12% | 307.36 → 265.39 ms |
| Callback calls | −11.65% | −22.30% | 434.95 → 378.89 ms |
| Aggregate sort | −50.35% | −15.01% | 241.02 → 155.51 ms |
| Multikey aggregate sort | See complete table | See complete table | 529.08 → 246.18 ms |
| Unique / repeated grouping | −52.04% / −79.65% | −12.07% / −29.36% | See complete table |
| BFS | −15.86% | −19.80% | See complete table |
| Identity sort | −0.02% | −0.01% | Remains stubborn |

Long call controls use N=50,000; aggregate sort controls use N=16,000 and 14
post-warmup rotating samples. Short closure/identity-sort CPU medians fluctuate;
do not infer a broad speedup for those families. Loop instructions are −0.12%
and allocations unchanged. A stronger [same-path million-iteration control](final/loop-control.json)
with 21 paired post-warmup samples gives CPU 399.99 → 398.23 ms and identical
396 ms built-in medians: neutral. Few-KiB RSS differences are noise; full measured
RSS/bytes and small unrelated instruction changes remain visible in the table.

Identity sort still creates 2,000 callable instances and copies 10,000 captured
bindings despite a cached prepared body; this tranche does not solve its factory/
capture/frame overhead. Calls still allocate scopes and parameter slots and
perform live lookup. Variable-held closures retain costly outer compatibility
dispatch. Frequency/map-set, sliding-window and filesystem have limited additional
wave-2 gains; their canonical lookup/index/path/value work remains. BFS benefits
from shared call/key improvements but retains queue, root/path and value churn.
JSON traversal/transform retains aggregate ownership and wide-object linear
lookup/storage costs. No updated Python/Node ratio is asserted from local probes.

## Per-change isolated evidence

Every retained change belongs to runtime commit `3c0a5b22c24da50f749e1f385d84d52ce1a4543d`.
The ranked table records root cause, targets and safety constraints. Paired files
below contain exact before/after instruction and repeated CPU/wall measurements;
the final three-state table supplies allocation/bytes/RSS and mixed controls for
the complete corrected implementation. Early bare/outcome experiment files
predate the alias correction and are historical isolation evidence only, not
final safety certification.

### Prepared defining-path ownership — KEEP

[Paired evidence](final/path2-paired.json). Instructions and CPU ms below are isolated
preceding candidate → candidate, rather than total campaign savings.

| Probe | Instructions before → after | CPU ms before → after |
|---|---:|---:|
| call-noarg | 21112324 → 18366719 | 4.007 → 4.133 |
| call-scalar | 33193833 → 30448349 | 6.479 → 6.888 |
| call-multi | 56947224 → 54201755 | 7.272 → 6.811 |
| call-recursion | 125477518 → 114534713 | 14.067 → 15.131 |
| call-closure | 152290802 → 152367899 | 17.635 → 17.852 |
| call-callback | 178203574 → 175570750 | 18.041 → 20.110 |
| loops | 12966018 → 12952071 | 3.269 → 3.085 |
| map-identity | 28131452 → 28260398 | 5.426 → 5.791 |
| sort-identity | 62000405 → 62142605 | 15.718 → 15.150 |
| empty | 2780649 → 2781857 | 1.617 → 1.691 |

### Sort private snapshot/capacities — KEEP

[Paired evidence](final/sort-extra-paired.json). Instructions and CPU ms below are isolated
preceding candidate → candidate, rather than total campaign savings.

| Probe | Instructions before → after | CPU ms before → after |
|---|---:|---:|
| sort-aggregate | 159464388 → 155156244 | 25.789 → 24.803 |
| sort-multi | 461860527 → 457570570 | 54.367 → 53.986 |

### Bare argument guard: deferred location scratch — KEEP

[Paired evidence](final/location-scratch-paired.json). Instructions and CPU ms below are isolated
preceding candidate → candidate, rather than total campaign savings.

| Probe | Instructions before → after | CPU ms before → after |
|---|---:|---:|
| call-noarg | 18370796 → 18370645 | 3.600 → 3.577 |
| call-scalar | 24796173 → 24414051 | 4.961 → 4.859 |
| call-multi | 48329693 → 47247397 | 5.994 → 6.678 |
| call-recursion | 113171577 → 111771955 | 12.292 → 15.093 |
| call-callback | 158485526 → 157447653 | 15.917 → 16.220 |
| call-imported | 29902265 → 29519994 | 5.170 → 4.591 |
| call-aggregate | 33998260 → 33652366 | 6.601 → 5.232 |
| loops | 12951668 → 12915130 | 3.054 → 2.895 |

### Owned argument outcome transfer — KEEP

[Paired evidence](final/outcome-paired.json). Instructions and CPU ms below are isolated
preceding candidate → candidate, rather than total campaign savings.

| Probe | Instructions before → after | CPU ms before → after |
|---|---:|---:|
| call-noarg | 18366671 → 18370731 | 3.391 → 3.806 |
| call-scalar | 25142187 → 24792101 | 4.647 → 4.576 |
| call-multi | 48911707 → 48329628 | 8.292 → 8.847 |
| call-recursion | 114578610 → 113180127 | 15.730 → 13.622 |
| call-aggregate | 36406530 → 33994546 | 5.731 → 6.591 |
| call-imported | 30248239 → 29898736 | 4.494 → 5.825 |
| map-set | 34634407 → 34634488 | 7.544 → 7.040 |
| frequency | 47858694 → 46730958 | 8.435 → 6.175 |
| bfs | 101465792 → 100640339 | 12.759 → 13.116 |
| loops | 12951606 → 12951651 | 2.885 → 2.964 |

### Prepared canonical fingerprint reuse — KEEP

[Paired evidence](final/read-key-paired.json). Instructions and CPU ms below are isolated
preceding candidate → candidate, rather than total campaign savings.

| Probe | Instructions before → after | CPU ms before → after |
|---|---:|---:|
| frequency | 46730958 → 46721099 | 6.607 → 7.970 |
| bfs | 100640339 → 94806982 | 12.079 → 11.783 |
| bfs-set-visit | 43431699 → 40513030 | 6.509 → 6.223 |
| map-set | 34634488 → 34634501 | 6.884 → 8.320 |
| loops | 12951651 → 12951675 | 2.986 → 2.982 |

### Live numeric object member plan — KEEP

[Paired evidence](final/member-paired.json). Instructions and CPU ms below are isolated
preceding candidate → candidate, rather than total campaign savings.

| Probe | Instructions before → after | CPU ms before → after |
|---|---:|---:|
| group-unique | 146166617 → 72535043 | 27.428 → 20.345 |
| group-repeated | 336027602 → 70832465 | 45.849 → 20.342 |
| sort-aggregate | 155160332 → 81583487 | 25.476 → 17.627 |
| sort-multi | 457530547 → 124454711 | 56.412 → 22.307 |
| map-aggregate | 63040181 → 63041112 | 12.855 → 13.187 |
| map-identity | 28260441 → 28260427 | 5.232 → 5.385 |
| sort-identity | 62084489 → 62084475 | 11.683 → 11.361 |
| loops | 12951675 → 12952140 | 2.800 → 2.791 |

### Direct callback argument snapshots — KEEP

[Paired evidence](final/callback-args-paired.json). Instructions and CPU ms below are isolated
preceding candidate → candidate, rather than total campaign savings.

| Probe | Instructions before → after | CPU ms before → after |
|---|---:|---:|
| map-aggregate | 63040163 → 60541049 | 12.004 → 12.838 |
| filter-aggregate | 68378645 → 65906498 | 13.355 → 15.298 |
| group-unique | 72535232 → 70036174 | 16.619 → 15.964 |
| group-repeated | 70833044 → 68333904 | 15.695 → 15.505 |
| sort-aggregate | 81587879 → 79124724 | 18.087 → 17.092 |
| sort-multi | 124460415 → 119537899 | 23.042 → 22.048 |
| map-identity | 28225218 → 28129639 | 5.648 → 5.430 |
| sort-identity | 62084077 → 61993743 | 10.257 → 11.677 |
| loops | 12915180 → 12950357 | 2.770 → 3.051 |


## Hosted acceptance follow-up

The accepted runtime and closeout were pushed normally to `02e3793`. All ten
relevant non-release hosted workflows passed at that exact SHA, including Deep
Guards, Test Integrity/bindings, NRS 93/93, PRS 12/12, performance and cross-platform
checks. **CP49 WAVE 2: CLOSED.** See the [run identities and next design-only
investigation](../cp49-identity/report.md). No official rerun or release run occurred.
