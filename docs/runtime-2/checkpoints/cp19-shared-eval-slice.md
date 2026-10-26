# CP19 — shared expression entry, bounded implementation slice

Status: implemented and locally verified; independent acceptance and integration pending.
Base: `45b22cdcc8fa2e70c2ce44a2a5244c0f8a4b555e`, tree `bf44b84f06eea823dcf9cf4b80518899f62da059`.
Branch: `codex/cp19-shared-entry-s1`, separate worktree. This is the first shared-entry implementation slice after the accepted classifier slice; it does not complete CP19.

## Result and boundary

`nift eval` now observes the existing `NIFT_VM` selection contract through the same runner as native file, inline and stdin scripts. Unset/0 retains legacy execution. Mode 1 runs a supported expression once on the VM or selects legacy before execution. Require refuses an unsupported expression without either engine executing it; this is not VM coverage. Classify never executes. An executed expression's runtime/internal failure cannot retry on legacy.

The supported surface is an entire expression that the existing Runtime 2 compiler admits, without any lambda node: scalar literals/operators, arrays and objects, supported read-only collection/string methods, supported Error construction/member/presentation operations, and currently admitted handle-free Host operations. This is an eligibility rule, not a claim every spelling in those families compiles. The tests require affirmative execution for each positive fixture. Plain and `--json` rendering retain the CLI's existing RuntimeValue formatting and byte/Error/timer serialization checks.

Examples positively verified include `1+2`, `[1,2,3]`, nested objects, array unique/flatten/sum, object keys/get, string uppercase, `pwd`, `open`, `exists`, `getenv`, `setenv`, and `print`. Host output occurs exactly once, including output preceding an error. New Error values retain empty source/zero line/column until raised, as in legacy. Existing Error identity, origin, handlers and unwinding are unchanged. `<eval>` source positions in VM statistics refer to the unmodified expression, including multiline positions; the CLI keeps legacy `eval: message`/exit 2 for expression runtime errors. Require refusals retain exit 1 and internal stops exit 70.

Assignment/declaration expressions, callable-containing expressions (including callback collection operations), unsupported member forms, existing bytes/timer compiler refusals and other existing compiler refusals remain outside this slice. They produce actual fallback accounting under mode 1, or an execution-free require-error. Neither is counted as a pass. Eval classify-all explicitly reports `inventory_complete=false` with `expression-inventory-not-implemented`; the script inventory remains unchanged. Supported native standalone file/inline/stdin paths retain their existing coverage and mode controls; this slice does not enable the VM by default.

Stateful interactive statements, user-function command dispatch, interpolation against persistent bindings, `-i` continuation and rc compilation remain legacy CP19 obligations. External shell commands continue through the existing classifier/CP20 path and do not become VM units. Website build/page/template/hook routing is untouched. No CP21 implementation is included.

## Architecture and lifetime

The expression parser consumes the original source. `compile_expression_unit` adds synthetic Program/Return AST structure without prepending source or shifting any offsets. It reuses unit planning, depth checks, kind inference, prepared-context refusals and bytecode lowering. Only this explicit expression compilation entry lifts the scalar-only top-level script-return restriction. Ordinary `compile_unit` still refuses `return [1,2]`.

`VmOptions::return_value` optionally captures a Value in the existing cold TopReturn handler. It adds no instruction-dispatch or ordinary call-entry work, no opcode, no Frame change, no fault slot, and no reentrant VM call. Without that pointer, script return rendering is unchanged. Data values are owned independently of the VM; callable values retain the existing Program-lifetime requirement, so the CLI conservatively refuses lambda-containing units before execution.

The CLI bridge to RuntimeValue is confined to result conversion while the compiled Program, SourceMap and VM remain alive. It is not used per element/instruction. Existing eval serialization remains the single formatting authority. Unexpected conversion/allocation failures cannot fall back after execution. Cold runner statistics moved verbatim into `script_runner_stats.{h,cpp}` so the shared entry remains within the existing lint limits; reporting schema is unchanged.

## Verification

Commands and preserved raw results are indexed in `docs/evidence/runtime2-cp19-shared-eval/verification.json`.

- `make test-cp19-shared-eval`: ownership unit (1,426 checks); 242 process observations on each of computed-goto and switch dispatch; a separately compiled CLI mutant restores the legacy-only bypass and is detected despite identical successful output.
- Positive cases assert exact stdout/stderr/status parity with the same binary's unchanged legacy evaluator AND one VM execution, zero legacy executions, expected outcome. Fallback, require-error and classification have separate accounting assertions. A deliberately tiny VM heap budget must stop internally without replay.
- Ownership unit retains aggregate results and copies after VM/compiler/AST/source teardown, repeats teardown 200 times, preserves fresh Error metadata, and checks ordinary scalar/script aggregate-return behaviour.
- `make test-cp19-expression-sanitize`: the ownership unit under ASan, leak checking and UBSan; 1,426 checks, zero failures. This is a targeted sanitizer tier, not full milestone certification.
- Existing CP13B functional tier: 80 fixtures in legacy/current/future and switch future modes, harness/trace/adversarial/build self-tests, nullable Error presentation.
- Existing shell classifier unit: 86 checks. Existing shell dispatch regression and PTY completion categories pass.
- Existing differential corpus: 820 entries, zero differing, zero approved differences. This same-binary determinism gate complements the new legacy/VM parity checks; it is not itself proof of zero fallback.
- Runtime lint/self-tests, legacy-name extraction, header collision scan and test-integrity controls pass. No existing corpus/expectation or ignore file was reduced or changed.

The first new ownership assertion incorrectly expected freshly constructed Errors to have a source origin. Correcting that test preserved the actual legacy contract; runtime code was not changed to satisfy the erroneous assertion. Initial CP13B invocation lacked its parser-tool prerequisite, then sandboxed timeout children received permission errors. Initial sanitizer libffi configure probes could not execute in the sandbox. Prerequisites were built and affected checks rerun with approved local execution outside the sandbox. A completion invocation initially used an unsupported `--nift` option; its positional invocation passed. Earlier logs are preserved separately from final evidence. No failed attempt is presented as a pass.

No performance campaign, million-catch instruction measurement, full sanitizer/milestone certification, macOS, Windows, .NET, push, CI, infrastructure or integration operation is claimed. Comparator-specific CP14B qualifications remain unchanged.

## Integration reconciliation and remaining CP19 work

CP18 and CP17/N2 are absent from this base. Do not replay this candidate directly into authoritative runtime-2 without reconciliation and review.

- CP18: preserve its accepted program/context API, ownership after close, diagnostics, heap-budget witness and libffi test correction. This slice does not call or replace that API. Merge the appended VmOptions result pointer and shared runner/compiler entry against the actual landed files; rerun API/collection/COW/Error and result-after-close witnesses.
- CP17/N2/W13: preserve final handle ownership/observer/cancellation and abandoned-frame semantics. No unintegrated interfaces or temporary handle adapters were introduced here. Requalify Host effects and exceptions on the actual combined baseline. Do not treat this eval result bridge as typed-handle migration.
- Classifier: `src/shell/CommandDispatch.*`, shell command selection, completion/history, process spawning and job control are unchanged. External commands have an explicit no-VM-record control; the existing classifier/dispatch/completion tests remain required.
- Finish stateful shared shell compilation/execution, persistent binding/user-function visibility, command interpolation, `-i` state continuation, and rc compilation within CP19. Coordinate CLI edits with CP21's owner through the owner/Claude; no direct agent messages occurred.
- Inventory and lift expression refusals through independently reviewable semantic work, particularly assignments and callable results/callback expressions; complete expression refusal inventory rather than marking classify-all complete.
- Reconcile supported-entry/corpus coverage under the proposed CP25-R boundary. This opt-in slice does not satisfy release-wide fallback retirement or enable default VM routing.

Independent reviewer should verify whole-expression engine selection before effects, immutable original source offsets, result lifetime and Error metadata, unchanged script-return restrictions and both-dispatch parity. Owner/Claude retain all landing/integration authority. Exact final candidate commit/tree and evidence hashes are supplied in the external text handover because a commit cannot contain its own identity.
