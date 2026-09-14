# Performance sequence and optimizer phases 3–4

This checkpoint applied the pre-Phase-3 performance sequence to every public
format and then implemented bounded Phase 3 and Phase 4 optimizer subsets. A
performance change was retained only when repeated same-process measurements
showed a material improvement (normally at least 3%) in an affected workload
without a conformance regression.

## Retained performance work

- Removed a redundant reconstructed JavaScript-token copy and ordering scan.
- Replaced locale-aware character classification in scanner hot paths with
  direct ASCII classification while retaining the existing byte-preserving
  non-ASCII path.
- Stored JavaScript token and binding spellings as source-backed
  `std::string_view` spans, allocating only transformed spellings.

A compatibility-wrapper experiment regressed structured and aggressive
JavaScript by roughly 3–4% and was discarded. Pointer cursors and numeric
conversion were not adopted without profile evidence. The shared ASCII change
benefits the HTML, CSS, JavaScript, JSX, JSON, XML and SVG scanners; the span
change targets the analysis-heavy JavaScript/JSX path.

On the same Linux runner, the 40-iteration synthetic benchmark changed from
the pre-sequence baseline as follows. These are indicative local medians, not
cross-machine claims.

| Workload | Before MiB/s | After MiB/s | Change |
|---|---:|---:|---:|
| HTML | 65.5 | 81.6 | +24.6% |
| CSS | 136.4 | 149.0 | +9.2% |
| JavaScript | 55.5 | 66.0 | +18.9% |
| JavaScript scope | 78.5 | 97.9 | +24.7% |
| Structured JS scope | 11.0 | 13.8 | +25.5% |
| Aggressive JavaScript | 5.4 | 5.0 | -7.4% |
| JSX | 53.9 | 58.9 | +9.3% |
| JSON | 88.2 | 86.7 | -1.7% |
| XML | 147.8 | 151.7 | +2.6% |
| SVG | 154.7 | 152.9 | -1.2% |

The aggressive throughput regression is the cost of additional optimization
passes, not a retained scanner micro-optimization. JSON/XML/SVG changes are
within the local noise threshold and no format-specific optimization is claimed.

## Phase 3: structured mangling subset

The structured allocator now covers safe concise-arrow parameters, simple
object and array destructuring parameters, and name reuse across proven-disjoint
lexical scopes. JSX receives the same transformations only in explicitly
enabled structured expression regions.

Captured-binding renaming was prototyped and rejected. Although Test262 stayed
green, Moment's executable artifact exposed a collision between an outer
captured binding and an independently allocated nested parameter. Captured
bindings remain excluded until nested functions share a coordinated allocator.

This is not the full structured roadmap. Complete destructuring/defaults,
module bindings, classes, nested-capture allocation, a full parser/printer and
the remaining scope-graph coverage are still open.

## Phase 4: aggressive compression subset

Aggressive mode additionally performs bounded logical-constant selection,
exact safe-integer modulo/bitwise/comparison folds, adjacent `var` joins,
simple conditional-return compression and context-safe unresolved `undefined`
shortening. These join the earlier constant, conditional, compound-assignment,
unreachable-debugger and literal-IIFE transforms.

Two unsound implementations were rejected by executable bundle gates. Unused
local-variable removal relied on incomplete reference resolution and removed
Terser's live `pure_funcs` binding. Declaration joining also needed an explicit
`for`/`for-in`/`for-of` boundary guard. The latter was repaired and retained;
dead-binding elimination remains disabled until complete effect and reference
analysis exists.

Phase 4 therefore remains a controlled subset. General constant propagation,
control-flow graphs, dead-code and dead-binding elimination, sequence formation,
escape analysis, function transforms and inferred property mangling are not
claimed.

## Release evidence

- All 12 benchmark artifacts passed their runtime validators in conservative,
  structured and aggressive modes (36 validated outputs).
- Test262 selected 48,011 programs: 39,747 original/transformed passes, 8,264
  classified runtime incompatibilities and zero transformed failures.
- The TypeScript JSX/TSX oracle passed 221/221 cases.
- Product smoke, generated (15,459 programs), scope, module, structured,
  aggressive differential, real-bundle, formatting, cross-format, CLI and
  70,000-case deterministic fuzz gates passed.
- ASan/UBSan smoke, CLI and the same 70,000-case deterministic fuzz gate passed
  with leak detection disabled for this environment.
- JSON, XML and SVG harness unit/oracle tests passed. CSS passed 23 tests with
  12 Chromium-dependent oracle tests skipped. HTML's unit harness could not run
  its DOM comparisons because `html5lib` and its provenance metadata were not
  installed; this is an environment gap rather than a product-test failure.
