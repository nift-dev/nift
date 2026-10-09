# NIFT v4.10 — SORT-FIRST COMPETITIVENESS CAMPAIGN

Status: **local certification PASS; review boundary before follow-up publication or architecture implementation.**

The two previously accepted wins are committed and pushed at
`4d80ca47e94beb68544904e736a9f3415b303157`; all ten exact-SHA non-release
hosted workflows passed. The first Deep run exposed a hardcoded 4.9.0 contract
expectation after the authorized 4.10.0 bump. That expectation was corrected;
no assertions were weakened. The initial failure remains recorded.

Sort remains the primary performance target. This checkpoint delivers the
post-acceptance ownership decomposition and concrete callable plan/instance
proposal. Its broad implementation remains behind the requested review boundary.
The secondary runtime experiment is retained locally (KEEP): it replaces redundant member searches with one
canonical first-match lookup, including prepared AST and root-path refresh paths.
Canonical Jsonic++ has been investigated without changing either parser copy.

## Sort decision

See [callable architecture](callable-architecture.md) and
[controlled decomposition](sort-decomposition.json). Identity sort at N=16k uses
431,538,781 production instructions and 480,463 allocations/frees. Metadata
accounts for 34,304,731 diagnostic instructions (about 8% of whole-process cost),
64,000 allocations and 12,016,000 allocated bytes. Production Parser teardown
uses 42,673,741 instructions and frees 176,025 blocks. Phase scopes overlap;
these numbers must not be added into a predicted speedup.

The identity body itself allocates nothing; factories, all-visible capture
insertion, callback frames and retained instances remain expensive. The explicit
pre-created control reduces instructions but is not an equivalent replacement
for an inline factory. No global hoisting, free-variable-only capture, changed
identity, registry reclamation, shared mutable frame, or source-text-only cache
is proposed. Syntax/origin sharing is category B for review; capture/frame/escape
ownership is category C. Small key/default-slot changes have a limited ceiling.
Metadata sharing alone will not close the roughly 50× Python headline gap.

Exact sort contracts: 31 new factory/capture/module/async/origin cases, plus the
existing 58 selector/location/error contracts. The proposal specifies immutable
syntax, parser-owned defining provenance, fresh instance identity, complete
captures, module ownership and effective async state. Fixed-length paths control
source-location allocation effects. Preliminary filename-boundary and component
runs are retained and explicitly excluded from the final scaling claim.

## One-fetch object experiment

One immediate first-match lookup replaces `has` followed by `operator[]`. Storage
and public ABI/layout are unchanged. No persistent index or pointer cache is
introduced. Copies are staged before overwriting a containing aggregate;
prepared AST aliases retain the existing owner. Root/path references are refreshed
after mutation. Missing-member diagnostics and insertion/promotion semantics
remain intact.

Actual diagnostic traversals halve for successful reads at all six widths;
missing lookups already used one scan and remain unchanged. Prepared selectors
have two separate semantic lookups (read and captured-binding location refresh):
both now use one traversal. Callgrind attributes the refresh to
`VariableBinding::sync`. The guard's legacy negative control fails as expected.
The new exact oracle covers 86 value, alias, capture, root/path growth, erase,
reinsert, missing-key, Unicode and error-origin cases; the C++ unit also
checks first-match duplicate keys and every RuntimeValue category.

| Width | Last-read instruction change | Before CPU ms | After CPU ms | Allocations saved | RSS before/after KiB |
|---:|---:|---:|---:|---:|---:|
| 8 | -4.69% | 3.07 | 3.08 | 0 | 7276/7072 |
| 32 | -11.02% | 4.94 | 4.97 | 0 | 7296/7300 |
| 128 | -15.63% | 5.75 | 4.87 | 0 | 7304/7332 |
| 512 | -37.22% | 15.77 | 12.06 | 0 | 7696/7928 |
| 2000 | -36.46% | 34.09 | 23.91 | 0 | 7784/7900 |
| 8000 | -24.91% | 243.44 | 179.36 | 0 | 10644/10472 |

These whole-process probes include literal construction. The 8k first/middle/
last/missing matrix does not claim constant-time access or linear construction.
CPU figures are 14 warmed alternating paired process samples; short small-object
CPU results are noisy. RSS is a one-process observation, not a memory improvement
claim. Successful wide reads show the material win; first-member/missing accesses
and ordinary controls remain close to neutral. [Full metrics](object-metrics.json)
retain all 55 paired cases, every sample, allocation count and first/middle/last/missing result.

Ordinary controls span sort, map/set, aggregate callbacks, BFS, loops, calls and
JSON parse/traverse/mutate/serialize. The largest instruction regression is
+0.85% on wide JSON; official-equivalent sort is +0.10%, ordinary BFS −0.04%.
Allocations are unchanged. Initial short frequency/map-set CPU outliers were
retained and investigated with 32k controls: frequency 59.81→58.15 ms, map/set
67.40→67.38 ms, scalar calls 30.48→30.75 ms. They do not support a sustained
regression claim; short-process timing noise is not used as a CI gate.

## Jsonic++ decision

[Canonical proposal](jsonic-proposal.md): strict unique 8k object parsing uses
1,030,717,909 instructions versus 9,175,144 in Preserve mode, with identical
allocation counts. The 36-case scaling matrix and four escaped-key/error-
precedence probes show the independent quadratic duplicate-check problem.
Propose a parse-local membership table above a measured threshold while retaining
ordered members and exact duplicate/error precedence. Implementation and vendored
synchronization have not started.

## Validation

Native and all language binding tests: PASS. First-party GCC/Clang and binding
warning checks: PASS, zero warnings. External NRS: 93/93; PRS: 12/12.
All 110 object/control Memcheck profiles: zero errors and zero live heap blocks.
Lifetime ASan/UBSan/LSan, exact sanitized object/sort oracles, root-path
corruption reproducers, async/future coverage and 57 core-memory phases: PASS.
Deep sanitizer and parser fuzz: PASS, 1,219 cases (232 successful builds and
987 controlled syntax errors).

## Evidence and boundary

Production profiles, zero-error Memcheck logs, exact probe sources, diagnostic
instrumentation and preliminary studies are retained in `profiles.tar.gz`.
Build commands/harnesses retain original scratch paths and are run on accepted
source before the object patch; maintained semantic guards are `make test-v410-*`.
Candidate source is captured in `candidate.patch`; no new follow-up commit/push
has been made. The accepted pushed certificate is separate from local follow-up
certification.

Version 4.10.0; public ABI 1.3. Frozen official `20261009-v490`: UNCHANGED,
no rerun. Benchmark repositories and all 30 frozen campaign files remain intact.
This campaign made no Labs edits. Concurrent external Labs presentation/evidence
relocation was preserved: 322 indexed copies and the publication receipt match
the original bytes in canonical `nift-experiments/lab-evidence`. One superseded
non-official diagnostic archive was replaced externally by a pinned README
recording its original hash; this is explicitly recorded, not called byte-identical.
All 323 current canonical targets match the pinned relocation index.
See [relocation audit](frozen-relocation-audit.json). The whole externally edited
Labs checkout is not claimed unchanged.

Recommendation: review the callable syntax/origin plan split first, with a
separate explicit capture/frame ownership checkpoint for material sort gains.
The object experiment is secondary. Do not claim practical peer competitiveness
or begin broad architecture work before review.

### Validation environment

The first full-suite attempt could not run the pagination syscall-order test:
`strace true` independently reproduced `PTRACE_TRACEME: Operation not permitted`.
The complete gate was rerun with tracing permission; pagination passed with
output sequence 1, stale cleanup 3, and info write 4. The restricted failure,
debug trace and permission proof are retained. No test was weakened or skipped.

The GCC 15 `-O1` deep-sanitizer compile retains the same `std::stable_sort`
`-Wmaybe-uninitialized` library diagnostic recorded in the accepted baseline
(`../cp410-competitiveness/checks/baseline-sanitizer-compiler.log`). Production
GCC/Clang `-Werror` warning gates are clean. No warning suppression was added.

### Experiment decisions

- Accepted capture insertion and native-name probes: KEEP, pushed and hosted-green.
- One-fetch hot object reads and location refresh: KEEP locally, exact contracts
  and all safety gates pass; material wide-read benefit and neutral instruction controls.
- Global factory hoisting/free-variable-only capture: REJECT, changes observable semantics.
- Callable syntax/origin plan: DESIGN ONLY, category B for review; no implementation.
- Frame/capture overlay and registry reclamation: DEFER for ownership proof, category C.
- Small key/default-slot alternatives: not forced; limited measured ceiling.
- Canonical Jsonic++ parse-local membership index: PROPOSAL ONLY; no parser edits.

STOP FOR REVIEW. The uncommitted runtime candidate and full proposal are ready
for a concrete acceptance decision.
