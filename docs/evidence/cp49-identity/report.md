# NIFT v4.9 — identity sort / callable architecture investigation

CP49-51–57 is complete as a local design/root-cause checkpoint. No production
runtime optimization was added. Recommendation: **SAFE THIRD BOUNDED TRANCHE**,
limited initially to capture-map insertion overhead; measure owned key storage
separately. The large capture/identity/lifetime changes remain architecture decisions.
Stop here for Nick's review; this report does not start that follow-up tranche.

## Wave-2 hosted closeout

**CP49 WAVE 2: CLOSED.** Accepted runtime:
`3c0a5b22c24da50f749e1f385d84d52ce1a4543d`; accepted closeout and hosted identity:
`02e3793ccc8d378ce63382b28cb12a69b5ce683d`.
Normal push succeeded from `d9b85f2` to `02e3793`, with clean tree and ahead/behind
`0/0` immediately after publication. No force push or Release artifacts run.
Version is 4.9.0; ABI is 1.3. [Exact hosted run identities](hosted-wave2.json)
record ten completed successful workflows on that SHA:

- [Deep Guards: PASS](https://github.com/nift-dev/nift/actions/runs/37785757493), including sanitized fuzz/lifecycle, GCC/Clang warnings, NRS 93/93 and PRS 12/12.
- [Test Integrity: PASS](https://github.com/nift-dev/nift/actions/runs/37785733844), including Go/C#/Node/Python bindings, clean aggregate walls and the non-destructive build-boundary proof.
- [Checkpoint 10 cross-platform: PASS](https://github.com/nift-dev/nift/actions/runs/37785733913), Linux/macOS/Windows and normalized equivalence.
- [Performance regression guards: PASS](https://github.com/nift-dev/nift/actions/runs/37785733841).
- [Compiler diagnostic: PASS](https://github.com/nift-dev/nift/actions/runs/37785733782).
- [FFI wall: PASS](https://github.com/nift-dev/nift/actions/runs/37785733784), [bytes wall: PASS](https://github.com/nift-dev/nift/actions/runs/37785734066), [v4.4/v4.5 cross-platform: PASS](https://github.com/nift-dev/nift/actions/runs/37785734019).
- [Init Targets: PASS](https://github.com/nift-dev/nift/actions/runs/37785734154), [build-only packaging: PASS](https://github.com/nift-dev/nift/actions/runs/37785734080).

Investigation began only after all ten were green. Rejected wave-1/wave-2
experiments remain rejected absent new evidence. The frozen `20261008-v480`
series is unchanged: all 2,828 saved hashes still match. No Labs writes, official
runs, node provisioning, public API changes or vendor changes. [Boundaries](boundaries.json)
verify the unchanged production source/binary. The investigation evidence commit
is local documentation, separate from the hosted runtime identity above.

## Method and evidence limits

Fresh production profiles execute current hosted-green HEAD at N=2,000 across
identity map/sort, arithmetic/aggregate sort, a syntactically formal-only
selector, a captured selector, pre-created selector, unused-binding stress and
scalar/closure/callback calls. Instructions, allocations/bytes/frees, CPU/wall
and five RSS observations are in [metrics](metrics.json); all ten Memcheck runs
have zero errors and allocations equal frees. These are local probes, not new
Python/Node comparisons or official methodology.

Short process medians include setup/startup and were collected while diagnostic
work also ran; treat them as noisy. [Long native controls](long-cpu.json) use
15 post-warmup rotating samples, N=16,000 selectors and N=50,000 calls/loops,
with no compilation or Valgrind wall running. They assert outputs against the
unchanged first-wave oracle. Built-in selector timers are useful for the original
probes; aggregate sort's empty timer tail is an output normalizer, **not** a
steady-state measurement. Process CPU is valid for that aggregate case.

[Phase diagnostics](phase-metrics.json) use an isolated expression translation
unit, stage gates and C++ new-request counters. Instruction counts include
instrumentation; allocation/byte counters exclude libc-only malloc. Factory
includes instance/metadata/capture/registration; body includes returned-key
copy. Nested totals overlap and must not be added. Frame covers lexical setup,
capture copies, parameter binding and source-context setup; cleanup is separate.
Key covers owned-key capacity and moving the returned key, not body execution.
Sort-entry destruction is explicitly cleared in the diagnostic binary only.
These stage estimates are not an exact additive partition of production costs.

[Production destructor measurements](destruction.json) toggle Callgrind collection
at the unmodified `Parser::~Parser()` boundary. They include retained instances,
scopes and all parser-owned cleanup, rather than pretending to isolate only
capture destructors. Allocation/free annotations supply the corresponding tree.
[Value lifecycle counters](value-counts.json) freshly run the accepted isolated
instrumented layout; all production C++ sources match its build. Counts diagnose
value churn and are not production timing or ABI-layout observations.

Reproducible tools live in [tools](tools/profile.py); raw traces, input fixtures,
binaries, build commands and logs remain under `.build/cp49-identity` and the
preceding campaign `.build` directories. Replays require those saved baseline
binaries/probe assets. Annotated production profiles are retained in `profiles/`.

## CP49-51: identity sort decomposition

| Production probe | Instructions | Allocations | Bytes | Peak RSS median KiB |
|---|---:|---:|---:|---:|
| call-scalar | 24,413,776 | 18,493 | 2,396,252 | 6,992 |
| call-closure | 146,379,284 | 84,527 | 8,793,170 | 7,280 |
| call-callback | 157,413,171 | 104,543 | 11,391,566 | 7,068 |
| sort-identity | 61,987,291 | 84,556 | 11,289,751 | 11,988 |
| sort-arithmetic | 67,462,637 | 86,563 | 11,435,125 | 12,104 |
| map-identity | 28,094,783 | 44,549 | 6,772,101 | 7,952 |
| sort-aggregate | 79,123,206 | 124,586 | 25,959,979 | 17,412 |
| sort-precreated | 42,542,024 | 48,562 | 7,248,515 | 8,456 |
| sort-captured | 72,606,328 | 90,563 | 11,801,052 | 12,260 |
| sort-unused-50 | 247,479,629 | 493,555 | 47,567,618 | 26,680 |

The matched pre-created selector captures the same five names as the direct
literal and uses the same input/output. It is an explicit usage control, not a
proposal to change selector evaluation timing. Its 42.54M versus 61.99M
instructions isolate substantial factory/registration/retained-instance work.
Earlier exploratory pre-creation before array setup captured only two names;
that confounded comparison is superseded by this matched control.

| Identity-sort stage (isolated instrumentation) | Instructions | C++ new requests | Requested bytes |
|---|---:|---:|---:|
| factory | 18,187,846 | 38,012 | 3,994,256 |
| instance | 761,757 | 2,000 | 576,000 |
| metadata | 3,996,402 | 8,000 | 1,230,000 |
| capture | 7,546,952 | 22,000 | 1,888,000 |
| registration | 3,392,371 | 6,008 | 299,936 |
| arguments | 761,895 | 2,000 | 304,000 |
| frame | 15,740,660 | 36,001 | 3,790,240 |
| body | 724,000 | 0 | 0 |
| key | 638,000 | 2,000 | 304,000 |
| decoration | 454,000 | 0 | 0 |
| sorting | 5,769,762 | 1 | 176,000 |
| result | 174,652 | 1 | 304,000 |
| cleanup | 2,374,000 | 0 | 0 |
| returned_key | 152,000 | 0 | 0 |
| destruction | 508,248 | 0 | 0 |

The critical asymmetry is explicit in the current implementation:
`sort_by` evaluates each selector inside the per-element/per-spec loop;
`map` evaluates its callback once before its element loop. Direct numeric map
therefore creates one instance and copies five capture bindings, while sort
creates 2,000 instances and copies 10,000. Both then copy five bindings into each
of 2,000 callback frames: a further 10,000 frame capture entries each.
Identity body execution is prepared 2,000 times with zero legacy body executions
and zero body allocations. Blaming repeated syntax parsing or compatibility
body execution is incorrect for this workload.

The identity sort performs 18,533 stable comparisons. Numeric keys are scalar:
returned-key copies allocate nothing, and owned keys/results already move.
Remaining per-element key vectors allocate 2,000 times (304,000 bytes). This is
a smaller, distinct opportunity than eliminating instance/capture machinery.

Production parser destruction costs 5.84M instructions for direct identity sort,
versus 0.16M for map or the matched pre-created selector. Its allocation tree
reports zero new allocations, 24,035 freed blocks and 4.14MB freed in that parser
cleanup subtree. Instances remain in `lambda_instances_` until parser teardown;
this explains meaningful process CPU/RSS costs outside the built-in sort timer.
With 50 unused bindings, total instructions rise to 247.48M and allocations to
493,555: the dominant term depends on visible-binding count as well as element
count. It is not a newly discovered quadratic comparison-sort defect.

## Longer current-runtime controls

| Probe | N | CPU ms median | Wall ms median | Built-in ms median |
|---|---:|---:|---:|---:|
| call-scalar | 50,000 | 47.00 | 47.42 | 43 |
| call-closure | 50,000 | 307.15 | 307.63 | 304 |
| call-callback | 50,000 | 335.61 | 336.07 | 333 |
| sort-identity | 16,000 | 89.81 | 90.30 | 51 |
| sort-arithmetic | 16,000 | 95.60 | 96.14 | 56 |
| map-identity | 16,000 | 23.96 | 24.39 | 12 |
| sort-aggregate | 16,000 | 152.78 | 153.52 | 0 |
| sort-precreated | 16,000 | 40.75 | 41.13 | 25 |
| sort-captured | 16,000 | 98.63 | 99.27 | 59 |
| loops | 50,000 | 22.61 | 23.01 | 19 |

The aggregate built-in zero is an empty normalization timer, as noted above.
The larger selector controls confirm the direct/pre-created difference; they do
not predict cross-language or official performance. Scalar/closure/callback
counts use the same N as the loop control, demonstrating remaining machinery
cost rather than an additional wave-2 regression claim.

## CP49-52–54: observable selector and capture contract

[19 executable contract controls](observability.json) match exact stdout, stderr
and exit status against the accepted first-wave oracle:

- Factory side effects execute once per sort element/spec, once for map, zero
  times for empty sort and once for empty map. Two specs preserve element/spec order.
- Fresh closures compare unequal; an escaped factory-created selector remains
  distinct per element. Equality with the same callable remains true.
- Mutable captures and outer rebinding stay live; parameters shadow variable
  captures, but named callable precedence can still win in body resolution.
- Returned and escaped callables preserve independent defining environments.
- Nested root/path captures remain valid across parent-vector growth.
- Defining-module ownership wins over a caller's conflicting private binding.
- Factory exceptions stop at the throwing element. Unsupported keys preserve
  controlled failure/cleanup. Source/exit/error text remains exactly equal.
- Async variable invocation returns an awaited future. Existing collection
  invocation of an async lambda is synchronous; the investigation preserves that
  current behavior rather than inventing a new contract.

Two controls make selective capture particularly unsafe:

1. `fn(read_hidden()) { return hidden }`, called by `x => read_hidden()` returned
   from `mk(hidden)`, observes the captured `7`, despite a caller `hidden := 100`.
   A syntax-only free-variable list of the lambda misses that indirect dependency.
2. An unused visible timer captured by `async x => x` makes future invocation fail
   with `async function capture contains a non-transferable timer`. Removing
   apparently unused captures would change that observable error policy even
   for a formal-only body.

The five bindings in the identity probe are `cmd`, `args`, `a`, `i`,
`probe_timer`. The parser captures all visible scopes, with inner-name overwrite,
not only syntactic free variables. Binding copies share value/slot ownership;
they are **not** deep copies of the entire receiver array. Logical location
paths may themselves require owned copies.

| Class | Current conclusion |
|---|---|
| A: factory must execute per element | Arbitrary factories, including observable counters/escapes/errors; proven. |
| B: fresh factory/identity, share immutable metadata | Direct literal syntax/parameter/body metadata is a promising design direction. Existing syntax caching already does the parsing part. Runtime origin/module/captures stay per instance. |
| C: runtime instance reuse for non-capturing forms | No safe production eligibility class established. Formal-only syntax is not a proof of an empty or unobservable capture environment. |
| D: needs semantic change | Globally moving factory evaluation out of sort, reusing escaping identities or dropping captures based only on syntactic free variables. Reject. |

## CP49-55: callable plan versus instance

`LambdaSyntax`/numeric plans are already shared through bounded syntax-only
caches. `LambdaInstance` still copies parameter/body metadata, defining path,
source view/provenance, module ownership and a capture map into a fresh identity.
Diagnostic metadata alone makes 8,000 allocation requests / 1.23MB in the
identity sort, independently of the capture-map requests.

An architecture option is an immutable syntax/parameter/body descriptor shared
by fresh instances. Keep defining `SourceView`, path/provenance, module and live
capture ownership outside source-independent thread-local caches. No cached
runtime values, scopes, mutable state or root/path locations belong there.
A per-parser owned origin descriptor could be considered separately, with exact
source diagnostics and defining-path authority retained. No implementation or
claim of proven compatibility is made here.

Lazy parent environments, persistent lexical slot IDs and selective capture need
explicit lifetime/shadowing/escape/worker-transfer contracts. Removing registered
instances requires an escape/lifetime decision: ordinary factories can leak the
selector to user code. These remain architecture options for Nick, not automatic
extensions of this investigation.

## CP49-56: bounded key and insertion opportunities

1. **Capture insertion without a discarded default binding.** Both capture
   construction and frame copying currently use `map[name] = binding`.
   `VariableBinding()` creates a shared slot which assignment immediately
   replaces. The isolated [mechanism probe](tools/insertion_probe.cpp) uses the
   actual private binding type: five entries × 2,000 copies make 24,000 allocation
   requests with indexed assignment versus 14,000 with `insert_or_assign`, saving
   10,000 requests and 320,000 bytes. Both include a location-path copy; hence
   their absolute totals differ from the scalar-only capture phase. Duplicate
   overwrite, all flags, shared slot/root identity and growth synchronization
   match. [Results](insertion-mechanism.txt) and an [ASan/UBSan/LSan mechanism run](insertion-sanitized.txt) pass. The sanitizer instruments this small probe; its linked embedding archive is the accepted ordinary build, not a newly instrumented full core. The first leak-check attempt hit the sandbox ptrace restriction; the permitted rerun passed with leak detection enabled. These checks prove this mechanism, **not** a
   production instruction/CPU speedup. A follow-up should change only canonical
   insertion, then measure all general callers and certify before KEEP.
2. **Owned scalar key storage.** Consider one contiguous owned-key vector with
   immutable original indices, or a measured single-key specialization. Preserve
   all keys before sorting, stable ties, multi-spec factory order, incomparable
   key diagnostics, source-array independence and failure cleanup. The measured
   per-item key allocations are real, but payoff and complexity are unproven.
   No output aliasing or borrowing from a mutating receiver is proposed.

Do not conflate either opportunity with dropping captures or reusing logical
callable identity. No additional production experiment was implemented or kept.
Global hoisting and free-variable-only capture elimination are DROP based on the
contract counterexamples. The insertion microreproducer is retained as diagnostic
evidence for the next bounded trial, not as certified production optimization.

## CP49-57: remaining general calls

Fresh counters show scalar calls still create 4,000 scopes for 2,000 calls
(including prepared loop iteration scopes), plus 2,000 source contexts and
parameter/value/shared-slot work. They have only seven outer compatibility
entries and 15 recursive compatibility entries, so a generic legacy-parser
rewrite is not their first residual target.

Variable-held closures have 2,009 outer compatibility entries / 8,019 recursive
entries, despite 2,000 prepared numeric bodies. Named calls through a callable
parameter have 2,008 / 6,017 and 6,000 scopes. This is dispatch/adapter/frame work
outside the already prepared body. [Fresh dispatch counts](dispatch-counts.json)
and call profiles expose the distinction. The pre-existing failed scope-reserve
experiment remains closed. Reusable frames, arenas, compiler-wide slot IDs or
symbol-table redesign would require the architectural decisions above.

## Final recommendation and stop

A **SAFE THIRD BOUNDED TRANCHE** is justified for the discarded default-slot
insertion mechanism, with owned-key layout a separately measured second candidate.
It should not become another broad sweep. Larger identity-sort gains from capture
sets, plan/instance separation or registry lifetime require design review first.
No class-C instance-reuse proof exists, and current semantics forbid global
factory hoisting or free-variable-only capture elimination.

Native production source/binary and ABI remain exactly the hosted-green wave-2
runtime; all its local and hosted gates remain applicable. This checkpoint adds
only local measurement tools/evidence and tests of the current contract. No new
full runtime certification claim is needed for a production patch because there
is no production patch. Official benchmarks remain deferred until after v4.9
release. **STOP: no further optimization or architecture implementation started.**
