# NIFT v4.10 — SCRIPTING COMPETITIVENESS INVESTIGATION

Status: **ACCEPTED, committed, pushed and hosted-green at `4d80ca47e94beb68544904e736a9f3415b303157`.**

All ten exact-SHA non-release certification workflows passed. The initial Deep
version-expectation failure was fixed without weakening the contracts; its log
is retained in `publication/initial-deep-failure.log`. The subsequent sort-first
checkpoint is recorded in [the follow-up report](../cp410-sort-first/report.md).

The largest sort penalty is callback factory/frame ownership, rather than selector
parsing or comparisons. General closure calls have a separate native-dispatch
string-construction tax. Wide object access and wide JSON parsing have linear
member searches that become major bottlenecks. These are distinct causes; a VM
rewrite would not by itself remove unnecessary allocations or quadratic searches.

## Baseline and boundaries

Current development baseline is `629b1f23afbb5b3be97dac66b6cea1a9d5b3d1fc`, version
4.10.0, C ABI 1.3. The initial working tree was clean. Local main was two commits
ahead and zero behind origin/main `aaadeb31219251b7cc02a62fbf75360ac3e11aaf`.
The saved exact baseline binary SHA256 is
`86d9dfb2e169aa09d30b890b09edf7dda4105b5e2946c661c23460364fdb09e2`.
The release closeout and development-version commits were included in the authorized normal push.

External oracle unchanged: **YES**. `20261009-v490` is COMPLETE and IMMUTABLE, measured from released
`aaadeb31219251b7cc02a62fbf75360ac3e11aaf`. No official run, workload edit, Labs
edit, node provisioning or public release is part of this checkpoint; the accepted source was subsequently pushed normally.
The final audit checks 8,292 scripting-repository files, 102 shell files, all
30 frozen campaign evidence files, and 166 source/201 public Labs oracle-related
files. The benchmark repositories and frozen series hashes match. The original
Labs frozen result/evidence files and scripting presentation match. Concurrent
Labs work advanced source/public HEADs and changed unrelated generator/Temporal/
iteration-memory reports; later uncommitted shell presentation edits changed the
shell page/template. These external changes are recorded and preserved. The
whole Labs checkout and shell presentation are not claimed unchanged. This
campaign edited no Labs or benchmark repository. See the [current oracle audit](oracle-verification.json),
[concurrent shell snapshot](oracle-concurrent-shell.json) and compressed starting
manifest. The distinction is immutable results versus externally edited presentation.

Local probes establish mechanisms and changes on this machine; they do not
establish new Python/Node ratios. The supplied official gaps remain orientation:
sort about 50× Python/23× Node; frequency, BFS and map/set about 6–9×; calls,
sliding and JSON transform about 5–6×; loops about 3× Python.

Before implementation, native/binding tests, GCC/Clang warnings, binding warnings,
NRS 93/93 and PRS 12/12 all passed. NRS is pinned at `34b1c2f`, PRS at `1da4365`.
See baseline certificate and metrics. All production experiments use the same
version and ABI. No RuntimeValue layout, source-origin representation, callable
identity, capture semantics, module ownership or retention redesign is included.

## Fresh measurement method

All 67 workload profiles were executed afresh against the saved current baseline.
Probe authoring and instrumentation scaffolding reused prior component definitions;
no previous profile result or instrumented binary supplied current measurements.
Local copies of official workloads preserve their semantics; JSON fixture paths
alone point at the immutable input. Official `sort-index` means `sort_by(x => x)`.
The separate local indexed-selector test means `x => a[x]` and is labelled as such.

Measurements include whole-process Callgrind instructions, Memcheck allocations,
cumulative bytes and errors, nine warmed CPU/wall samples, and three peak-RSS
samples per baseline case. Paired trials rotate process order, measure both exact
binaries, check output oracles, and repeat instruction/allocation/RSS measurements.
Larger CPU comparisons and follow-ups are the timing decision evidence: short
startup-dominated samples can be noisy, and CPU observations are machine-specific.
`perf` was refused by the existing host policy; that policy was left unchanged.

Separate diagnostic binaries count phase allocations and RuntimeValue lifecycle
operations. Phase scopes overlap/nest: never sum inclusive phase percentages.
Their instrumentation affects layout and instructions, so phase instruction
counts establish relative mechanisms rather than replacing production totals.
Lifecycle counters are for counts only, never speed claims. Raw JSON parse and
conversion exclude fixture-read/preparse setup from instructions and subtract
zero-round allocation baselines.

The complete per-workload instruction/allocation/byte/RSS/CPU table and self and
inclusive hotspots are retained beside this report. Scaling uses N/2N/4N; wide
objects vary member count independently of 2,000 reads. Baseline and candidate
outputs and exact error contracts are checked.

## Ranked causes and opportunities

| Priority | Symptom and shared reach | Evidence and proposed action | Class/risk |
|---|---|---|---|
| 1 | Identity sort, captured/multi/aggregate sort, map/filter/group and closures | Remove discarded default shared slots during capture insertion; retain fresh identities and canonical slots | A: bounded, lifetime-sensitive certification |
| 2 | Closure/callback/scalar calls, generic expression dispatch | Native-call probes construct temporary names/prefixes even on mismatch; compare a borrowed name directly | A: bounded, exact parsing/diagnostic certification |
| 3 | Identity sort factory/metadata/frame cost and retained teardown | Immutable per-instance metadata and parser retention; investigate plan/instance split before implementation | C: ownership, source origin and escape proof |
| 4 | Wide object member reads, JSON traversal/transform, nested graph access | Linear existence check followed by a second fetch scan; immediate single-fetch is a possible bounded next trial | B for immediate lookup; C for persistent indexes/layout |
| 5 | Wide JSON parse | Canonical Jsonic++ duplicate-key checking scans existing members | C: canonical parser investigation; no vendored-only patch |
| 6 | Frequency/map-set/BFS and array controls | RuntimeValue destruction, statement dispatch, key/location work and collection construction remain distributed | B investigation: immediate duplicate lookups/moves only after provenance and alias proof |
| 7 | Calls/imports/callbacks | Immutable source/path/parameter metadata ownership repeats | C for shared descriptor/plan; exact source diagnostics are mandatory |
| 8 | Script startup and process orchestration | Loader/startup and fork/exec/capture I/O; smaller impact than scripting targets | D for this tranche; preserve startup strengths |

Ranking combines severity, breadth, measured confidence and safety. The largest
ratio does not automatically justify the riskiest implementation. No reusable
frames, arenas, compiler-wide slots, bytecode, JIT or object-layout changes were
attempted.

## Sort decomposition and factory semantics

Production official-equivalent descending identity sort at N=2,000 executes
57,830,201 instructions and 76,539 allocation requests. The fresh instrumentation
counts 2,000 instances, 8,000 factory capture entries, 8,000 frame capture entries,
and 9,580 comparisons. The same-size map creates one instance with four initial
capture entries and still copies 8,000 frame capture entries.

| Nested diagnostic phase | Calls | Allocations | Bytes | Instructions |
|---|---:|---:|---:|---:|
| Factory, inclusive | 2,000 | 34,012 | 3,660,256 | 16,784,624 |
| Instance allocation | 2,000 | 2,000 | 576,000 | 761,878 |
| Immutable/defining metadata | 2,000 | 8,000 | 1,232,000 | 3,996,579 |
| Capture insertion | 2,000 | 18,000 | 1,552,000 | 6,145,025 |
| Registration | 2,000 | 6,008 | 299,936 | 3,392,285 |
| Callback frame, inclusive | 2,000 | 32,001 | 3,456,240 | 14,028,426 |
| Arguments | 2,000 | 2,000 | 304,000 | 761,985 |
| Selector body | 2,000 | 0 | 0 | 724,000 |
| Returned scalar key | 2,000 | 0 | 0 | 152,000 |
| Key/decorated storage | 4,000 | 2,000 | 304,000 | 638,000 |
| Decoration | 2,000 | 0 | 0 | 454,000 |
| Stable sorting/comparisons | 1 | 1 | 176,000 | 5,114,887 |
| Result construction | 1 | 1 | 304,000 | 174,652 |
| Per-callback cleanup | 2,000 | 0 | 0 | 2,192,000 |

Production parser destruction costs 5,361,951 inclusive instructions (9.27%) for
this sort, versus 160,782 (0.61%) for map. This includes teardown beyond retained
lambdas and must not be equated entirely with retention. Captured `x + offset`
sort creates 10,000 capture and frame entries and allocates in the generic body;
the indexed-selector and prepared identity paths have different body costs.

Sort invokes the selector expression for each value. Map's prepared collection
route can evaluate/reuse its callback once. These paths have observable factory
side effects and identity behavior. Hoisting the sort expression would change
semantics. Removing retention requires proof for returned closures, escaped tags,
reference captures, nested callbacks, errors, lexical modules and async clones.
The registry resolves callable identity tags; callback completion alone does not
prove lifetime ended. Architecture work stops here.

## Calls, dispatch and source metadata

The N=2,000 closure profile has 145,995,810 instructions. Leading self costs are
string append 8.87%, C-string-to-string construction 6.02%, the native-call
argument matcher 5.96%, string concatenation 5.72%, strlen 5.43%, and memcpy 5.11%.
The dispatch block has 80 native-call probe sites. Failed probes repeatedly materialize `name + "("` before ordinary callable
resolution. Borrowing `std::string_view` and comparing name, opening parenthesis
and final parenthesis directly removes that work. Names are used synchronously;
error strings are still materialized on failure. Balancing, parameter parsing,
spreads, shadowing, host/module dispatch and source diagnostics are preserved.

Named prepared calls, recursion, multi-argument/local/imported calls and closures
were profiled separately. SourceView/path ownership and RuntimeValue lifecycle
remain measurable; sharing a defining descriptor may reduce metadata duplication,
but requires review of per-instance defining/runtime origins and module ownership.
Existing syntax/numeric plans already share immutable state. A proposed plan may
hold syntax, parameter names and immutable defining source descriptors; instances
must retain identity, captures and runtime/module ownership. Plans must never
cache scopes, RuntimeValues, mutable captures or root/path locations.

## Collections, arrays and BFS

Frequency and map/set mutation combine key conversion, lookup, read/update/store,
root/path work and temporary-value destruction. Ordered runtime collections
already have their existing indexing mechanisms; this investigation does not
introduce another persistent map or frequency-specific primitive. A next bounded
trial could remove a duplicate immediate lookup, provided marked/reference
fallback and insertion order stay exact.

BFS is measured both as the official grid workload and a local chain, plus queue,
set-visit, map-update and nested-index components. At N=2,000 these components cost
26.18M, 39.84M, 20.15M and 28.01M instructions respectively; the chain costs 93.64M.
Their self hotspots include RuntimeValue destruction and statement dispatch.
These components overlap mechanisms and their totals must not be summed to
reconstruct the chain. No BFS-specific optimization was added.

Sliding/window and scan use prepared arrays/numeric paths already, but pay value
construction/destruction, dispatch and indexing/bounds work. No interior pointer
is retained across mutation. A read-only immediate scalar path or a one-fetch
member primitive is a possible B trial, requiring exact alias/location tests.
Loops still pay ordinary statement dispatch and RuntimeValue destruction; those
are the baseline tax, not an explanation for the extra factory/capture sort tax.

## JSON and wide lookup: review boundary

Repeated last-member lookup at 2,000 reads costs approximately 14.86M, 17.62M,
21.64M, 89.47M, 245.73M and 2.201B instructions at object sizes 8, 32, 128, 512,
2,000 and 8,000. Setup includes constructing/parsing the object, so these whole
process values combine linear repeated lookup and wide construction costs.
At size 2,000, memcmp alone consumes 48.23% self instructions; const member access
15.48%, RuntimeValue::has 15.08%, and JsonDocument::has 6.84%. Source confirms
ordered vector scans and existence followed by retrieval. This is a genuine
wide-object bottleneck; it is not solved by the retained capture/dispatch trials.

Separate raw JSON instruction costs per round:

| Shape | Parse | Runtime conversion | Parse allocations | Conversion allocations |
|---|---:|---:|---:|---:|
| 2,000-member wide object | 45,508,767 | 504,666 | 4,009 | 1 |
| Depth 64 | 62,212 | 49,039 | 128 | 64 |
| 2,000-element array | 1,245,223 | 370,639 | 2,010 | 1 |
| Mixed 1,000-object tree | 4,466,992 | 3,092,024 | 9,009 | 3,001 |

Wide parse is about 90× conversion, with canonical duplicate-key membership
checking implicated. Mixed conversion is a substantial fraction of the pair.
Do not extrapolate the wide result to every JSON shape. No Nift-side JSON parser
patch or vendored Jsonic++ change was made.

Architecture options for review:

- A transient operation-local member index avoids permanent small-object cost,
  but must be built only when repeated access amortizes O(m) construction. Memory
  is O(m) hash entries/positions, plus key storage if keys are copied. It
  cannot retain invalidated member addresses during insertion/erasure/move.
- A lazy index adds memory and cache invalidation state to each participating
  object, with O(m) entries plus buckets and possible duplicated keys. Copies,
  moves, aliases, mutation, key equality and concurrency need a
  single canonical ownership policy.
- A hybrid small ordered vector plus member-index structure preserves iteration
  order by storing positions, but duplicates key/index storage and requires
  updates after erasure/reordering. Exact memory depends on allocator/hash layout;
  do not claim an unmeasured byte budget.
- A canonical Jsonic++ duplicate-key/index investigation must originate in the
  standalone project, preserve strict duplicate/error behavior, synchronize all
  consumers, and be validated independently before Nift integration.

Review payoff and tests explicitly before implementation. A plan/instance split
could replace up to the measured 8,000 metadata allocation requests per 2,000
sort instances with allocations per unique plan; actual speed depends on shared
ownership/refcount costs and remaining capture/frame work. It should retain only
O(unique plans) immutable metadata plus fresh per-instance captures/origins.
Lifetime reclamation requires an escape proof and is a separate decision.

An immediate one-fetch object primitive adds no persistent per-object storage
and can reduce a two-scan read to one scan, preserving the first matching member
and exact missing-member behavior. It must resolve fresh after mutation, never
cache raw interior pointers, and cover marked references, array/object growth,
erasure, key equality and insertion order. Its payoff is shape-dependent and
must be measured rather than assumed from the wide profile.

All designs require canonical diagnostics (file/line/column/frames/imports and
callable defining origins), identity/factory/unused-capture/module/async contracts,
root-path growth/corruption and mutation aliases, ASan/UBSan/LSan/Memcheck, full
native/bindings and NRS/PRS, and deterministic scaling/allocation controls. A
Jsonic++ change additionally requires canonical standalone strict duplicate-key,
malformed-input/schema tests and byte-for-byte consumer synchronization.

None is approved or implemented here. The immediate single-lookup B candidate
could halve a double scan without changing asymptotic complexity; do not label
that a representation fix.

## RuntimeValue lifecycle

Fresh counters include constructors, copy/move construction, assignments and
destruction for 12 workloads. Identity sort N=2,000 counts 44,085 constructors,
8,002 copy constructors, 18,768 move constructors, 16,015 copy assignments,
38,744 move assignments and 70,855 destructors. The counts support focusing on
factory/frame/key ownership; moves are not evidence that the operation is free.
Explicit aggregate-copy instrumentation was compiled afresh across all isolated
baseline translation units; regular lifecycle totals match the first counter
run. Identity sort has three aggregate copy operations transferring 4,000
immediate children (two nonempty input-array copies). Named/closure/callback
scalar calls and frequency have zero aggregate copies in these probes: their
large copy/assignment counts are mostly scalars and frame machinery. Official
BFS has 1,710 aggregate copies transferring 4,135 immediate children; official
JSON transform has 2,001 transferring 4,000. Small object/neighbor copies and
construction matter there. Nested copying is counted at each aggregate level;
shared bytes/timer/error resources are not classified as deep copies. Empty
aggregate operations count even when zero children are transferred. See the
complete [lifecycle counters](lifecycle.json). No public value semantics or
storage representation changed.

## Shell and startup

Leave startup alone. Empty script production profiling totals 2,448,961
instructions and 188 allocations; leading self loader-symbol costs alone are
about half its instructions. This whole-process baseline includes file read,
parser/source construction, environment/setup, execution and teardown. It gives
no evidence for days of startup work or an architecture replacement.

A bounded captured `run("/usr/bin/true")` profile records one command and 100
commands. The 100-command syscall summary has 100 clones, 101 execve calls
(including Nift), 100 wait4, 606 openat and 200 unlink calls. Wait dominates the
strace time; tracing distorts elapsed performance. The process backend uses
fork/execvp, waitpid and two temporary capture files per captured run. Those files
explain extra open/remove work. This script intentionally exercises captured run;
it is not an official shell-row rerun or a Bash/Fish fairness comparison.
Foreground shell execution has a different capture/terminal policy. Any future
spawn/capture trial must preserve stdout/stderr, cwd/env, pipelines, exit status,
job control and terminal behavior. No process-backend change was made.

## Retained/rejected experiments, final comparison and certification

Two bounded production changes are retained as a VALIDATED CANDIDATE:

1. Copy capture bindings with `insert_or_assign` through an outlined private
   helper at the factory, collection callback frame and direct lambda frame.
   Existing names overwrite in scope order, and canonical shared slots stay
   shared. Parameter insertion, worker clone transfer and retention are unchanged.
   Official-equivalent N=2,000 sort saves 16,000 allocation requests and about
   512 KB cumulatively; identity map saves 8,004 requests. Allocation reduction
   does not imply 512 KB lower peak RSS.
2. Borrow native probe names and compare the prefix without concatenation.
   Closure N=2,000 saves another 12,048 allocation requests and 34.57% of the
   capture-candidate instructions. Final vs original closure instructions fall
   about 35.08%; callback instructions fall about 32.34%. The benefit is largest
   when generic callable resolution reaches many failed native probes.

The initial inline insertion variant was not selected: it saved the same slots,
with neutral instruction controls, but initial larger CPU controls were uncertain
(loops +8.1%, noarg +10.7%). Follow-ups reduced those differences. The outlined
variant was measured independently and selected after neutral larger controls
and exact safety checks. This does not prove an instruction-cache explanation.
No semantic failure was found in the inline trial.

The dispatch trial's initial sliding control (+7.5%) did not repeat in 31 warmed,
rotated samples (-0.48%). Its filter aggregate follow-up was -1.62%; closure
repeated -34.92%. The combined candidate's short loop comparison (+12.31%) and
scalar call (+3.62%) were resolved by 27 warmed larger samples: loops +0.95%,
scalar -0.32%, noarg -1.13%. Preliminary results remain in the evidence; the
longer targeted follow-ups take precedence. No CPU gain is claimed for ordinary
named calls. No failed correctness test was dismissed as flaky.

| Local family | Final instructions vs baseline | Allocations saved | Repeated process CPU vs baseline |
|---|---:|---:|---:|
| Loops | -0.77% | 30 | +0.95% |
| Noarg calls | -0.54% | 30 | -1.13% |
| Scalar calls | -0.40% | 30 | -0.32% |
| Closure calls | -35.08% | 18,051 | -35.92% |
| Callback calls | -32.34% | 16,038 | -34.29% |
| Identity sort | -5.27% | 16,048 | -3.04% |
| Indexed selector sort | -5.21% | 16,048 | -3.36% |
| Frequency | -2.36% | 258 | -2.51% |
| Map/set | -0.51% | 48 | -0.09% |
| Sliding | -0.61% | 54 | +0.60% |
| BFS chain | -0.29% | 90 | -3.41% |
| JSON traversal | -0.30% | 30 | +1.16% |
| JSON transform | -0.24% | 30 | -1.40% |
| Filesystem | -0.11% | 24 | No larger timing claim |

Instruction/allocation columns use N=2,000. CPU columns use the larger workloads
recorded in the CPU evidence, including N=160,000 for loop/named-call follow-ups
and N=32,000 for sort follow-ups. Sizes differ, and these cannot be converted to
new official peer ratios. Short paired CPU/wall/RSS observations for every trial
remain available in the full comparison JSON.

Official-equivalent descending identity sort final instruction reduction is
about 5.77% at N=2,000, with 16,108 fewer allocations. This is useful but nowhere
near removing the historical order-of-magnitude peer gap. Wider unused captures
save substantially more allocations without changing which variables are
captured. Generic call improvements do not remove the fresh-instance-per-element
sort behavior.

Official-equivalent local rows improve much less than the closure component:

| Local official-equivalent small fixture | Instructions vs baseline | Allocations saved |
|---|---:|---:|
| Frequency | -1.88% | 108 |
| Map/set | -2.70% | 84 |
| Sliding | -1.44% | 72 |
| Grid BFS | -1.22% | 150 |
| JSON parse | -1.31% | 24 |
| JSON traversal | -0.62% | 30 |
| JSON transform | -0.59% | 42 |
| Ordinary function calls | -0.83% | 30 |
| Scan | -1.23% | 30 |

The large closure/callback win must not be described as a 35% improvement to the
official ordinary-function-call row. These are local fixture measurements; the
external official results remain frozen. Named prepared calls already bypass
much of the costly generic probe path.

Five repeated RSS samples per binary for empty/loops, identity and captured sort,
closure calls, identity map and wide objects show broadly stable peak residency.
Official sort N=4,000 median changes 16,700 to 16,892 KiB (+1.15%); captured sort
remains 17,400 KiB; other medians fall slightly. Process RSS jitter and allocator
retention prevent claiming a peak-memory improvement from temporary-allocation
savings. See [RSS repeats](rss-repeats.json).

The allocation guard runs N=100/200/400 with and without 16 unused bindings, then
generic closure calls. Capture slope is 2.25 allocations per extra entry versus
original 4.25 (budget 2.5). Dispatch slope is 33 allocations per call versus
capture-only 39 and original 42 (budget 36). Both doublings match. Final passes;
original fails both budgets; capture-only fails the dispatch budget. The explicit
Make target is registered in the existing Valgrind performance workflow, with its
test file included in workflow path filters. No absolute wall-time guard was
introduced. This gate is calibrated on Linux x86_64/libstdc++; allocator/library
changes require explicit budget re-evaluation rather than timing retries.

N=2,000/4,000/8,000 instruction measurements cover loops, scalar calls, frequency,
map/set, sliding, BFS, JSON traversal/mutation and filesystem. Whole-process 4N/N
ratios remain about 3.34–3.89 before and 3.36–3.89 after; fixed startup prevents
expecting exactly four. This is evidence against a new quadratic regression in
these local families, not proof for all inputs. Wide-object construction/access
is separately identified as nonlinear setup plus linear reads.

### Full fresh baseline profile table

Every row has exact output verification and zero Memcheck errors. RSS is the
median of three baseline samples, in KiB. CPU/wall samples and top six self and
inclusive symbols per row are in [baseline metrics](baseline-metrics.json) and
[hotspots](hotspots.json). Nominal N=1,000 on official small-fixture rows is a
catalog label; inspect the probe for actual geometry/fixture size (BFS is a 20×20
grid), rather than interpreting every label as iteration count.

| Probe | N/label | Instructions | Allocations | Cumulative bytes | Peak RSS KiB |
|---|---:|---:|---:|---:|---:|
| loops | 2,000 | 12,609,240 | 4,329 | 798,471 | 7,292 |
| array-build | 2,000 | 11,880,246 | 4,366 | 1,390,873 | 7,516 |
| call-noarg | 2,000 | 18,013,446 | 8,376 | 1,203,364 | 7,144 |
| call-scalar | 2,000 | 24,092,487 | 18,386 | 2,388,726 | 7,320 |
| call-multi | 2,000 | 46,927,073 | 42,400 | 7,062,915 | 7,172 |
| call-locals | 2,000 | 32,561,685 | 30,413 | 3,739,639 | 7,164 |
| call-recursion | 2,000 | 111,410,101 | 88,428 | 14,193,884 | 7,464 |
| call-closure | 2,000 | 145,995,810 | 84,420 | 8,787,643 | 7,428 |
| call-callback | 2,000 | 157,062,050 | 104,437 | 11,386,282 | 7,296 |
| array-index | 2,000 | 23,113,355 | 8,436 | 2,072,574 | 7,588 |
| sort-identity | 2,000 | 58,621,617 | 76,449 | 10,614,223 | 11,980 |
| sort-index | 2,000 | 59,272,988 | 76,456 | 10,591,440 | 11,868 |
| sort-arithmetic | 2,000 | 64,101,721 | 78,456 | 10,759,597 | 12,116 |
| map-identity | 2,000 | 26,518,709 | 40,440 | 6,430,406 | 8,184 |
| map-index | 2,000 | 27,344,711 | 40,456 | 6,419,744 | 8,084 |
| map-arithmetic | 2,000 | 32,052,776 | 42,451 | 6,567,788 | 7,964 |
| frequency | 2,000 | 45,672,074 | 31,032 | 4,788,368 | 7,204 |
| map-set | 2,000 | 34,290,351 | 14,451 | 3,967,306 | 8,500 |
| sliding-window | 2,000 | 27,856,199 | 8,434 | 2,072,207 | 7,512 |
| json-parse-convert | 2,000 | 12,539,806 | 14,369 | 4,035,520 | 9,316 |
| json-traverse | 2,000 | 32,955,550 | 42,428 | 13,032,364 | 9,308 |
| json-mutate | 2,000 | 40,433,801 | 44,454 | 14,509,869 | 9,296 |
| json-serialize | 2,000 | 27,761,692 | 25,510 | 11,584,099 | 10,912 |
| construct-destroy | 2,000 | 20,097,521 | 16,349 | 3,055,852 | 7,352 |
| bfs | 2,000 | 93,640,950 | 64,659 | 11,057,257 | 9,012 |
| filesystem | 2,000 | 81,839,203 | 69,309 | 22,445,421 | 9,272 |
| empty | 0 | 2,448,961 | 188 | 109,701 | 6,272 |
| map-aggregate | 2,000 | 58,976,492 | 96,450 | 23,969,719 | 13,900 |
| filter-aggregate | 2,000 | 64,228,955 | 96,474 | 23,982,809 | 13,804 |
| group-unique | 2,000 | 68,452,180 | 112,479 | 28,820,605 | 17,632 |
| group-repeated | 2,000 | 66,748,848 | 106,628 | 27,886,069 | 16,680 |
| official-sort-index | 1,000 | 30,598,146 | 38,530 | 5,379,720 | 9,600 |
| official-scan | 1,000 | 8,992,236 | 2,331 | 463,282 | 7,316 |
| official-frequency-count | 1,000 | 24,453,486 | 15,688 | 2,459,629 | 7,488 |
| official-map-set | 1,000 | 12,413,915 | 3,525 | 837,784 | 7,704 |
| official-sliding-window | 1,000 | 18,924,741 | 5,439 | 1,271,546 | 7,628 |
| official-bfs | 1,000 | 48,351,269 | 29,904 | 4,892,207 | 7,968 |
| official-json-parse | 1,000 | 7,657,552 | 7,384 | 2,088,636 | 8,244 |
| official-json-transform | 1,000 | 25,161,216 | 19,476 | 6,556,807 | 7,936 |
| official-function-calls | 1,000 | 13,297,590 | 9,392 | 1,261,172 | 7,284 |
| official-json-traverse | 1,000 | 18,009,225 | 21,437 | 6,589,684 | 8,452 |
| official-sort | 1,000 | 30,592,383 | 38,530 | 5,375,597 | 9,696 |
| captured-sort | 1,000 | 36,693,564 | 43,557 | 5,778,154 | 9,728 |
| unused-captures | 1,000 | 92,856,226 | 171,179 | 17,593,269 | 14,456 |
| official-sort | 2,000 | 57,830,201 | 76,539 | 10,616,873 | 12,064 |
| captured-sort | 2,000 | 69,906,605 | 86,566 | 11,419,430 | 12,156 |
| unused-captures | 2,000 | 178,321,409 | 341,188 | 35,002,545 | 21,424 |
| official-sort | 4,000 | 113,324,466 | 152,541 | 21,102,161 | 16,860 |
| captured-sort | 4,000 | 137,331,794 | 172,568 | 22,704,718 | 17,352 |
| unused-captures | 4,000 | 350,918,094 | 681,190 | 69,823,833 | 36,088 |
| wide-object-8 | 2,000 | 14,858,677 | 4,413 | 812,775 | 7,256 |
| wide-object-32 | 2,000 | 17,617,944 | 4,523 | 851,486 | 7,416 |
| wide-object-128 | 2,000 | 21,635,822 | 4,911 | 1,008,045 | 7,488 |
| wide-object-512 | 2,000 | 89,465,215 | 6,451 | 1,646,637 | 7,736 |
| wide-object-2000 | 2,000 | 245,732,590 | 8,394 | 2,771,957 | 7,788 |
| wide-object-8000 | 2,000 | 2,200,832,756 | 20,396 | 8,841,125 | 10,772 |
| sort-aggregate | 2,000 | 78,814,550 | 124,479 | 25,933,365 | 17,772 |
| call-imported | 2,000 | 29,244,266 | 34,536 | 4,503,298 | 7,272 |
| call-aggregate | 2,000 | 33,371,021 | 38,488 | 7,056,219 | 7,268 |
| sort-multi | 2,000 | 119,236,830 | 200,496 | 37,150,883 | 20,972 |
| json-wide | 2,000 | 93,592,751 | 50,445 | 22,354,831 | 15,424 |
| json-deep | 2,000 | 20,937,220 | 16,826 | 10,179,541 | 9,572 |
| json-mixed | 2,000 | 54,144,506 | 52,446 | 21,826,031 | 15,512 |
| bfs-queue | 2,000 | 26,182,732 | 8,436 | 2,073,030 | 7,572 |
| bfs-set-visit | 2,000 | 39,835,051 | 12,464 | 2,511,989 | 7,692 |
| bfs-map-update | 2,000 | 20,150,205 | 6,458 | 1,423,315 | 7,884 |
| bfs-nested-index | 2,000 | 28,012,881 | 10,448 | 2,684,002 | 8,056 |

### Safety and certification

All required local certification is green:

| Gate | Result |
|---|---|
| Native suite, embedding and maintained bindings | PASS |
| Normal GCC/Clang first-party warnings and native wrapper warnings | PASS, zero |
| C# build/tests | 0 warnings, 29 tests PASS |
| NRS at `34b1c2f`, development version 4.10.0 | 93/93 PASS |
| PRS at `1da4365` | 12/12 PASS |
| Canonical diagnostics and callable/callback compatibility | PASS, including 470 numeric/fallback parity pairs |
| Exact factory/capture/identity/module/async contracts | 19/19 native and lifetime-sanitized PASS |
| Selector/capture/location/error exact baseline diagnostics | 58/58 lifetime-sanitized PASS |
| Root-path references, growth/corruption and async/future | PASS under lifetime sanitizer |
| Full use-after-scope lifetime canary/corpus, ASan/UBSan/LSan | PASS |
| Deep-capable sanitizer and maintained parser fuzz/resource wall | PASS, 1,219 cases |
| Core lifecycle/memory | PASS, four rounds, 57 phases |
| Production paired Memcheck workloads | All 67 rows per trial PASS, zero errors |
| Allocation slopes with two defect negative controls | PASS |
| N/2N/4N instruction scaling | 27 paired baseline/final rows PASS |
| Portable bundled workload reproduction | All 119 probes PASS |
| Public headers and canonical vendored Jsonic++ | 11 files byte-identical to baseline |

See [progress certificate](final/progress.log), [fuzz certificate](final/parser-fuzz.json),
[core memory](final/core-memory.json), [source boundaries](final/source-boundaries.json)
and the complete recorded logs. Counter-build outputs are checked against the
same probe oracles; adding explicit deep-copy classification preserves every
regular lifecycle total from the first fresh counter run.

Normal warning gates are zero. GCC 15's O1 deep ASan compilation additionally
emits a `-Wmaybe-uninitialized` diagnostic inside libstdc++ stable-sort comparator
machinery in `evaluate_collection_value`. Compiling the baseline expression
source with the same sanitizer flags emits the same warning; it is retained in
[baseline compiler log](checks/baseline-sanitizer-compiler.log) and the final deep
build log. That function is unchanged by these experiments. No warning
suppression was added, and this report does not claim zero warnings in every
sanitizer compiler configuration.

The first deep fuzz invocation incorrectly used `nift-sanitize-lifetime`, the
repository's deliberately shallow use-after-scope profile. Its inflated evaluator
stack frames overflowed a build-worker stack at the 5,000-unary boundary before
the existing expression-depth guard. The failure is preserved in
[misconfigured fuzz log](final/misconfigured-parser-fuzz.log). This was an
invocation defect, not a discarded flaky result or a production semantic fix.
Normal baseline and final binaries both reject that input with identical
file/line/column/error output; the dynamic build elapsed-time line is excluded
from their stdout comparison. See [native boundary](unary-boundary-native.json).

The campaign harness is corrected to call the maintained deep-capable
`checkpoint-9-parser-fuzz` Make target after `test-sanitize`. The separate
`test-sanitize-lifetime` canary confirms use-after-scope detection is ON there
and OFF in the deep profile. All capture/alias/async lifetime checks remain on
the full use-after-scope profile. No depth limit was lowered, sanitizer finding
ignored, sanitizer target weakened, retry-until-green loop introduced, or
production parser changed to accommodate this configuration mistake.

## Recommendation and publication state

The user accepted both retained runtime changes and the allocation guards and
local safety evidence. Normal coherent commits and push are authorized, including
the existing release-closeout/development-bump ancestors. Focused committed-state
checks and exact pushed-SHA hosted certification must pass before the next runtime
checkpoint. Prior v4.9 hosted green is not a certificate for this code.

Sort/selector architecture is the primary v4.10 target. Extend exact contracts,
measure post-acceptance factory/metadata/capture/frame/retention costs, and design
immutable plans plus fresh instances. Return the ownership/source/escape/async
proposal for review before implementing the plan/instance redesign. A bounded
one-fetch object-member experiment and canonical standalone Jsonic++ investigation
are authorized secondary work after hosted green. Persistent indexes, runtime
layout changes, selective capture, reclamation and reusable frames remain outside
this authorization. The practical competitiveness target has not yet been met.

Publication state at acceptance: local and unpushed; hosted certification pending.
Historical baseline and local certificates above describe the completed investigation.
No release, tag, official series, node provisioning or campaign Labs publication
is authorized. Concurrent external Labs edits remain preserved.
