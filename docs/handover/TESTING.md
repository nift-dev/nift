# Nift testing handover

## Test architecture

Nift has two complementary layers:

```text
this repository/tests
    implementation-local C++ and executable integration tests

nift-regression-suite
    implementation-independent black-box behavioral contract
```

Local tests may know internal APIs. The external suite must remain usable against
an arbitrary executable implementing the same observable contract.

## Current local entry points

The Makefile defines focused targets covering direct JSON/Schema behavior and
executable-level parser, comments, JSON binding, control flow, requirements,
path security/safety, metadata safety, cross-feature behavior, incremental new
features, persistence/concurrency failure, and Minify++ integration. It also
delegates to embedded Minify++ tests and provides scaling/performance targets.
Read the current Makefile for the exact list.

The external suite's `run-contract.sh` runs the historical/ruthless suite in a
disposable copy and then focused contract modules. Performance remains separate
because timing and RSS are environment-sensitive.

Reconciliation run on 2026-08-16: the current Nift executable passed all 14
standalone contract modules. This includes the external suite's 578-assertion
historical/ruthless layer and every focused module for JSON/schema, parser/content,
comments, binding/control flow, requirements, path and metadata safety,
cross-feature behavior, incrementality, persistence/concurrency failure, and
Minify++ integration. This validates the present external contract; optional
performance/RSS scripts and sanitizer builds were not part of this run.

The focused shell modules exist both under the implementation repository's
`tests/` and the independent suite's `contract/`. Their ownership/synchronization
policy should be made machine-checkable so copied tests do not silently diverge.

## Mandatory local/external coverage review

Every new externally observable behavior must be mapped deliberately to both
test layers before checkpoint completion. This includes syntax and configuration
changes, generated scaffold contents, CLI output/status, filesystem effects, and
incremental dependency transitions—not only parser output.

1. Add or update the focused implementation-local test and expose it through an
   appropriate Makefile target.
2. Add independent black-box coverage to `nift-regression-suite/contract/` and
   register the module in that repository's `run-contract.sh`.
3. Run the local focused/high-risk tests and the complete external contract
   against the same candidate executable.
4. Record both test locations in the checkpoint report. If one layer cannot
   reasonably test the change, document the specific reason.

A test present only in one repository is not automatically mirrored coverage,
and an external module not registered by `run-contract.sh` is not part of the
canonical contract run.

## Testing principles

- Test failure families, not vanity counts.
- Preserve each important bug as a deterministic regression.
- Test success status, failure status, filesystem/output/state transitions, and
  incremental decisions—not output bytes alone.
- Prefer controlled mtimes, paths, inputs, and environments over sleeps/luck.
- Keep cases isolated except where a lifecycle sequence is the behavior.
- Run old coverage after focused tests; new semantics inherit prior obligations.
- Change old tests only for deliberate contract changes, with rationale and docs.

## Historical lessons

| Historical issue | Durable lesson |
|---|---|
| `@content<` boundary | Define valid token characters positively; test adjacency. |
| CSS `@media` interference | Unknown web syntax should pass through. |
| Backtick ambiguity | Keep quote grammar small and explicit. |
| Empty/malformed tracking JSON | Persisted state is untrusted; test zero items and wrong types. |
| Quoted titles corrupting JSON | Serialize user metadata correctly. |
| Watch first-build state | Test complete lifecycle, not isolated save helpers. |
| Error printed with success status | Exit status is contractual. |
| Deleted generated output | Source unchanged does not imply project valid. |
| Directory/hash cache behavior | Repeated edits and directory dependencies need lifecycle tests. |
| Same-second fixture flake | Control sub-second mtimes deterministically. |
| Demonstrated narrow hash collision | Construct hostile evidence; do not assume collisions away. |
| Traversal/collisions | Paths and derived outputs are security/correctness boundaries. |
| O(n²) tracked validation | Safety work needs scaling guards. |
| Hash-table memory spike | Measure memory and object lifetime as well as CPU. |

Where practical, link durable documentation to the actual regression protecting
the behavior. Do not delete strange tests without understanding their history.

## High-risk interactions

Future defects are likely at boundaries such as JSON plus incrementality, loops
plus lexical scope, sorting plus loop metadata, schemas plus data transitions,
Minify++ plus output transactionality, symlinks plus cached containment, and
dynamic external assets plus requirements.

## Parameter interpolation contract family

The `$[...]` textual-parameter feature is specified externally and implemented.
Its protected categories include whole values, literal/value mixing,
multiple and adjacent values, both quote styles, exact existing escaping, nested
value paths, loop scope/shadowing, skipped branches, missing/wrong types,
malformed expressions, and no accidental output emission.

Directive integration should cover each semantically textual argument while
leaving binding identifiers/control grammar static. Resolved quotes, commas,
parentheses, `@...`, and `$[...]` must remain data and must not change argument
boundaries or recursively execute.

The defining lifecycle test is:

```text
selector chooses A → build
selector changes to B → rebuild
changing A no longer matters
changing B matters
missing B fails normally
repair recovers without manual state deletion
```

Apply the analogous test to dependencies, requirements, and dynamic JSON sources
where supported. Verify failed builds preserve prior output/metadata according to
the existing transaction contract.

The independent suite owns this contract in
`contract/parameter_interpolation_smoke.sh`. It now passes 73 checks against the
implementation. All 15 contract modules, including the 578-assertion historical
layer, are green. The suite runner's temporary-directory isolation was also
hardened with `mktemp -d` after PID reuse exposed stale-directory collisions.

## Sanitizers and fuzzing

ASan/UBSan are particularly relevant to parser indexing, string lifetimes,
filesystem/state handling, and native minification. Record exact flags and
workload. TSan is relevant only when shared mutable behavior changes. Parser,
state, and Minify++ fuzzing remain valuable future layers; minimized findings
should become deterministic regressions.

## Performance evidence

Use `PERFORMANCE.md` and current scripts. Prefer scaling ratios and repeated
checkpoint measurements to brittle absolute thresholds. Never weaken validation,
containment, or dependency checking merely to improve a benchmark.

## Project-contract checkpoint (2026-08-17)

Project contracts are tested as an observable guarantee rather than only as a parser feature. The focused `tests/contracts_smoke.sh` module is mirrored into the standalone suite and covers successful resolution, lazy loading, dependency/config remapping, parameter/control-flow integration, missing/malformed sources, missing members, render-type errors, local JSON compatibility, namespace collisions, invalid declarations, traversal, and symlink escape. Preserve those failure families and add permanent reproducers for any new contract defect.
