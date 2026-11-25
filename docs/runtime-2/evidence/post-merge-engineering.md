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

### Certification follow-up for 1db7e2c6

The four-platform VM subset and build-only packaging passed. The first red
Checkpoint 10 assertion on Linux, macOS and Windows was the same known-null
lambda index in `test-v49-selector-parity`. The VM now raises the historical
error at the exact direct call site. Admission requires a single direct
null-literal invocation whose origin is known. A wrapped invocation keeps
its precise refusal. All 69 local selector checks pass.

The aggregate element-path case remains a real semantic blocker. An
identity call using `a[0]` returns a live location in legacy. Replacing
`a[0]` makes that returned location read the new value. Merely admitting
the aggregate and loading its original object would instead read the old
value. The new control pins both the historical result and the VM refusal.
The downstream callback and object-member gates still expose reserved
declarations and the computed-key case. The unchanged recipe and
file-buffer contracts do not authorize any of these forms.

The macOS historical run passed the normal CP21 scenario. The mutant 4
fixture reported restored ownership and then hit PTY EIO. Its last-handoff
check could observe later cleanup. The fixture now observes the kernel stop and terminal owner at the shell's
first subsequent input attempt, before cleanup. Missing stop, missing
input, failed handoff and invalid owner evidence cannot count as a mutant
kill. The 22 audit guards and all five Linux PTY mutants pass. Darwin
interposition and the mutant result require the hosted run.

The Test Integrity log records a runner shutdown and exit 143 during the
parallel aggregate. The language binding steps, including the explicit
aggregate binding target, completed beforehand. Treat this aggregate as
incomplete and rerun it. The log does not establish an aggregate product
failure.

### Hosted follow-up for 0d0fc741

All three Checkpoint 10 jobs passed the corrected selector stage. Linux
then rejected the historical aggregate path argument at `p.f:3:9` in the
v4.8 callback gate. macOS and Windows stopped earlier at the comparator
sort in the same gate, with the exact `method:sort` refusal. The historical
comparator witnesses now use the existing CP14B profile probe. Verified
builds must execute both VM witnesses. Other builds must produce the
exact refusal with no VM execution and match explicit legacy during
fallback. Both paths passed locally, with the refusal profile exercised
using a build that declines comparator sort. Explicit legacy also passed.

The macOS historical job stopped in the execution-shell smoke before the
terminal mutation tests. Its background child reported EPERM when creating
its process group. The parent and child both set that group. The child now
preserves the original failure unless a fresh kernel query confirms it
is already in the requested group. Fault injection on Linux returned
EPERM with the expected group installed and with a mismatched group.
The first child ran and the second retained the original setup error and
exit 126. The historical shell smoke passed locally. The Darwin
failure's exact kernel sequence still needs hosted verification.

The live aggregate element argument remains unsupported. The current
cell forwarding shares a lexical cell, and the pre-B5 selector cell was
removed. Loading or forwarding the aggregate value cannot represent an
array slot that may later be replaced. A correct implementation needs
an owned root and a checked slot re-resolution without retaining a
pointer into the array storage. The existing live-slot oracle remains
mandatory. No refusal was approved for the failing callback fixture.

### Indexed-cell ownership groundwork

The local indexed-cell core retains the original lexical binding cell and
encodes the evaluated index in the cell tag. It uses the existing owned
Value edge, heap destruction and cycle traversal. Reads resolve the
current root and bounds-check its array before copying the current element.
No pointer into array storage is retained. Rebinding an alias detaches
that alias. Cached replacement refuses an indexed descriptor and leaves
the root and descriptor intact.

The allocation and lifetime unit passed 21 checks in native and
ASan/UBSan/LeakSanitizer runs. The complete native destruction harness
passed 100,509 checks. The unit covers invalid construction, injected
allocation failure, slot replacement, shifts, shrinkage, root reassignment,
root retention after external references are released, shared child
mutation, alias detachment and the root edge in cycle detection.

Compiler and call/return lowering remain open. Existing bytecode paths
that read a cell's stored Value directly, including comparator sort
locations and some session and coroutine paths, cannot accept this
descriptor. The original 7 VM comparison witnesses and downstream
location gate still need to pass before integration. No candidate was
frozen for this local work.

### Indexed-location bytecode

The local VM now has a checked `NewIndexedCell` operation. It builds an
indexed descriptor from an owned root and an already evaluated number.
Ordinary internal call and return windows preserve the descriptor in
hand-built bytecode. `CellLoad` resolves the slot and reports its binding
name through the existing diagnostic note. Tests cover surviving a
callee frame, root reassignment, an invalidated slot and rejection when
a descriptor is returned as a user value. The VM suite passed 35 checks
in both native and ASan/UBSan/LeakSanitizer runs. A stale-root build that
retains the old array cell failed the reassignment and invalidation
assertions. The existing allocation lifetime suite passed 21 checks.

The ownership audit distinguished the stored root edge from script reads.
Cycle traversal and heap destruction deliberately follow the stored edge.
Sort rejects the indexed anchor. Top-level rendering and isolate transfer
reject the descriptor, and indexed-cell construction is disabled in
session, coroutine and isolate VM contexts. The compiler still needs to
prove and mark the location-carrying argument and return paths, preserve
binding type for detaching aliases, and prevent exposure through cached
writes, raw-value consumers, or user aggregates before enabling emission.
No script case was admitted by this patch.

Historical macOS run 45508702035 passed the v4.4 shell stage and timed out
in the v4.5 native runtime wall after reporting CP21 mutants 1 through 3.
It supplies no mutant 4 verdict. Test Integrity run 45504368934 reached
the parallel aggregate step and was still running at the recorded check.

### Indexed-location compiler lowering

Standalone scripts now carry a checked indexed cell through a statically
identified one-parameter identity call. A simple named factory can return
that cell out of its frame. A declaration receiving the location gives it
a typed binding of its own so reassignment can detach the alias. Value
reads resolve the current slot through the original binding. The compiler
requires a root with proven array-valued slots and keeps arbitrary location
forwarding, captures and unsupported escape paths under pre-execution
refusal. The indexed key must be a numeric literal or a plain numeric
binding read.

The selector gate runs the seven original inputs independently in required
VM and explicit legacy modes. It compares exit status, output and exact
diagnostics, including the missing-target position. Two further controls
reject bare and call-mediated alias forwarding before the script can print
or mutate the array. Independent review reproduced both stale-value paths
before the admission correction and confirmed their refusal afterwards.

Test Integrity run 45504368934 finished with one failed job. The Linux
production-binding runner received a shutdown signal during its aggregate
step and exited 143. The static scan, deterministic contracts and both
aggregate-evidence jobs passed. The interrupted aggregate requires a
complete rerun on the eventual candidate.

The final local lowering ran seven exact location witnesses, two
propagation refusals, 69 other selector contracts, 55 closure
differentials and the 182-script VM corpus. The indexed-cell and VM
harnesses passed 21 and 35 checks under ASan/UBSan/LeakSanitizer. The
native destruction suite passed 100,509 checks. The stale-root mutant
failed three assertions across the reassignment and invalidation cases.

The v4.8 callback and sort/factory gates passed locally. The callback
overlay and object-member oracles now retain explicit legacy expectations
while independently checking the D-182 lexical result and the B5
aggregate reference result. The callback overlay still stops at the
reserved declaration and the object-member oracle at computed-key
assignment. No refusal approval or release certification is inferred
from these runs.

### Lambda-mediated indexed locations

A local identity lambda and a zero-argument capture lambda both used to
return the selected child snapshot. Each now retains the root cell and
index when it initializes a supported alias. Identity lambdas still run
with a resolved value argument. A captured literal index is checked by the
actual lambda call before the descriptor is formed. The compiler proves
that the lambda binding is only used as a direct callee and checks both
lexical binding identities again at emission. Session, coroutine and
isolate transfers retain their previous restrictions.

Independent review found a captured-index error-site discrepancy. The
compiler now marks that invocation so a checked index fault has the
legacy call-site position. Review also checked the bounded numeric-literal
push path and the non-escaping direct nested print read. The seven new
dual-mode witnesses cover parent-slot replacement, root rebinding,
mutation through the replacement, vanished targets and exact errors.
The seven original indexed witnesses and both propagation controls pass.
The unchanged v4.8 identifier location case now passes in required VM
mode. The suite reaches its separate unresolved-name diagnostic witness.

Checkpoint 10's Windows selector and macOS CP14B comparator failures
included fixture paths in alternate native spellings. The oracle now
uses the existing MSYS fixture-root aliases and CP14B resolves its
working directory before invocation. The exact diagnostic and
pre-execution assertions remain intact. The CP21 PTY fixture now closes
the master before its bounded child reap. A teardown timeout records a
fixture failure and cannot count as mutant evidence.

The Test Integrity check annotation for job 138885898893 reports that
the hosted runner lost communication with the server. The run published
no artifacts and its log blob was unavailable. The last recorded step
was the parallel production aggregate. Treat that failure as incomplete.

### Diagnostic contracts and aggregate visibility

On 20113844 the historical workflow passed Linux, macOS and Windows.
The Linux Test Integrity aggregate received a runner shutdown signal
and exited 143. Four other jobs passed. The aggregate now streams its
output while retaining the same command, log and failure status so a
runner shutdown leaves evidence of progress.

The standalone `print(missing)` witness uses the same pre-execution
undefined-operand contract already pinned by D-158. The v4.8 identifier
suite independently checks the historical runtime diagnostic and the
exact VM refusal reason and position. It also runs the two syntactically
invalid historical inputs separately in both modes. No generic
unresolved-name or opaque refusal is approved by these checks.

CP14B shares the MSYS-aware oracle path normalizer. Its new control
rejects partial path matches while recognizing the translated fixture
root. The v4.8 identifier suite passes locally in both modes. The
surrounding callback suite now stops at an imported coroutine capture.
The original reserved `first` and computed-key witnesses remain strict.

### Closure loop inference

The original v4.8 callback hammer now runs in required VM mode and matches
explicit legacy output (80200, 6, 8). Before the fix, the first inference
walk saw an empty element type while the factory's stored result was
pending. Treating that as an unknown callable widened lambda parameters
permanently. A standalone tracked array with a pending element now keeps
that fact at bottom until its existing worklist dependency grows. Loop
bodies are still analyzed. Open arrays, unknown arrays, and sessions keep
their previous rules. Review checked the site dependencies and widening
paths. A fractional-return closure still refuses the integer accumulator
assignment before execution.

The callback fixture preserves explicit legacy execution of the original
imported-task test, checks the precise D-064 refusal in required VM mode,
and checks synchronous private captures with a separate fixture. No
coroutine or isolate rule changed. The callback shell suite passes. Its
separate numeric grid still finds a pre-existing diagnostic disagreement
for array operands in `a + b * c`, reproduced on the earlier binary.

### Nested numeric diagnostics and method receivers

The script and lambda routes now report the failing child operator first
for nested trees over lexical bindings and numeric literals. Other syntax,
loop contexts and sessions keep their existing refusal ordering. All 470
pairs in the original numeric grid pass in required VM mode. The local
precise-position and evaluation-order gates pass.

The post-certification refusal gate exposed a stale method:push sighting.
The original named-identity receiver now executes and produces 3 in both
engines. The companion missing-target case exposed a direct aggregate
initializer that still read a snapshot. It now uses the same checked
descriptor when the existing root and consumer proofs hold. Both
historical inputs run independently in the location receiver test, which
checks the exact missing-target name and position. The ledger removes
only the resolved sighting and keeps the two other pinned method:push
cases. All 90 remaining scoped refusals and the three mutation controls pass.

Independent review also reproduced bare forwarding of a direct indexed
alias: after slot replacement legacy observes [9], while the VM observes
[1]. The same result occurs on the earlier binary. This remains open.
The reserved declaration and computed-key position still require their
existing historical checks.

### Bare alias forwarding and CP18 aggregate repair

A plain declaration that copies a proven direct indexed alias now
checks the source before carrying its payload into a fresh typed cell.
An attached alias carries its descriptor. A detached alias carries the
current array. Each destination has an independent binding, so later
rebinding either variable leaves the other alone. The five dual-mode
controls cover live replacement, detaching before and after copying,
destination detachment and the exact missing-target error at the source.
The copy opcode retains the existing execution-context guards. Separate
call-mediated forwarding from a direct indexed initializer can still
materialize the value and remains open. The existing named-identity
propagation refusals remain in their original controls.

The earlier Checkpoint 10 reserved-name and computed-key witnesses
still produce their original legacy results and precise VM refusals.
Independent probes show that renaming the reserved binding or placing
the computed key into a plain local before the write allows both engines
to execute with the same results. The original checked fixtures remain
strict.

Test Integrity run 45549554803 published its aggregate output before the
Linux runner shutdown. The CP18 sanitizer build failed to find
minify/Minify.h before the shutdown. The sanitizer harness now consumes
the runtime unit source list and include flags from Make and includes
Minify++ dependencies in its object cache key. Both VM dispatches and
the existing ownership mutation controls are still required. The
interrupted aggregate has no complete verdict.

Once the CP18 harness built, its injected nested-growth campaign found a
persistent allocation-count delta associated with the first real cycle
walk. The reusable thread-local scratch is now primed before the leak
baseline. The walker clears pending work and resets its busy flag on all
exits. The full CP18 gate passed both dispatches and detected both
ownership mutations under both dispatches. The focused indexed-cell
harnesses passed 41 VM and 21 lifetime checks under ASan, UBSan and LSan.

### Call-mediated indexed alias forwarding

A checked direct indexed alias can now pass through a proven identity
callee into a new standalone binding. The compiler checks the source
before the call, then passes the live descriptor if attached or the
ordinary array if detached. It recognizes a statically resolved named
identity function and a directly used local expression identity lambda.
It rechecks the lexical source and callee identities and gives the result
a fresh typed binding. Unproved callees and transfers remain outside this
lowering. The existing propagation refusal witnesses are unchanged.

Five new legacy/required-VM controls include a parent-slot replacement
through each callable form, independent rebind, detached forwarding, and
the exact source-position error when a slot is missing before invocation.
The bytecode forwarding control now passes the copied payload through the
identity call before creating the destination binding.

All three Checkpoint 10 jobs on 747a421b passed the numeric and location
checks and stopped at the original reserved `first` fixture at 1:42.
The CP18 sanitizer evidence on that commit still covers both dispatches
and both ownership controls on each dispatch. The original computed-key
and reserved-name rules are unchanged.

### Clean aggregate runner and downstream contract observations

Test Integrity run 45564824304 ended with an explicit runner shutdown
and exit 143. The stream reached the repaired CP18 sanitizer command
and contained no earlier failed Make recipe. Four sibling jobs passed.
The clean test-all command, its subsequent exclusive measurements and
the build-boundary check now run on a fresh Linux job with the same
language toolchain setup. The production binding job still executes
each language gate and the clean public test-bindings target.

D-182 expressly preserves the first-name reservation and the precise
refusal for unsupported computed writes. The downstream tests keep
both historical inputs and their explicit legacy outputs. They pin the
original VM refusal positions and independently execute a renamed
binding and a plain-key write as positive controls in both modes.
This records the two existing guards and does not add an inventory
deferral or alter supported syntax. The next local failures are the
callback overlay's local-capture-declaration and the object-member
oracle's location-array-growth.

### Callback entry and object-valued array slots

The two original duplicate-declaration callback fixtures are reached
legacy errors. For the proved first-statement shape, the VM now raises
the same error at the declaration when the callback runs. Empty maps do
not enter the body. An unrelated multi-statement block still follows its
existing admission rule. Both originals run independently in explicit
legacy and required VM modes.

An array slot holding an object now retains its checked location through
an admitted identity call. A direct replacement control verifies that
the alias observes the new object. The original array-growth fixture
also passes. Its projected array may be read as a shared value because
the compiler proves append-only growth, no slot replacement and no
rebinding of the object alias. A replacement combined with a retained
projection still refuses before execution. Arbitrary nested or keyed
locations remain outside this proof.

The next callback-overlay failure is `location-invalid`, which refuses
a vanishing captured loop location. The next object-member failure is
`location-parent-object-growth`, which uses a keyed location and a
computed object growth loop. Their historical witnesses remain intact.

### CP17 aggregate failure and B5 convergence

The fresh Linux runner finished with a genuine test-all failure.
The first failed recipe was test-rt-handles: the CP17 scanner found
legacy handle-tag literals in two VM compiler admission checks. Both
checks now call the same prefix predicate as the legacy RuntimeValue
handle detection. Decoding, refusal reasons and the scanner allowlist
are unchanged. The complete local handle gate passed.

Shortened probes isolate the remaining Checkpoint 10 witnesses.
A logically non-shrinking array map with a proved read-only callback
can run across growth, so the original location-reallocate fixture now
matches legacy. A separate clear still refuses that captured-loop
witness before execution. B5 iterates a snapshot and the historical
vanished-loop error comes from a live-loop contract. The keyed parent
fixture still refuses at the identity argument after hoisting the
computed write key. Even isolated key replacement requires a keyed
parent location, which is separate from a numeric array slot.

The B5 run exposed an earlier regression: standalone direct extraction
must preserve the selected value after root rebinding (Q3), while the
identity location-return forms retain their separately pinned semantics.
The direct proof now declines roots that may rebind. The two M-3
precision witnesses remain unchanged. The conservative scan retains the
refusal for a pushed literal lambda that captures the current loop
element. All 140 default-dispatch B5 fixtures passed after these changes.
