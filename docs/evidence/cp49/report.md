# NIFT v4.9 — PERFORMANCE INVESTIGATION + ROADMAP RESET

CP49-0/1/2 complete locally; STOP for review. No runtime optimization implemented.

## Baseline and scope

Start HEAD and origin/main: `9de5c3e3e62291c929702271b34f2c870ec2a442`, clean, 0/0. Public v4.8.0 target remains `1bfc4a54b373da3477b497106fa4db59111e2770`; development version 4.9.0, ABI 1.3. NRS `34b1c2ff4d1a3f591176f3ce79b74ad2958b6852` passed 93/93; PRS `1da43659da269c96af21aea7234799d2ad56b2df` passed 12/12 on the development binary. Neither suite needed edits.

Roadmap reconciliation commit `ba09953`: HANDOVER, ROADMAP, DECISIONS, DEVELOPMENT, ARCHITECTURE and PERFORMANCE now distinguish released v4.8, development v4.9, closed campaigns and evidence-driven CP49-0/1/2. Corrected v4.5 release-candidate roadmap and v4.0.3 ACTIVE campaign label; the final reconciliation explicitly labels the obsolete August non-AST/interpolation architecture snapshot historical and supplies the current Parser/Ast/value/source-origin map. Historical records remain retained. ReleaseNotes retains v4.8 history and records binding-only maintenance. No runtime redesign selected. Explicit FFI release API, further rewrite/redesign dogfooding, formal binding publication/support reassessment, fresh formal benchmarks/lab update and profiler-justified capture/storage work remain deferred.

Binding maintenance commit `2d83767`: Go, C#, Node, Python and aggregate builds/tests PASS; strict GCC/Clang native wrappers PASS. See [binding report](bindings.md). No public API or ABI changes. This local checkpoint is not new cross-platform binding certification or publication.

Frozen oracle `20261008-v480`: UNCHANGED. 2,828 fingerprinted campaign/workload/results files unchanged; benchmark source repository clean at `2ad0eb6b0760ec4adce46ef99dbdf29542758caa`. Official rerun NO; provisioned nodes NO; presentation/workloads changed NO. All probes are separate local Nift-core investigation fixtures. The frozen workload named **sort-index actually uses `sort_by(x => x)`**. This report separately labels identity sorting and indexed-selector sorting; it does not rename or modify the oracle.

## Method and limitations

Current optimized CLI: C++17, GCC `-O2 -Wall -Wextra -pedantic -pthread`, unchanged core source since `9de5c3e`; binary SHA-256 `f8da5147504226c0353559de11b9366820ba6064d07aced5e8ca366b49e7679e`. 105 output-checked local probes (26 families × four sizes plus empty process), N=2,000 / 4,000 / 8,000 / 16,000. Three recorded repetitions after one warmup; randomized order; wall time, in-script timer phase and process CPU (getrusage child user+system) retained. RSS collected in the first native pass. Timer phases exclude setup where indicated but have millisecond resolution. These timings are local diagnostics, not portable benchmarks or comparisons with Python.

Initial wall timings were noisy/non-monotonic and are preserved in `.build/cp49/timings.json`; do not derive complexity or official gains from them. CPU timings reduce scheduler interference but remain variable. Prefer deterministic instruction scaling for complexity conclusions. Hardware perf events were denied by host policy (`perf_event_paranoid=4`); no host policy was changed. Existing Callgrind 3.26 supplies instruction counts and self/inclusive call paths, not hardware CPU samples. Memcheck supplies allocation/free/byte counts. Recursive inclusive counters can exceed 100% and overlap: never sum them or call them exclusive wall percentages. Optimized symbols are available; most first-party source-line debug data is absent. Copies/moves are indicated by symbols and isolated copy probes, not exact custom copy counters.

Reproduction (run from Nift root after `make -j2`; requires the existing Valgrind tools and supported native toolchain; no official harness involved):

```sh
python3 docs/evidence/cp49/tools/generate_probes.py
python3 docs/evidence/cp49/tools/profile.py
python3 docs/evidence/cp49/tools/memory.py
python3 docs/evidence/cp49/tools/scale_instructions.py
python3 docs/evidence/cp49/tools/cpu_timings.py
python3 docs/evidence/cp49/tools/build_counter.py
strace -c -o .build/cp49/filesystem-syscalls.txt ./nift .build/cp49/probes/filesystem-2000.f
g++ -std=c++17 -O2 -Wall -Wextra -Werror -I. docs/evidence/cp49/tools/json_stages.cpp src/RuntimeValue.o -o .build/cp49/json-stages-cpu
g++ -std=c++17 -O2 -Wall -Wextra -Werror docs/evidence/cp49/tools/path_sort_probe.cpp -o .build/cp49/path-sort-probe
```

The counter binary recompiles only ParserExpression.cpp with existing `NIFT_TEST_LAMBDA_CACHE_STATS`; links the current other objects. Exact build command vectors are in counter-build.json. Production binary/source is untouched. Raw Callgrind self/inclusive logs, memcheck, native samples, syscall trace, counters and first-pass failures are retained in `.build/cp49/`. Corrected probe errors (redeclaration/string typing/string size) were harness errors, not runtime fixes.

## Native baseline: process CPU milliseconds

Numbers include startup and setup; phase/wall samples are in cpu-timings.json. BFS is an independent two-forward-edge chain; filesystem is a 100-files-per-directory tree. JSON uses two-field records. These do not reproduce every frozen workload shape.

| Local probe | N | 2N | 4N | 8N |
|---|---:|---:|---:|---:|
| array-build | 3.92 | 6.11 | 8.69 | 15.60 |
| array-index | 7.13 | 8.23 | 13.36 | 25.30 |
| bfs | 23.92 | 38.80 | 110.68 | 218.86 |
| call-callback | 29.13 | 52.50 | 102.55 | 224.34 |
| call-closure | 34.92 | 53.29 | 100.14 | 180.47 |
| call-locals | 7.43 | 17.56 | 26.82 | 49.50 |
| call-multi | 15.43 | 19.54 | 50.78 | 81.26 |
| call-noarg | 6.65 | 9.50 | 13.81 | 30.31 |
| call-recursion | 21.24 | 47.56 | 70.85 | 147.79 |
| call-scalar | 7.51 | 19.65 | 23.98 | 47.05 |
| construct-destroy | 5.92 | 10.19 | 14.80 | 21.85 |
| filesystem | 47.93 | 86.24 | 182.19 | 336.50 |
| frequency | 18.74 | 32.11 | 59.14 | 116.64 |
| json-mutate | 12.14 | 27.72 | 51.95 | 102.35 |
| json-parse-convert | 6.23 | 12.25 | 24.50 | 36.48 |
| json-serialize | 11.03 | 20.17 | 40.37 | 78.33 |
| json-traverse | 10.09 | 17.05 | 34.17 | 80.65 |
| loops | 5.15 | 6.05 | 10.21 | 13.23 |
| map-arithmetic | 15.04 | 22.92 | 40.36 | 76.23 |
| map-identity | 12.52 | 21.17 | 36.69 | 67.27 |
| map-index | 29.34 | 56.85 | 106.99 | 208.37 |
| map-set | 10.55 | 14.95 | 32.35 | 61.62 |
| sliding-window | 7.75 | 13.88 | 20.94 | 33.60 |
| sort-arithmetic | 25.63 | 46.04 | 102.67 | 177.93 |
| sort-identity | 18.95 | 50.97 | 110.25 | 261.21 |
| sort-index | 34.34 | 63.80 | 166.39 | 388.44 |

## Deterministic scaling: millions of instructions

Counts include startup/setup; fixed overhead makes small-size linear ratios below 2. Sort-index here means the separate captured-array selector. All measured families show linear or modest N log N growth, not ratios approaching 4. This disproves a quadratic hypothesis only within these sizes/shapes; it is not a universal complexity certification.

| Probe | N | 2N | 4N | 8N | successive ratios |
|---|---:|---:|---:|---:|---|
| bfs | 111.87 | 219.61 | 435.13 | 867.60 | 1.963, 1.981, 1.994 |
| call-closure | 152.12 | 300.93 | 598.54 | 1193.77 | 1.978, 1.989, 1.994 |
| call-scalar | 33.16 | 63.16 | 123.16 | 243.16 | 1.905, 1.950, 1.974 |
| filesystem | 207.81 | 433.74 | 916.20 | 1878.55 | 2.087, 2.112, 2.050 |
| frequency | 58.20 | 111.29 | 217.47 | 429.83 | 1.912, 1.954, 1.977 |
| json-mutate | 40.81 | 78.46 | 153.61 | 308.71 | 1.922, 1.958, 2.010 |
| json-parse-convert | 12.85 | 22.65 | 42.25 | 81.31 | 1.763, 1.865, 1.924 |
| loops | 12.93 | 22.76 | 42.43 | 81.76 | 1.760, 1.864, 1.927 |
| sort-arithmetic | 67.47 | 132.76 | 262.61 | 527.75 | 1.968, 1.978, 2.010 |
| sort-identity | 64.10 | 126.05 | 249.20 | 500.90 | 1.966, 1.977, 2.010 |
| sort-index | 143.40 | 284.84 | 566.97 | 1136.15 | 1.986, 1.991, 2.004 |

## Allocation and profiles

Whole-process N=2,000 counts include setup/destruction. Empty process: 323 allocations, 121,028 bytes. All focused Memcheck probes finish with zero live heap blocks and zero reported errors; this is not a full sanitizer/lifetime certification.

| Probe | allocations | cumulative allocated bytes | instructions (millions) |
|---|---:|---:|---:|
| loops | 4,435 | 803,412 | 12.93 |
| call-noarg | 14,479 | 2,364,137 | 21.08 |
| call-scalar | 32,489 | 4,673,497 | 33.16 |
| call-multi | 60,503 | 10,079,688 | 56.91 |
| call-locals | 44,516 | 6,024,538 | 41.63 |
| call-recursion | 128,530 | 20,350,073 | 125.48 |
| call-closure | 92,527 | 9,881,050 | 152.12 |
| call-callback | 134,539 | 15,946,735 | 178.07 |
| sort-identity | 84,567 | 11,608,079 | 64.10 |
| sort-arithmetic | 86,574 | 11,737,453 | 67.47 |
| sort-index | 98,573 | 11,971,264 | 143.40 |
| map-identity | 44,550 | 7,075,989 | 30.28 |
| map-arithmetic | 46,561 | 7,197,371 | 33.64 |
| map-index | 58,565 | 7,451,295 | 109.73 |
| frequency | 46,990 | 7,014,627 | 58.20 |
| map-set | 14,558 | 3,974,754 | 34.65 |
| sliding-window | 8,539 | 2,076,846 | 28.30 |
| bfs | 80,752 | 13,300,200 | 111.87 |
| json-parse-convert | 14,472 | 4,041,476 | 12.85 |
| json-traverse | 42,529 | 13,037,788 | 33.29 |
| json-mutate | 44,554 | 14,514,013 | 40.81 |
| json-serialize | 25,611 | 11,589,515 | 28.10 |
| construct-destroy | 16,456 | 3,062,030 | 20.43 |
| filesystem | 175,757 | 29,848,756 | 207.81 |

Sort-index: stable sorting accounts for 3.84% inclusive instructions, scalar numeric comparison 0.46%; expression-evaluation entry accounts for 84.81% inclusive. Top self paths: string append 9.09%, evaluator lambda 6.08%, string concatenation 5.84%, string construction 5.75%, memcpy 5.48%. Sort-identity stable-sort share is 8.90%, with allocation/move/destruction costs prominent. The sorting algorithm itself is not the dominant measured cost.

Simple scalar named call: RuntimeValue destruction 6.36% self, binding hash lookup 3.73%, move assignment 3.58%, free 3.23%. Closure call: append 8.53%, string construction 5.92%, evaluator 5.73%, strlen 5.54%. The indexed-selector and closure profiles share compatibility expression/string machinery. Named-call versus closure costs differ even though the numeric lambda body counter reports prepared invocations.

Frequency-count: RuntimeValue destruction 6.66% self, statement executor 4.34%, strlen 3.68%, memcmp 3.43%, move assignment 3.03%. BFS: destruction 7.25%, executor 3.49%, move assignment 3.37%, strlen 3.15%, numeric decimal parsing 3.06%. No new scalar-key linear scan is demonstrated; indexing/mutation/temporary value work remains the lead. Sliding-window/array-index controls and profiles retain nested lookup costs.

JSON mutation: destruction 7.70%, object-vector growth 6.79%, move assignment 3.78%, allocator/free each 3.52%. Parse+convert: raw object parser 6.89% self plus allocation, conversion and destruction. Top complete paths for every workload, including loops, traverse, serialize, construct/destroy and filesystem, are retained in profile-summary.json. Ordinary loops alone cost 12.93M instructions versus scalar calls 33.16M, closure calls 152.12M, sort-index 143.40M and BFS 111.87M at the same N; the delta is specific machinery, not evidence that an execution-engine rewrite is needed.

## Isolation and root-cause ranking

### 1. Prepared callable/selector coverage and repeated callback construction

**Demonstrated cause, high confidence; broad payoff, medium semantic/lifetime risk.**

ParserTemplate.cpp's AST `ctx.call` rejects callable-tag variables and async calls, then Ast.cpp falls back to source evaluation. Named synchronous functions reuse prepared bodies, but variable-held closures/callbacks traverse compatibility dispatch. NumericLambdaPlan in ParserExpression.cpp explicitly accepts binary/unary forms and rejects the identity Binding node and Index nodes. Existing counters: sort-identity 2,000 instances / 2,000 legacy / 0 prepared; indexed selector 2,000 instances / 2,000 legacy; arithmetic selector 2,000 instances / 2,000 prepared. Syntax is cached once, so this is not repeated syntax compilation.

The sort_by decoration loop invokes the source selector per element; lambda instantiation copies visible binding entries into captures and registers a new instance. Map counterparts instantiate once: identity still uses 2,000 legacy body evaluations, indexed map likewise; arithmetic map is prepared. Map-identity costs 30.28M instructions versus sort-identity 64.10M; map-index 109.73M versus sort-index 143.40M. These contrasts include decoration/sort differences and cannot attribute the full delta solely to instance creation. They do independently expose repeated instance construction and substantial indexing-body fallback.

Affected: identity/key sorting, array callbacks, first-class function calls, captured scalar/index expressions; potentially callback-heavy JSON/graph work. BFS in this probe has no callbacks, so improvement is not promised there. Recommended shape: bounded typed synchronous callable dispatch and narrow reusable selector plans with compatibility fallback. Do not simply hoist all captured lambdas: fresh capture timing, shadowing, mutations, location roots and escape behavior must remain correct. No numeric or array specialization keyed to a benchmark name.

Expected payoff: substantial in demonstrated fallback-heavy cases, unquantified end-to-end; not a claim of recovering the frozen Python ratios. Required tests: argument side effects/order, recursion, captures and shadowing, callable returns/escape, imported/module calls, aliases/root+path, mutation during callbacks, async refusal/fallback, exception and original-source diagnostics, types/arity and cache bounds. Permanent guards: typed dispatch/fallback and lambda-instance counters for eligible fixtures, correctness oracles and relative scaling. Safety: ASan/UBSan/LSan and relevant lifetime/ownership/concurrency gates; independent NRS and supported Linux/macOS/Windows.

### 2. Function frame, binding and temporary-value churn

**High confidence cost; medium confidence specific remedy; widest breadth, higher ownership risk.**

Scalar named calls allocate roughly 16 times per iteration above startup versus loops roughly 2; closure calls roughly 46. `ParserTemplate.cpp` prepared-call execution pushes unordered-map scopes and heap-backed parameter RuntimeValues; expression contexts and value propagation copy/move/destruct temporaries. RuntimeValue is value-owned for arrays/objects and location references are synchronized through roots/paths (Parser.cpp resolve_direct); bypassing this would invalidate v4.8 safety guarantees.

Affected: named/recursive functions, callbacks, map/set operations, BFS, JSON traversal/mutation and ordinary evaluator operations. Proposed shape: isolate parameter allocation versus scope/hash cost with additional counters, then reserve known frame sizes or reduce duplicate temporary adapters. A reusable/stack frame is not approved: escaping closures, references and async ownership may require storage beyond the call. No object-storage redesign.

Payoff: broad but cannot be computed from destruction self-time alone. Test simple/noarg/multi/local/recursive/captured functions, returns/escape, error cleanup, concurrent/async calls and aliasing. Counter guards should demonstrate reduced allocations without changed output; scaling alone will not catch linear constant-factor regressions. Run full relevant safety gates and NRS before accepting any storage change.

### 3. Repeated filesystem path serialization during ordering

**Proven structural cost; high confidence, low semantic/lifetime risk; narrower but directly product-relevant.**

`ParserHelpers.cpp` glob_walk directory comparators and glob_expand final comparator call `generic_string()` repeatedly. Filesystem profile: glob_expand 94.21% inclusive; final introsort 70.39% inclusive. Generic-string conversion 25.21% self, append 21.53%, memcpy 10.57%. Trace: 2,058 newfstatat and 84 getdents64 calls; trace syscall timings are instrumentation-dependent, not directly subtractable from native wall time. The dominant instruction paths are Nift/C++ path/string work around traversal, not a demonstrated syscall-only problem.

A separate C++ reproducer compares current generic-string comparison with one precomputed key per path; outputs are identical. N=16,000: 552,550 conversions versus 16,000; sort CPU 72.13ms versus 21.17ms (~3.4×). This includes cached-key construction but not total product traversal, and is not a production patch. Smaller sizes and data retained in path-sort-isolation.json.

Proposed shape: retain the identical generic-string ordering with cached keys for the existing sort boundaries; account for key-storage peak RSS. Tests: deterministic ordering/deduplication, Unicode/escaping, absolute/relative paths, glob wildcards/hidden files/symlinks, missing/unreadable directories, containment and Windows separators. Guard conversion counts O(N) for ordering plus N log N instruction/time scaling; no machine-specific timing promise. ASan/UBSan/LSan and cross-platform filesystem contracts.

### 4. JSON conversion/object and collection-value pipeline

**Measured cost, medium confidence remedy; medium/high aliasing risk.**

Native 20-repeat process-CPU probes separate raw canonical Jsonic++ parse+destroy, runtime_from_json conversion+destroy, serialization (runtime_to_json+dump) and RuntimeValue copy+destroy. N=16,000: parse 9.96ms, conversion 9.91ms, serialization 7.13ms, copy 3.35ms. No stage disappears; isolated conversion costs are material. Nift traverse/mutate add much larger evaluator/location/mutation costs than raw parse alone. This two-field shape does not establish a wide-object lookup complexity defect, and exact stage subtraction from different processes is invalid.

Affected: JSON parse/transform/traverse, object construction/mutation, collections and BFS value machinery. First seek duplicate conversions/materializations and temporary propagation in Nift, not a vendored parser patch. Raw Jsonic++ is not demonstrated to dominate these mixed pipelines; no upstream component change is proposed. If later raw-parser profiling becomes dominant, stop and handle it in canonical Jsonic++.

Expected payoff unquantified; protect exact-number/StrNumber, object ordering, nested alias/location mutation, serialization errors and destruction. Add wide/deep JSON isolation before any data-structure proposal. Safety: native value/JSON contracts, malformed/deep input, ASan/UBSan/LSan, Memcheck where practical, external contracts and component synchronization.

## Recommended attack order and guard strategy

1. **CP49-3 proposal:** narrow prepared scalar identity-selector support and typed synchronous callable dispatch, starting with independently specified compatibility/side-effect/diagnostic contracts. Separate instance-construction hoisting from body dispatch; do not combine speculative frame storage changes. Re-profile both identity sort and indexed selector; cross-check calls, callbacks, collections and BFS.
2. Cached glob ordering keys is a small, well-isolated **candidate safe to implement after authorization**; can be a separate product-focused checkpoint. No production code implemented here.
3. Parameter/scope allocation and duplicate value-adapter investigation, then only the measured bounded fix.
4. JSON conversion/materialization fixes if further isolation justifies them; revisit remaining primitive dispatch/hash/path costs from new profiles.

Root-cause priority and low-risk implementation order need not be identical. No general speedup estimate is promised from local instruction shares. There is no evidenced reason yet for bytecode/JIT, giant execution-engine replacement, RuntimeValue.object redesign, closure/storage redesign or renewed scalar-key scanning work.

Use relative N/2N/4N/8N guards with warmup, repeated paired cases and broad noise tolerance for expected linear/N log N paths; avoid fixed wall limits. Prefer deterministic operation/fallback/instance/allocation counts where feasible. Preserve behavioral contracts before speed claims. Future candidate fixes must improve their isolated general abstraction and naturally cross-check representative mixed workloads; failure to improve BFS means inspect its distinct primitive/iteration costs, not add a BFS shortcut. Full formal results remain frozen; later official evaluation requires a new series.

## Stopping state

Only documentation reconciliation and binding warning maintenance implementation commits exist. Core source/public headers unchanged from the 4.9 bump. Profiling tools/evidence are retained separately; no performance optimization commit, feature, API change, package publication or official rerun. Commits are local; no new push requested for this review checkpoint. Review this ranking before authorizing CP49-3 or any larger optimization campaign.
