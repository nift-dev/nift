# NIFT v4.9 performance campaign

The user accepted CP49-0/1/2 and authorized the bounded campaign, accepted
checkpoint commits, normal pushes and non-release certification on 2026-10-08.
Starting accepted source: e291575f004a601a7d9cbb645ec7810e09bdc325; original
runtime binary SHA256 f8da5147504226c0353559de11b9366820ba6064d07aced5e8ca366b49e7679e.
Development version 4.9.0; ABI 1.3. Frozen official series 20261008-v480 remains
an immutable reference; all probes here are independent local diagnostics.

## CP49-3A: live numeric identity binding

A pure numeric binding in an expression lambda previously re-entered the whole
compatibility expression evaluator. The shared callback-body helper now reads
its canonical live variable scope slot after synchronization. Named function
precedence, nonnumeric/unresolved bindings and excessive expression depth retain
compatibility dispatch. No values, scopes, references or origins are cached.
The result remains a value copy; return-location propagation is unchanged.

At N=2,000, exact paired Callgrind totals (whole process) changed:

| Probe | Before | After | Delta |
|---|---:|---:|---:|
| identity map | 30,313,041 | 28,157,102 | -7.11% |
| identity sort | 64,116,878 | 62,042,922 | -3.23% |
| scalar call control | 33,194,651 | 33,194,672 | +0.0001% |
| BFS control | 111,872,138 | 111,870,131 | -0.0018% |
| loop control | 12,967,714 | 12,966,044 | -0.0129% |
| arithmetic sort control | 67,481,342 | 67,487,367 | +0.0089% |
| indexed sort control | 143,381,666 | 143,397,709 | +0.0112% |

Ordinary identity calls at N=16,000: 1,094,046,890 → 1,076,894,914
instructions (-1.57%). This is a shared general abstraction improvement.
Seven interleaved recorded samples plus warm-up at N=2,000 give identity-map
CPU 13.26 → 11.74 ms (-11.5%); sort 28.35 → 28.18 ms (effectively neutral).
An earlier independent run gave map 9.49 → 7.62 ms and sort 27.74 → 24.73 ms.
Host/compiler load is variable: do not interpret tiny CPU changes or claim a
portable sort wall-time speedup. Deterministic instruction reductions reproduce.
The ordinary-call timing medians 226.35 → 217.71 ms are likewise noisy.

Memcheck: allocation counts unchanged (map 44,550; sort 84,567; scalar call
32,489; BFS 80,752), zero errors and zero live heap blocks. Cumulative allocated
bytes differ by only three CLI-path bytes. This patch eliminates dispatch work,
not frame allocations. N=16,000 peak RSS samples overlap except map medians
16,676 → 16,852 KiB (+176 KiB, about 1%); no data representation or allocation
change explains a systematic increase. Raw paired data are in [3a](3a/).

Correctness: 27 explicit selector/capture/location/error contracts preserve exact
baseline status/stdout/stderr; existing callback compatibility matrix and 470
numeric/fallback parity pairs pass. Deterministic counter guard proves identity
map/sort execute 100,000 prepared bodies with zero legacy calls, while nonnumeric
values still fall back; existing cache bounds and arithmetic guards pass.
Safety certification and acceptance are recorded as each checkpoint completes.

CP49-3A decision: **KEEP**. Focused ASan/UBSan/LSan selector (27 cases),
callback compatibility matrix and location receiver synchronization checks pass.
The first lifetime build contains the identity branch before its final depth
eligibility narrowing; final optimized parity/counters cover that narrowing.
Final campaign sanitizer certification will rebuild the exact final source.
Memcheck zero-error/all-freed evidence independently covers the final optimized
candidate. No concurrency machinery was changed.

## CP49-3B: syntax-only narrow indexed selector

**KEEP**. The bounded existing expression-plan cache recognizes only a Binding
root and a numeric Literal/Binding index. Invocation resolves canonical live
scope slots after synchronization, checks numeric size and array bounds, then
copies the selected value. It never caches runtime values, locations or scopes.
There is no intervening effectful evaluation. Nonnumeric roots, object indexing,
missing bindings, named function collisions, negative/fractional/oversized
indices and other failures use unchanged compatibility evaluation and origins.
The depth eligibility includes both child nodes. Existing alias/return-location
propagation remains untouched.

N=2,000, immediately preceding accepted binary vs candidate:

| Probe | Instructions before | After | Delta | CPU medians ms before / after |
|---|---:|---:|---:|---:|
| map-index | 109,747,365 | 28,967,693 | -73.60% | 20.75 / 8.73 |
| sort-index | 143,397,719 | 62,663,199 | -56.30% | 27.37 / 16.91 |
| identity map control | 28,156,923 | 28,157,093 | +0.0006% | 5.72 / 5.83 |
| arithmetic sort control | 67,475,185 | 67,477,369 | +0.0032% | 18.99 / 17.34 |
| scalar call control | 33,194,497 | 33,194,672 | +0.0005% | 7.05 / 6.45 |
| BFS control | 111,872,211 | 111,862,135 | -0.0090% | 31.54 / 21.39 |
| loop control | 12,967,759 | 12,966,044 | -0.0132% | 5.58 / 5.25 |
| JSON traverse control | 33,285,789 | 33,286,666 | +0.0026% | 9.75 / 9.93 |

CPU is seven interleaved recorded samples plus warm-up, affected by variable
host/compiler load. Control timing shifts without corresponding instruction
shifts illustrate that noise; target instruction reductions are decisive.
Memcheck allocation counts: map 58,569 → 44,570 (-23.90%); sort 98,577 →
84,578 (-14.20%), each saving 13,999 allocations and 385,975 cumulative bytes.
Identity-map/BFS allocations unchanged. All four probes: zero errors/all heap
freed. N=16,000 RSS sample distributions overlap: map medians 16,444 →
16,472 KiB; sort 51,204 → 51,280 KiB. No meaningful retained memory growth.

Correctness: 30 explicit contracts with exact original-baseline status/stdout/
stderr; 160 extra A/B type/index/result/error combinations; 470 existing numeric
pairs and full callback matrix pass. Counters prove 100,000 prepared indexed
map/sort invocations, zero legacy bodies, with unchanged bounded-cache/fallback
contracts. Exact current lifetime-sanitized build passes the 30 contracts,
location receiver guard and full callback matrix (including module ownership,
escaped captures and async worker behavior), ASan/UBSan/LSan enabled. Data in
[3b](3b/).

## CP49-5 / CP49-13: cached exact filesystem ordering keys

**KEEP**. A shared internal decoration helper computes the exact existing
`path.generic_string()` once per entry, sorts those keys, then moves the original
paths/directory entries back. Both glob_walk directory ordering boundaries and
glob_expand final ordering use it. Traversal, hidden/symlink policy, permission
handling, normalized paths, and final `std::unique(path)` are unchanged. Empty or
singleton vectors require no ordering conversion. No native/case/locale ordering
substitution. Memory is bounded O(N) and transient within each ordering boundary.

N=2,000 whole traversal: 207,839,261 → 82,149,896 instructions (-60.47%);
seven paired CPU medians 25.01 → 17.71 ms (-29.19%). Control instruction changes
are all below 0.02% (loops, scalar call, BFS, JSON serialize, indexed map).
Control CPU variation has no corresponding instruction increase; do not claim
those tiny changes as gains/regressions. Memcheck allocations 175,761 → 69,422
(-60.50%); cumulative bytes 29,851,364 → 22,454,838 (-24.78%); zero errors,
all heap freed. N=16,000 RSS medians 25,072 → 25,408 KiB (+336 KiB, 1.34%)
with overlapping ranges. This small bounded tradeoff is accepted for the much
larger instruction/allocation reduction.

Deterministic test-only instrumentation (not compiled into ordinary CLI/ABI)
requires exactly 2N conversions for flat N-file probes at N=100/200/400: one
per directory entry plus one per final result. New eight-pattern exact A/B
contracts cover Unicode/spaces, relative and absolute paths, recursive wildcard
ordering, repeated-recursion dedup, hidden entries, symlinks and missing dirs;
existing glob/copy/remove smoke passes. `ls` symlink rendering can produce
identical displayed targets after glob dedup; this existing behavior is retained.
Exact current ASan/UBSan/LSan lifetime build passes both new and existing glob
corpora. Existing path safety/containment and unreadable-source gates remain
required in final certification, including hosted Windows path behavior.

strace confirms unchanged dominant syscall counts: 2,058 newfstatat and 84
getdents64 (same baseline). Remaining traversal includes real filesystem work;
no syscall-count reduction is claimed. strace instrumentation timing is not
native wall time. Data in [5](5/).

## CP49-3C / CP49-4: dispatch/construction investigation

**NO CHANGE / DEFERRED**. Typed variable-call dispatch currently deliberately
falls back to the canonical source-aware call path. That path binds argument
locations, handles named-module/struct/lambda/async ownership, propagates return
locations, performs argument/spread evaluation, and appends diagnostic frames.
Copying it into AST Context.call would duplicate semantics; exposing a shared
canonical typed call adapter requires a broader design/review than the bounded
selector fix. No compatibility removal or async change attempted.

Callback syntax/plans are already reused. Fresh sort instances remain observable
through selector factory effects (existing multiple_selectors contract requires
six factory calls for three values/two selectors), captures, identity and escaped
closures. No global hoist attempted. Restricted instance reuse requires a
separate eligibility proof and retained payoff after indexed dispatch; none was
established strongly enough to justify production machinery in this campaign.

## CP49-6 / CP49-7: frame isolation and rejected reservation

Allocation deltas versus loops at N=2,000: noarg calls 14,479 vs 4,435; scalar
32,489; three-arg 60,503; two-local 44,516; recursion 128,530; closure 92,527;
callback 134,539. Debug-line Memcheck allocation-tree investigation retains
raw evidence in `.build/cp49-campaign/frames/`; it separates parameter scope/
shared RuntimeValue+slot allocation, argument AST work and SourceContext path
copies. Recursive allocation-tree inclusive totals overlap and cannot be summed.
Scalar prepared Context.call's source-path copying alone accounts for roughly
1.16 MB/6,009 inclusive blocks, showing frame cost is not only unordered_map.
Noarg vs scalar/multi isolates significant parameter/argument churn; locals also
add costs beyond initial parameter binding. No reusable-frame/arena/alias design
was introduced.

Experiment: reserve exactly known parameter/variadic capacity in prepared named
function scopes. Existing callable and location parity pass. **DROP, reverted
YES**: instruction work increased (scalar +0.18%, multi +0.10%, locals +1.37%,
recursion +0.19%); no reproducible execution improvement. Tiny CPU shifts with
unchanged controls are host noise. Smaller bucket storage alone does not earn
this tradeoff; locals can subsequently rehash the undersized map. Original
scope implementation restored and rebuilt before subsequent work. Paired data
in [7](7/). No failed production code retained; no sanitizer claim made for this
dropped experiment.

## CP49-8: move owned collection results

**KEEP**. Three canonical array/collection callback paths copied each map result
into its result vector, then deep-copied the complete owned map/filter result
again into output. These temporaries have no subsequent use; move them instead.
Copying source elements, argument binding, callback execution and external value/
identity/location semantics are unchanged. No reserve/representation/alias change
is bundled with this experiment.

Independent mixed aggregate records at N=2,000: map 69,404,060 → 62,900,344
instructions (-9.37%), CPU medians 16.10 → 12.93 ms (-19.69%); filter
72,489,527 → 68,238,392 (-5.86%), CPU 17.97 → 16.15 ms (-10.13%).
Scalar identity/index/arithmetic map instruction savings are only about 0.1%;
BFS/loops/JSON mutate/scalar call instruction differences below 0.02%. The
arithmetic-map CPU increase 6.81 → 8.42 ms is not matched by instructions or
extra allocations; final paired cross-workload reprofile is required to assess
such host-sensitive timings, not one sample batch.

Memcheck: aggregate map allocations 118,554 → 106,553 (-10.12%), bytes
30,677,606 → 26,341,599 (-14.13%); filter allocations 112,578 → 106,577
(-5.33%), bytes 28,674,696 → 26,354,689 (-8.09%). Both zero errors/all freed.
N=16,000 RSS medians map 81,360 → 62,768 KiB (-22.85%), filter 81,440 →
62,720 KiB (-22.99%). This removes peak simultaneous ownership of deep copies.

33 exact original-baseline contracts now include mapped/filtered aggregate
independence and returned callable captures; 470 numeric parity pairs, callback
matrix and deterministic counters pass. Exact current ASan/UBSan/LSan lifetime
build passes 33 cases, location receiver, callback matrix and collection ops
smoke. Data in [8](8/). Extra aggregate probes are independent generated records,
not official benchmark inputs; generator retained in tools.

## CP49-10: JSON timer preflight once per conversion

**KEEP**. `runtime_to_json` recursively rescanned each subtree for timers before
conversion. Retain its whole-value timer preflight at the public boundary and
use a private recursive converter for the already-checked tree. Error priority,
ordering, number spellings, output-on-failure, bytes/Error rejection and value
semantics are unchanged; no vendored Jsonic++ code changed. Normal exported API
and ABI are unchanged; counters exist only in a dedicated native guard binary.

Independent wide objects, mixed nested arrays and 48-level chains at 2,000/
16,000 leaves were isolated into raw parse, runtime conversion, serialization
(runtime_to_json+dump) and deep copy; each stage includes destruction. Seven
paired process-CPU batches, each 20 repetitions: deep serialization 0.60250 →
0.23705 ms (-60.66%) at 2,000, 5.71430 → 2.01485 ms (-64.74%) at 16,000.
Wide serialization 0.72060 → 0.67970 ms / 9.22620 → 8.93040 ms; mixed
0.82115 → 0.76295 / 11.68900 → 11.52440 ms. Small shallow differences need
caution; the repeatable deep gain is decisive. Whole native deep-2,000 helper
instructions (all four stages, 20 repeats) 346,687,615 → 219,729,953 (-36.62%).
Raw parser/conversion/copy timings vary with host load; their implementations
are unchanged. No raw parser dominance or parser modification is inferred.

Ordinary shallow JSON serialization instructions 28,106,729 → 28,076,284
(-0.11%); parse/convert, traversal, mutation, aggregate map, BFS, loops, scalar
call and filesystem control changes all below 0.02%. Allocation counts unchanged
(25,615), bytes differ only by CLI path length; zero Memcheck errors/all freed.
N=16,000 RSS distributions overlap (40,880 → 40,916 KiB median).

Expanded native contracts first passed on the unmodified implementation, then
on the candidate: wide/deep round-trip, timer-before-bytes/Error precedence,
unchanged output on failure, cleared error on success. Exact current ASan/UBSan/
LSan native unit and lifetime CLI timer/Error characterization pass. Deterministic
guard proves one preflight and exactly N+49 conversions for depth-48 arrays at
N=100/200/400. Native stage columns in paired-stages.json are parse, conversion,
serialization and copy CPU milliseconds; these are local diagnostics, not an
external language benchmark. Data in [10](10/), shape generator retained in tools.

## CP49-9: construction-only array grouping index

**KEEP**. Array group_by repeatedly scanned the newly constructed ordered object
for every rendered key. A local string→position map now indexes construction;
the RuntimeValue representation remains unchanged, first occurrence controls
object ordering, and existing rendered-key collisions are preserved. Two
previously duplicated accumulation paths share this helper. The index is local
to the operation and stores positions, not pointers into reallocating vectors.
No persistent storage/index redesign or old scalar collection scan work revisited.

Unique mixed-record grouping at N=2,000: 264,919,348 → 146,031,043 instructions
(-44.88%); initial paired CPU 32.91 → 22.77 ms (-30.81%). Repeated 16-key
shape instructions 337,329,532 → 335,825,577 (-0.45%). An initial repeated-key
CPU batch regressed (49.01 → 64.32 ms), so acceptance was withheld and tested
again with compiler work finished. Twenty-one interleaved recorded samples:
unique 54.445 → 44.327 ms (-18.58%); repeated N=2,000 71.649 → 67.131 ms
(-6.31%), N=16,000 391.695 → 394.175 ms (+0.63%, neutral/noise). The first
regression did not reproduce; variable host CPU conditions are retained in data.
Frequency/map-set/sliding-window/BFS/map/loops/call/JSON instruction controls
remain within 0.02% (BFS CPU unchanged in the initial batch).

Actual scalar unique-group instruction scaling N=1,000/2,000/4,000: baseline
65.21M / 152.95M / 666.62M; candidate 18.39M / 33.77M / 64.54M. A mandatory
Callgrind guard uses a broad <2.6 doubling ratio, fails on the saved preceding
binary (second ratio 4.38), passes current (~1.83/1.91), and runs in the Linux
performance workflow after installing Valgrind. It is a specialized explicit
target, not a silent prerequisite skip on unsupported platforms; ordinary
cross-platform correctness still runs the semantic contracts.

Tradeoff: a bounded O(unique groups) temporary hash index. Unique aggregate
allocations 130,573 → 132,581 (+1.54%), cumulative bytes 31,222,524 →
31,370,509 (+0.47%); repeated allocations 156,726 → 156,744 (+18 blocks).
Both zero Memcheck errors/all heap freed. N=16,000 RSS medians unique 86,440 →
92,172 KiB (+5,732 KiB, +6.63%); repeated 84,000 → 84,000 KiB. The targeted
unique-group memory increase is accepted for eliminating superlinear scans;
unaffected/repeated controls remain neutral. No unsafe string_view/pointer key
scheme was introduced to save index memory.

37 original-baseline exact contracts now include group ordering, numeric/string/
bool rendered collisions, aggregate independence and callback effects. Existing
470 numeric parity pairs/callback matrix/counters pass. Exact-current lifetime
ASan/UBSan/LSan passes 37 cases, collections and callback module/capture/async
matrix. Data in [9](9/).
