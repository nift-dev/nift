# CP19 S1 R1 — bounded correction record

Status: correction candidate for focused independent review; not accepted or integrated.
Parent is the rejected, preserved `378d231f0487e2c22cb55fe1f72d9f236da26633`, tree `829fcf2f6382ddf27501b4ba9b27daaf8bece89a`. Its branch, original documentation/evidence, delivery bundle and reviewer artifacts remain unchanged. R1 branch: `codex/cp19-shared-entry-r1`.

This record supersedes the parent record's broad lambda-free eval eligibility claim only for the restrictions below. No accepted golden, previous corpus, ignore list, Error representation or release threshold changes.

## R1 — nested Error aggregate presentation

Chosen correction: conservative whole-expression refusal, not a change to the shared CP14 native serializer. The serializer still does not support nested Errors; this candidate must not expose that defect through newly selected eval execution.

After successful compilation and before VM creation, `refuse_eval_error_presentation` scans all generated protos for ErrorNew and native stringify/prettify/highlight calls. Co-occurrence refuses the entire eval unit as `eval:error-aggregate-presentation`. The scan is deliberately conservative rather than claiming dataflow proof: it can refuse unrelated Error construction plus ordinary native formatting in one unit. Lambda/statement/import flows remain excluded by S1; admitted Error values originate at ErrorNew. The post-CP17/CP18 admission/lifetime reconciliation remains required.

Direct Error and nullable Error presentation lowers to ErrorJson rather than these native calls and stays admitted. Ordinary aggregate formatting without Error construction stays admitted. Tests cover array/object, deeper nesting, all three methods, print preceding the failing formatting and touch markers. Mode 1 matches legacy exactly after selecting it before effects; require/classification perform neither VM nor legacy execution and leave no output or marker. No partial-VM recovery/replay is added.

## R2 — transparent synthetic Return

`return_statement` attaches kInstrReturnCall for genuine script/function returns, but not eval's synthetic result boundary. No global message stripping occurs. Original source offsets and Error provenance remain untouched. Numeric/bool/null malformed callback cases for map, filter, find, sort_by, find_index and group_by remain admitted runtime errors with exact legacy text and exit 2; a multiline case verifies `<eval>:2:3` statistics.

Quoted callback/selector arguments have a separate legacy validation contract. S1 refuses string-capable callback/selector arguments at compile time as `eval:quoted-callback`, rather than merely removing a prefix from a still-wrong error. This is conservative even for valid quoted sort_by selectors. Tests include both malformed `map("x")` and a valid quoted selector, plus a preceding touch effect.

A real separately compiled compiler mutant restores the synthetic kInstrReturnCall flag. Its healthy VM execution produces the wrong `return:` prefix and is rejected by the exact-message gate. Original script and function return failures keep their prefixes and match legacy on both dispatches. The earlier real CLI legacy-bypass mutant remains detected by affirmative execution counts.

## R3 — nullable Error-member access

The expression compiler refuses member reads whose Error receiver kind includes Null, as `eval:nullable-error-member`. This is scoped to expression_result_ and does not modify generic VM member diagnostics, script compilation, Error objects, catches, handler selection or origins.

Absent cause message/code/source reads and equivalent present-cause reads are tested. The present-cause flows are conservatively refused too because existing kind inference retains Null; this is not a claim that the cause is absent. Direct successful Error metadata reads remain admitted. Accepted nullable and present Error stringify/prettify/highlight operations remain admitted and match legacy. Side-effect controls prove selection before evaluation for refused flows. These refusals remain coverage debt, never successful require-mode VM coverage.

## Evidence and reproducibility

The immutable final commit/tree and artifact identities are in the external R1 text handover. `docs/evidence/runtime2-cp19-r1/verification.json` records source/binary hashes, compiler flags, raw result hashes and exact local gates. Raw evidence includes all original review observations and the rejection report. Reviewer sources are copied byte-for-byte; their reruns here are developer verification, not a new independent acceptance.

- Original CP19 suite unchanged: 242 process observations on each dispatch.
- R1 corrections: 183 observations on each dispatch, asserting exact admitted parity and affirmative execution; refusals assert exact legacy behavior under mode 1 and zero executions/effects under require/classify/classify-all.
- Original reviewer matrix unchanged: 135 observations per dispatch, with an added assertion-only analyzer; no executed legacy/VM differences permitted. Refusals are recorded separately.
- Original ownership unit: 1,426 checks. Reviewer extended COW/result-teardown/repeated-entry unit: 1,561 checks on each normal dispatch, and each dispatch under targeted ASan/LSan/UBSan. A test-only forced-include header supplies Windows environment setters without changing the frozen reviewer source; Windows execution remains unverified. No full sanitizer/certification claim.
- Existing CP13B 80-case modes, harness/trace/adversarial controls and nullable Error presentation; CP14B functional/effect/origin/sort/witness regressions; shell/classifier/completion; runtime lint, test-integrity, header collision and unchanged legacy names.
- Relevant real mutants: compiled synthetic-return and legacy-route controls plus the existing CP14B mutation gate. Exact results are in the manifest rather than inferred from test target names.

Two new fixture-development assertions were corrected before freezing: an aggregate result containing Error is a successful VM execution followed by CLI serialization rejection (not vm-error), and the genuine-function-return control must use Nift's `fn(f())` syntax. Neither correction changed production behavior or existing goldens. Final raw rows are preserved with hashes.

## Scope and integration

No production changes beyond these S1 corrections. Persistent shell execution, actual callback execution/admission, rc compilation, universal expression refusal inventory and default VM routing remain unfinished. CP18 program API and CP17/N2/W13 require the same owner reconciliation and combined-tree checks documented in S1. CP21 jobs/completion/history ownership is unchanged. No integration, push, CI, release, infrastructure or spending occurred. macOS/Windows/.NET and milestone certification remain unverified by this round.
