# Runtime 2.0 post-merge engineering

The release correction is under D-180. D-181 closes the 97 filesystem
recipes with 59 VM execution matches, 38 verified refusals, and 97 explicit
legacy matches. The original 102-row oracle remains sealed.

## Downstream work

| Root cause | Evidence and disposition |
| --- | --- |
| Free-call diagnostics | The native-dispatch gate passes 15 cases. The dead-code admission gate passes all 63 controls. |
| Growing a captured loop source | Direct array append, read-only lambda calls, and single-selector array sorting now pass the relevant growth cases. Clear, replacement, and mutating callable controls remain conservative. |
| Binding precedence | A bare read of the colliding name can resolve to the named function in legacy, while a call can resolve to the local binding. The VM's lexical lambda parameter result differs from the historical oracle. D-009 addresses built-in precedence. Keep the collision cases open. |
| Reserved declarations | The VM accepts `first` as a named-function parameter and as a receiver method. Ordinary declarations use the reserved-name gate. No approval extends the parameter exception to declarations. |
| Computed object targets | The historical prepared loop accepts a computed string key. A proof must also cover fallback to textual execution, evaluation order, and errors before widening the VM form. |
| Callbacks and module linking | Sort-factory still fails on module ownership, a nested returned factory, and a module member diagnostic. The callback overlay also retains the reserved-declaration failure. D-063 continues to govern keyed collection callbacks. |

## Hosted evidence

Historical run 45387812259 passed Linux and Windows. macOS passed the normal
terminal scenario and failed mutant 4's observation. The ownership audit
correction is in 9ebecaf0. The next run, 45416327694, failed earlier on macOS
when the shell smoke missed the continuation after `false`. The revised
fixture supplies an owned command, checks its marker and checks continuation.

FreeBSD run 45328750459 passed 48 exact-output comparisons and 86 applicable
VM checks. The original intermittent `open-2` omission remains without a
confirmed root cause. Preserve the mandatory exact-output assertion.

Historical run 45417780659 passed Windows and Linux. macOS passed the
revised shell smoke and reached the CP21 Make failure control. The control
now injects a failure in the common driver on both platforms and injects
the four Linux-specific fault branches only where Make runs them.

The numeric lambda member diagnostic matches the historical module case
and the direct and callback probes. Lambda block return operands retain
their separate diagnostic path. The nested factory inference prototype
remains reverted because it changed a reached closure error into a refusal.

The focused loop sanitizer gate passed all 57 cases outside the sandbox.
The earlier sandbox run failed when LeakSanitizer could not inspect threads.

The nested returned factory now stays pending until callable inference
finds its selector. The companion receiver proof preserves the existing
reached `push` error for an unchanged numeric closure result. The 55
closure differential cases and 1,163 safety oracle cases pass. The
sort-factory gate still reports named precedence and module ownership.

## Frozen candidate results

Commit `d73046c6965cfca9c1b24a187dd1d193b3bf6761`, tree
`5a3a4d5536ef2552ec5831b438937ae1ca456a78`. All six hosted workflows
completed before the correction pass began.

| Gate | Observed cause and contract | Smallest correction or remaining proof | Platforms |
| --- | --- | --- | --- |
| VM subset, 45448001595 and 45448012412 | The old member-admission witness now reaches the approved historical error and call-site position. CP25R still required two obsolete classify-all sightings, including one masked by the now-supported variadic lambda. FreeBSD reported 84 passes and two failures. | Pin the reached member diagnostic, use the historical variadic smoke in required-VM mode, and keep all remaining exact refusal checks and mutants. | Linux, macOS, Windows, FreeBSD |
| Checkpoint 10, 45448022508 | A lambda parameter collides with a named function. VM prints `int` and legacy prints `function`. There is no approved difference. Make stopped at the selector target. Later targets and cross-platform comparison were skipped. | Establish the precedence rule for this collision before changing admission, code generation, or the witness. Keep bare reads and calls in the controls. | Linux, macOS, Windows |
| Test Integrity, 45448037448 | GitHub reports loss of runner communication during the aggregate job. Go, C#, Node, Python and the explicit aggregate binding target passed. The parallel aggregate was in progress and the final two steps were pending. | Rerun on the correction candidate. The interrupted job is incomplete. | Linux |
| Historical compatibility, 45448029258 | Passed. | Preserve the exact-tip result. | Linux, macOS, Windows |
| Build-only packaging, 45448044488 | Passed. | Preserve the exact-tip result. | Hosted packaging matrix |
| Local CP14B sort effects | The old copy witness uses `b := a`. B2 reference semantics make this an alias, and owner requirement E protects the sorted receiver through every alias. | Keep the input and assert the catchable `runtime.sort_receiver_mutated` error. Separately prove a distinct array can be mutated by the comparator. | Linux verified comparator profile |
| Local CP14B replay and sanitizer replay | 32 record-array cases starting at 17 elements refuse with `method:sort` on the verified comparator profile. A reduced witness crosses the 16-site inference boundary. At 17 records the VM cannot prove the field types for `x.k < y.k`. Adding the trace read `x.stringify()` moves the first failure to the comparator effect check. The toolchain refusal contract does not account for these cases. | Preserve the numeric-field and read-only effect proofs across site merging and rerun the pinned replay and sanitizer replay. Keep the trace oracles. | Linux verified comparator profile |
| Local CP10 downstream beyond first hosted failure | Private module ownership is treated as a global name collision. `binding-shadows-fn`, the reserved `first` declaration, and a computed object write also still refuse. These exact witnesses have no approved exception. | Resolve the module binding by its owner while preserving visibility and collision checks for exports. Treat the callback admission cases separately from the computed-key proof and preserve the historical witnesses. | Linux reproduced |

The 97 filesystem-recipe contracts and the 102-row original oracle remain
sealed. The scoped local corrections passed CP25R including all three negative
controls, the five precise-position cases, and CP14B effects and origin
checks on both VM dispatch builds. The hosted results above apply only to the
frozen commit. The remaining compiler and sort-replay failures block integration.

### Consolidated correction investigation

The CP14B boundary is B4b's allocation-site budget. In
`Inference::classes`, the seventeenth known origin collapses the class and
marks it opened. `materialize_now` then clears the named fields and widens the
element contents. The reduced comparison loses the two numeric field facts.
The replay's callback also reads `x.stringify()`, for which the comparator
effect scan cannot prove a built-in read after widening. B4b's A31 mutant
specifically checks that arbitrary opened classes cannot retain precise
dispatch facts. A repair must keep that general rule and prove only the
comparator facts it uses. The existing relational opcode assumes safe
operand kinds and treats a mismatch as an internal defect. Admitting the
comparison unconditionally would therefore change more than coverage.

The binding probes distinguish bare reads, direct calls and fallback:

| Collision probe | Legacy | Required VM |
| --- | --- | --- |
| Parameter named `x`, body reads `x`, outer function also named `x` | `function` | `int` |
| Same parameter, body calls `x()` with an injected zero-argument lambda | `3` | `3` |
| Local lambda declaration named `x`, then `type(x)` and `x()` | `function`, then `3` | `binding-shadows-fn` before execution |
| Local lambda called with a counter-incrementing argument | `11`, then `1` | `binding-shadows-fn` before execution |
| Lambda calls outer function `x()` with no colliding binding | `8` | `8` |

Recommendation for the unresolved collision only: select the nearest lexical
binding for a bare read and a direct call. Use the named function when no
nearer binding exists. After selecting the binding, apply the existing
callable and argument-order rules. This preserves both engines' parameter
call and outer fallback results. It keeps the VM's `int` for the parameter
read and would allow the local lambda declaration, with one evaluation of
its argument. Retain independent legacy witnesses for the bare-read
differences. Reserved built-in names such as `first` have a separate rule.

Module ownership has an existing lexical contract under D-038 and the M2
module-function decision. The exact private binding collision is registered
as pending under D-067 #9. The observed implementation failure is in the
linker: it indexes declarations by spelling across the flattened graph
before splicing the units. By then the invocation planner can rename private
`args` and `cmd`, but ordinary private definitions have no equivalent
identity-preserving rename. The correction needs to resolve each use to
its defining module before assigning an internal name. Preserve imported
export collisions, local shadowing, and diagnostic source mappings.

### D-182 standalone lexical precedence

Standalone declarations and named-function parameters can now shadow a
named function. Existing lexical reads and callable-value lowering select
the binding. Interpolation consults the inferred lexical call type, and
coroutine admission distinguishes bound names from bare named async function
values. The session compiler still has phase-specific named function
resolution. Its existing collision refusals remain until that resolution
and the session inference path agree on binding identity.

The new selector controls cover bare reads, direct calls, named and lambda
parameters, interpolation, an async name shadowed by a synchronous lambda,
named fallback, and argument ordering. The non-callable binding evaluates an
argument once and reports the call-site error without invoking the named
function. The historical selector and sort-factory observations are executed
in explicit legacy mode. The callback shadowing fixture retains its
historical output and checks the deliberate D-182 output independently.

The local downstream runs still report an indexed returned-location mismatch,
a redeclaration in the callback parameter's own scope, and the private-module
binding collision. The computed-key capture-growth fixture exceeds the
plain-key forms approved for element paths. The reserved declaration case
remains within the existing reservation. No certification matrix was
dispatched for this partial correction.

### D-182 module ownership and downstream witnesses

The linker now gives a proven plain module-private declaration its own
internal spelling when it collides with an entry declaration. Renames use
AST identifier ranges and the existing source segments, and the original
entry spelling remains in the import-visibility check. Exported names and
ambiguous binding shapes continue through the original collision checks.
The bounded proof declines declarations with same-spelled local or parameter
bindings, and modules with raw command or template text and other binding
forms it does not resolve. Existing positional refusal controls still test
ambiguous collisions. The accepted-state sort/factory oracle executes the
original private-binding witness independently in legacy and VM modes.

The module gate passed 62 fixed and 100 generated cases. The sort/factory
gate passed all 31 cases. The package diagnostic group passed 67 checks.

The indexed-returned-location witness expected the legacy copy. Under B5
the returned child is a reference, so mutating it remains visible through
the parent after the parent grows. The selector suite keeps the historical
outputs and checks the B5 results separately for returned, mapped,
filtered, grouped, member-read, sorted, argument and iteration references.
The next selector failure is the known-null member diagnostic.

The callback compiler now handles a duplicate lambda parameter declaration
in the current scope as a reached textual error. The control checks its
position, execution of the preceding statement and suppression of the
initializer in both engines. Ordinary duplicate local declarations and the
session and loop paths retain their existing admission rules.

The known-null lambda member now uses the existing reached diagnostic
lowering for scalar receivers without members. The selector suite reaches
the subsequent invalid index-type diagnostic. The callback gate passes the
duplicate-parameter timing and D-182 callable-shadow witnesses. It next
reaches the historical fixture that declares built-in names as variables,
which retains the reserved-name admission restriction.

CP14B's call-site record proof and its replay and sanitizer gates remain
open. The focused module and callback results do not close certification.

### D-182 CP14B closure

The comparator proof derives a separate, call-site record shape only for
a fresh literal array with a single comparator use. The allowed uses
exclude aliasing, record writes and escaping record parameters. Every row
must supply the same scalar integer fields. The comparator may read those
fields or call the built-in zero-argument `stringify()`. Its ordinary
effect check and the runtime sort protections still apply. The proof is
cached by receiver and exact comparator within one inference run. B4b's
global site cap and opened-class behavior are unchanged.

On the verified libstdc++ profile, both production dispatch builds pass
all 234 pinned traces. The permanent 16/17 controls require VM execution.
The receiver-alias, record-write and missing-field controls require
pre-execution rejection. The A31 gate passes its unmutated controls and
catches A31 in the inference oracle, production suite, work gate and
guard-off build.

Q7 makes a skipped comparator drain invisible to the original output
witness because the sort never commits its working copy after an error.
The named and design mutation gates keep their exact VM and legacy
channels and also verify actual controller progress in an instrumented
allocation-forbidden driver. This additionally records the instrumented
binary hashes and the exact missing-progress failure of the mutant.
Both ASan/UBSan dispatch builds also passed all 234 traces. The instrumented
allocation driver observed 17 drain steps and zero allocation attempts.
All ten named mutants and eleven active design mutants were caught.
All 99 design-harness negative controls passed.

The selector diagnostic correction covers literal Boolean array keys in
the textual lambda path. An effectful Boolean key retains the precise
pre-execution refusal because legacy can retry it. The callback
reservation fixture retains the original legacy output and independently
pins the VM refusal at its first declaration. The later null-index and
element-path fixtures still expose differences. The full matrix determines
their effect on certification. The 97 filesystem contracts remain at
59 executions and 38 approved refusals. The 102-row original oracle
remains unchanged.
