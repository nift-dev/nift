# CP04 — Top-level programs on the prepared path

Status: **deferred by owner decision D-006** until CP08 (semantics spec and
differential harness) exists. Not committed. The experimental patch
(`cp04-wip.patch`, applies to `5c0aece`) and its full test log are kept outside
the branch, in the delivery folder `.runtime-2-delivery/cp04-preserved/`, so no
experimental change ships on `runtime-2`. Its 44 probes are in the VM regression
corpus (`tests/runtime2/vm-corpus/cp04/`).

## Root cause
A standalone script is translated to template text and each top-level
statement goes through the canonical textual evaluator; every user-function
call re-translates and re-parses the body (`ParserScript.cpp:402`). Loop bodies
use a separate prepared statement evaluator, built once per loop, whose
prepared calls dispatch without re-parsing. That evaluator is defined inside
`Parser::parse`'s per-character scanner loop after the `$[`/`@fn` handlers, so
top-level statements never reach it.

## Attempt
- Hoisted the prepared evaluator's setup out of the scanner loop (per-position
  state reset each step).
- In strict script mode, routed `$[...]` statements that are plain calls or
  `name := expr` declarations through the prepared path; errors and rendering
  via a shared `render_leaf_value`; `NIFT_RT2_LEGACY_STATEMENTS=1` reverts.
- Aligned several loop-path divergences toward canonical behaviour (string
  interpolation inside loops, `const` mutability in loops, builtin vs
  same-named user function, assignment/return error wording, error location
  inside called functions).
- 44-case parity corpus (`tests/runtime2/toplevel_prepared.sh`, in the patch).

## Measurements (callgrind)

| Measure | Baseline / runtime-2 | CP04 WIP | Gate |
| --- | ---: | ---: | --- |
| fib(22) top level | 10,751,395,898 | 868,516,746 (12.4× fewer) | ≥ 8×: met |
| fib(22) top level, wall | 1.34 s | 0.11 s | — |
| 300-page full build | 848,334,459 | 835,045,023 (−1.6%) | met |
| 300-page no-op build | 114,914,079 | 114,917,433 (+0.003%) | met |
| fib(22) inside a loop | 863,023,251 | 870,488,590 (+0.9%) | — |

## Why it is blocked
The two evaluators differ in semantics and diagnostics. Inside loops this is
hidden because admission is narrow; at top level the existing suite exposes
it: 16 targets fail beyond the 6 environmental ones
(`../evidence/cp04/failing-targets.txt`), including non-error semantics —
`error("x").message`, struct member resolution, map equality, an embedded-script
host binding, warning output — and `test-call-semantics` encodes the loop
path's old wording, which the alignment contradicts. 4 of 44 parity probes
still differ (fatal-error diagnostics). The 8× gate is met only by changing
behaviour, which the acceptance criteria forbid.

## Decision (D-006, owner)
Defer. Do not merge the legacy evaluators to pass this gate; the measurements
are evidence for the new VM, and the divergences are regression corpus for it.
