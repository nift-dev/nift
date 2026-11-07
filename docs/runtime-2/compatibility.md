# Compatibility contract

Must not change unless a decision in `decisions.md` approves it.

| Surface | Rule |
| --- | --- |
| C ABI 1.3 (`include/nift/c_abi.h`) | Symbols and signatures unchanged; additions bump the minor version |
| `nift::Value` / `nift::Engine` C++ API | Source compatible |
| CLI commands and flags | Unchanged; new flags allowed |
| `.nift/` project files and page-info | Readable by new code; new fields optional and versioned; old metadata triggers at most one rebuild |
| Template syntax and output bytes | Byte-identical for unchanged inputs |
| Script syntax and observable results | As `semantics.md`; changes only where a row says **Change** |
| Diagnostics | Message text, codes and spans identical unless a checkpoint records an improvement |
| `nift eval --json` / `--capabilities` | Contract version 1 kept |
| Bindings (Python, Node, Go, C#) | Unchanged; they sit on the C ABI |

## Approved breaks

| Break | Decision | Checkpoint |
| --- | --- | --- |
| Project-root confinement removed from general filesystem operations | D-002 | CP03 |
| String-encoded handles stop working | Design report; D-001 | CP17 |
| Reconstructed handle tags no longer resolve after release | Design report; D-001 | CP17 |
| Recursion limit raised from 64 | Design report; D-001 | CP11–CP12 |
| Candidates for deliberate language changes (break, argument evaluation, value-based typing, declarations in loops, aliasing, catchable errors) | D-015 table (proposed, not yet scheduled) | — |
| User functions shadow built-ins of the same name | D-009 #3, D-014 | to be scheduled |
| `exit` in `~/.niftrc` / `nift -i file` on a terminal is refused (the file stops there, the session continues at the prompt) instead of ending the session with status 1; migration: drop the line | D-061 | VM default |
| A write to the script invocation bindings (`args = …`, `cmd = …`, `args[i] = …`, compound/increment) is rejected before execution (`invocation-assign:<name>`, nothing of the program runs) instead of failing fatally at the statement after earlier statements ran; migration: declare a new binding (`my_args := args`) or replace it with a top-level `args := …` before any use | D-062 #2 | weekend args/cmd |
| An import-resolution error (package not installed or invalid, lock or revision mismatch, lock acquisition failure, missing or unreadable module, import cycle, package-relative escape) rejects the whole program before it runs, with legacy's text and position and exit 1; legacy runs the statements before the import first. An import a top-level `return` leaves unreached is rejected too (legacy exit 0); the diagnostic says so | D-NEXT-packages #1 | weekend packages |
| A VM package lookup creates `.nift/.packages-transaction.lock` only inside an existing `.nift/` (never truncating it) and never creates `.nift/` itself; legacy creates both. One read lock covers all package imports of the program | D-NEXT-packages #2 | weekend packages |
| A module's `return <value>` outside functions (reported at the `return`; legacy reports at the import), `export(x)` naming no binding, a duplicate export and `export(<not a name>)` reject the program before it runs (legacy fails when the statement executes); a `return <value>` in a branch legacy never takes is rejected too | D-NEXT-packages #5 | weekend packages |
| A module's top-level code (or a function it may call while initialising) reading the entry script's `args`/`cmd` is refused before running (`import-scope:invocation-init`; legacy fails when it executes, and runs it when the code is never executed) | D-NEXT-packages #4 | weekend packages |
| Of several errors in one program, the VM reports the first found by a fixed compiler-stage order: the module graph (depth first, statement order, the first refusal or program error stops it), then the linker (bare module return, name clash, scope, the `args`/`cmd` planner), then the compiler. Legacy reports the one it executes first; the two may differ. Every reported error is genuine, classified, positioned and raised before any effect (owner diagnostic policy, 2026-11-01) | D-NEXT-packages | weekend packages |
| ~~`map()`, `set()` and `stack()` are values, not references~~ (**superseded by D-NEXT-refsem**: collections are references again, legacy-equal within an isolate). Historical row: `b := a`, arguments, returns, closure reads, container insertion/extraction, struct fields and thread transfer give independent (deep) copies; self-insertion stores a snapshot instead of failing with "would create a cyclic reference"; `same()` on a collection is an error; threads accept collections. Migration: return or re-assign a collection that a function fills (`m = fill(m)`), keep shared state in one binding, drop `copy()`/`deepcopy()` used only to separate copies | D-063 | weekend collections |
| `same()` on ordinary values is an error: arrays bound to names (`a := [1]; same(a, a)`: legacy `true`; two arrays: `false`) and (until D-NEXT-refsem) structs and collections, which are now compared by identity, legacy-equal; a plain object name errors in both. Migration: compare with `==` | D-040 #4, D-063; structs and collections superseded by D-NEXT-refsem #2 | weekend collections |
| `copy(x)` is shallow (Nift v4.2's contract): a new outer struct instance or collection whose contents are shared (an array or object stays a value until Phase B); legacy v4.4+ copies deeply. `deepcopy(x)` preserves sharing inside the copied graph (a collection reached twice is copied once); legacy duplicates it per occurrence. The two-argument filesystem `copy(src, dst)` is unchanged. Migration: use `deepcopy()` where an independent nested value is needed | D-NEXT-refsem #3 | refsem Phase A |
| A refused reference cycle is a **catchable** Error (code `runtime.reference_cycle`; legacy's text where legacy refuses: "set/push would create a cyclic reference", "assignment would create a cyclic reference: <field>"); legacy's refusal is fatal. The receiver is unchanged | D-NEXT-refsem #4 | refsem Phase A |
| Threads and parallel `async` accept collections and struct instances, copied as one alias-preserving graph (two aliased arguments alias one copy in the worker); legacy refuses them ("non-transferable resource") | D-NEXT-refsem #5 (D-063 #6) | refsem Phase A |
| `same()` on mutex, atomic, channel, future and thread compares identity (the same resource or task: `true`; independently created: `false`), where legacy errors "same: operands must be identity-bearing values" | D-063 (owner, 2026-11-01) | weekend collections |
| A `for ((k, v) : m)` loop over a map visits the entries captured at loop entry; insertions, removals and value updates made by the body do not change the active snapshot (legacy iterates the live entry vector: shows updated values, shifts after a removal, undefined past the end). A visited entry may already be removed from `m`: removing it again fails with "remove: key not found". Migration: read the map again after the loop; guard removals with `m.contains(k)` | D-063 (owner, 2026-11-01) | weekend collections |
| An explicit `c.stringify()` / `c.prettify()` / `c.highlight()` on a struct instance runs the struct's own method of that name when it declares one (public, no parameters); legacy ignores such methods and renders the instance. Implicit presentation (an instance inside a container or another instance being rendered, transfers, sessions) never runs user code. A private one is reachable only through `this`: from outside the built-in renders (legacy parity) | owner ruling 2026-11-05 (B3); the private-method rule is the B3 review L3 fix (a coordinator ruling applying legacy parity), not the owner's ruling | B3 (docs/runtime-2/checkpoints/b3-structs.md) |

### Reference cycles are refused (intentional limitation, D-NEXT-refsem #4)

Maps, sets, stacks and struct instances are references, and there is no cycle
collector. A store that would make one of them reachable from itself is refused
before anything changes (a catchable `runtime.reference_cycle` Error). Legacy
refuses a direct cycle through struct fields and collection values too, but it
**accepts** cycles through an array, an object or a closure, and so does
JavaScript; the VM refuses them:

- `m.set(1, [m])`, `m.set(1, {"k": m})`, `st.push([st])`;
- an object holding a closure that captures the object itself, inside a
  function: `m := map(); f := () => m.size(); m.set("f", f)` (and the same for
  an instance field). A closure over a *top-level* binding reads it by name, so
  storing it closes no cycle and is accepted.
- the cycle closed the other way, by storing into the captured binding
  itself: `m.set("f", () => a.length())` then `a = [m]` or `a.push(m)` inside
  a function. This refusal is catchable too, and the binding keeps its value
  (review round 1: it was fatal).
- a cycle closed by `insert`, `splice`, an expression `push` or an element
  store into a captured array: refused catchably before the array leaves its
  cell, which keeps its value (review round 2: the cell was left empty).
- a cycle closed by a splice that also removes elements (`a.splice(1, 2, [5,
  6, st])` where only the last replacement closes it): refused catchably
  before anything is removed or inserted; the array and its reachable graph
  are unchanged (review round 3: the removal took effect).

Migration: keep the back reference outside the object (an id into an owning
collection, or a top-level binding the closure reads).

## Arrays and objects are shared references (approved breaks, D-NEXT-refsem-b, B5)

Owner decisions 2026-11-01/02 (plan section 8 Q3 approved as one list; Q1 option (a); Q2 option B;
Q7 copy-sort-commit, 2026-11-07). Arrays and objects are shared mutable references in the VM, like
maps, sets, stacks and struct instances; the Phase A guard that refused these programs is deleted
(B5). `NIFT_VM=0` keeps legacy's behaviour. Each changed expectation is listed in
`docs/runtime-2/evidence/b5/expected-changes.tsv`.

| Program | Legacy | Runtime 2.0 |
| --- | --- | --- |
| `fn(f(x)){x.push(4)}; f(a); print(a)` | `[1,2,3]` (arguments copied) | `[1,2,3,4]` |
| `fn(r()){return a}; x := r(); x.push(4); print(a)` | unchanged | grows |
| `m.set("k", a); a.push(2); m.get("k")` | `[1]` | `[1,2]` |
| `p := o; p["k"] = 2; print(o)` | `{"k":1}` | `{"k":2}` |
| `for (v : a) { a[0] = 9; print(v) }` | `9` (live element) | `1` (entry snapshot, D5) |
| `for (v : b) { b.pop(); print(v) }` | error "reference target no longer exists" | every entry element |
| a closure capturing a loop variable (`for (x : a) { f = () => x; x = 7 }`) | reads the source element when called (`1`, or the vanished-element error) | the loop binding is an ordinary, per-iteration binding (`7`): closures of one iteration share it and see later assignments to it, closures of different iterations are independent. An intentional capture change approved by the owner 2026-11-07 06:55 (B5 D1), distinct from snapshot iteration (fixtures `tests/runtime2/b5/loopclosure`) |
| `d := copy([[1]]); d[0].push(2)` | original unchanged (deep) | original shows `[[1,2]]` (shallow `copy`; `deepcopy` is recursive) |
| `a.push(a)` | pushes a copy | catchable `runtime.reference_cycle` |
| `b := args; b.push("q")` | `args` grows | "cannot mutate const array: b" (Q1: `args` immutable through every alias) |
| `const a := [1]; a.push(2)` | "cannot mutate const array: a" | succeeds (Q2 B: `const` is binding-only) |
| `same(x, y)` on arrays/objects | `true` for an aliased name | identity |
| comparator `sort` | in place; aliases see partial order; a comparator error leaves a permutation | sorts a copy and commits it once: aliases see the original order, a failure leaves the receiver unchanged, identity kept (Q7) |
| `ys := xs.reverse(); ys.push(3); print(xs.length())`; `c := a.sort(); c.push(0)`; `same(xs, xs.reverse())` | reverses/sorts `xs` in place and **returns a copy**: `2`, `a` does not grow, `false` | returns **the receiver itself**: `3`, `a` grows, `true` (`reverse()`, `sort()`, comparator `sort`). **APPROVED by the owner 2026-11-07 14:00 as an intentional B5 difference from legacy** (basis: plan section 3 "Returns" (share) and the B4b Option A ruling 2026-11-06 22:56; the 20 trim-audit category-b corrections rest on it). The returned value is the receiver and every alias of it, aliases observe the successful mutation, a mutation through the result reaches the receiver: no fresh identity. Comparator sort may use temporary storage internally (copy-sort-commit, Q7) while returning the original receiver: returning the receiver is not the same as sorting in place (during the sort the receiver keeps its original order; a comparator failure commits nothing and leaves the receiver and its aliases unchanged). Tests: `tests/runtime2/b5/receiver/` (reverse, plain sort; production), `tests/runtime2/b5/sort/comparator_receiver.f` with `aliased_reads.f`, `failure_injection.f`, `nested_identity.f` (comparator sort: the NIFT_RT_REFERENCE_PROBE build only on a GCC 13.3 host, where production refuses comparator sort, D2) |
| extracted array elements stay valid after the source binding is reassigned: `a := [1]; x := a[0]; a = []; print(x)`; the same with an aggregate element: `b := [[1]]; y := b[0]; b = []; print(y.stringify())`, `c := [[1]]; w := c[0]; c = [[5]]; print(w[0])` | the scalar example prints `1` too (legacy copies a scalar element); for an aggregate element legacy binds a **location** in the source: after `b = []` it fails `reference target no longer exists: y` (likewise `x[0]`, `z.k`), exit 1, and after `c = [[5]]` it reads through the new `c`: `5` | `x := a[0]` binds **the element itself** (Extraction): `1`, `[1]`, and `1` after `c` is rebound. **APPROVED by the owner 2026-11-07 15:25** (re-review R2-3; basis: plan section 3 "Extraction", approved with the Q3 list 2026-11-01/02). Test: `tests/runtime2/b5/q3/r14_extracted_element_survives_rebind.f`; 17f7964c refused these programs (`aliasing`) |
| `first()` returns the element itself: `a := [[1, 2], [3]]; head := a.first(); head.push(9); print(a.stringify()); e := a[0]; print(same(head, e))` | returns a copy: `[[1,2],[3]]`, `false` | returns the element: `[[1,2,9],[3]]`, `true`. **APPROVED by the owner 2026-11-07 15:25** (re-review R2-3 and item 6; same Extraction rule). Tests: `tests/runtime2/b5/q3/r15_first_returns_element.f`; `tests/runtime2/vm_builtins.py` `shapes@loop` / `shapes@fn-loop` (pinned: legacy's output with `[[1,2],[3]]` -> `[[1,2,9],[3]]`, b5-refsem.md 13.4) |
| (B5 item B) a failing-kind `trim()` / `to_upper()` / `to_string()` and the related string built-ins on a map, set, stack or struct instance, whatever happens to the result: `for (v : [" a ", map()]) { x := v.trim(); print("got") }; print("end")` | no error at the call (legacy works on the collection's internal tag string): an unused result passes silently, so the loop prints `got got end`, exit 0; a printed result either prints the tag text (`to_upper`, `substr`: exit 0) or fails later in `print` ("value is not directly renderable") | an ordinary run-time error at the call, `unknown value or malformed expression: v.trim()`: `got`, then the error, exit 1. Approved rule: owner 2026-11-07 04:47 item B (a receiver kind that does not support the method gives the existing ordinary program error, never an internal tag); vm_adversarial groups G2/G3 and gen1 show the pattern. Legacy's behaviour here is not an error message but a silently rendered tag, so "legacy's family text" applies only where legacy itself fails |
| (correction of a B4b production defect) `k0 := stack(); aa := [{"a": [k0]}]; for (v : aa) { v.push(1) }` (`push` on an object element whose nested contents hold a collection) | `unknown value or malformed expression: v.push(1)`, exit 1 | the same error (legacy-equal). **B4b production (17f7964c) printed nothing and exited 0: a wrong result** in the accepted B4b baseline; B5 corrects it. Witnesses: review `p/sf_{stack,aint,chan}.f`, minimal `objpush_min2.f` (B5 fix-up). (`channel()` without a capacity is an undefined callable in legacy, so the `chan` variant fails earlier there; that difference predates B5) |

## Splice evaluates its arguments first (approved difference, review round 4)

`a.splice(start, count, replacement)` evaluates the receiver and every
argument, then validates (bounds, the replacement, cycles), prepares the
result without touching the array, and commits once. When an argument throws
or validation fails, the array is unchanged, whether it is a plain local,
captured by a closure, or a global: `try { a.splice(0, 2, [boom()]) }`
leaves `[1,2,3]`. Side effects performed while an argument is evaluated
(`boom()` pushing to a log) are not rolled back; atomicity covers the splice
mutation only. Legacy removes the elements before it evaluates the
replacement (`[3]`). Coordinator ruling under the owner's
correctness-over-legacy policy, owner-approved, 2026-11-01 (D-NEXT-refsem
round 4); the owner may reverse it.

## Documented compatibility differences (approved by the owner, not release blockers)

| Difference | Legacy | VM | Status |
| --- | --- | --- | --- |
| A module's exported function constructing that module's own struct, called from the importer (`m.f`: `struct(C){v:=7}`, `fn(make()){return C()}`, `export(make)`; `main.f`: `a := make(); print(a.v)`) | fails: `return: undefined callable: C` (exit 1); construction at the module's top level works | constructs the instance (`7`) | **predates B3** (the pre-B3 VM already constructs it); B3 lets such instances render too (`refsem/modules/presentation_struct_contents`, `presentation_struct_expression`, `b3/modules/proven_method`); **approved by the owner, 2026-11-05**: correct VM behaviour, the legacy defect is not reproduced (review M1 of B3) |
| (B4, owner rulings 2026-11-06; b4-dispatch.md 6) Interpolating a struct instance or a map/set/stack (`"v=$[c]"`) | prints its internal tag (`\x1fnift:struct:N`, `\x1fnift:collection:N`) | an ordinary run-time error (`struct instances cannot be rendered as text`, `map, set and stack values cannot be rendered as text`) | approved: no internal tag is reproduced |
| (B4) A function shown by a session or `nift eval` without `--json` (`f`, `[x => x]`) | `nift eval` shows its internal tag (a session errors) | `opaque runtime value is not serializable` in both | approved: no internal tag. `nift eval --json` is unchanged (B3's tag string; review L1) |
| (B4) An atomic shown by a session or `nift eval` without `--json` | its internal tag | its current value (one sequentially consistent load) | approved; interpolation already showed the value in legacy (now equal). `nift eval --json` is unchanged (not serializable; review L1) |
| (B4) A map/set/stack value inside a session line (`print(1); set()`) | `null` followed by the collection's tag | `nullset([])` | approved: no internal tag (a bare collection statement shows `set([])` in both) |
| (B4 C1) An import whose export collides with a binding bound before it (`mf := 1; import("m.f")`) | runs the earlier statements and the module, then `import: export collides with existing binding: mf` | the same error line, rejected before running (no output) | approved by the coordinator (2026-11-06) under the invocation-export precedent. Only where legacy certainly reaches the check (no earlier statement, its modules' included, can return, exit or fail; review M2); otherwise refused `import-collision` |
| (B4, review M1) A malformed method call on a call receiver with arguments in prepared code (`for (i : [0, 1]) { print(mk(i).starts_with("a")) }`, `mk(1)` null) | `unknown value or malformed expression: mk(i).starts_with(__nift_native_arg_5)` (its prepared evaluator's hoisted argument names) | the same message with the source text, `mk(i).starts_with("a")`; same output, line, column and exit status | approved diagnostic difference (coordinator, 2026-11-06): legacy's numbering cannot be reproduced; golden `tests/runtime2/b4/dispatch/malformed_call_receiver_loop.vm` |
| (CP22, owner round 7; recorded by B4b review L2) A lambda created in a task (an async function's returned lambda) and called outside it (`fn[async](a()) { return (x) => ... }`, `fa := await a()`, `fa(" a ")`) | `invalid lambda` at the call, or another lambda with the same per-worker id | the lambda itself runs (each lambda keeps its identity anywhere; lambdas with captures stay refused) | approved: the CP22 correction `correction:lambda-registry` (cp22-coroutines.md 874, 1238; `tests/runtime2/cp22/sem_lambda_*`) |
| (B4) An explicit presentation call on a mixed receiver (a loop element, a parameter) where some struct declares that method | the built-in form | the struct's own method for its instances (chosen at run time), the built-in otherwise | the owner's explicit-call ruling (2026-11-05) extended by B4's dispatch; was refused `presentation-struct-method` |

## Syntax compatibility (D-045, owner, 2026-10-20)

Runtime 2.0 must accept exactly Nift's established syntax, as implemented by
legacy Nift and documented at https://nift.dev, and must refuse (before
execution) anything it does not support rather than reinterpret it. Examples:
template functions `@fn(name(args)) { … }`, standalone `.f` functions
`fn(name(args)) { … }`, single-brace bodies in Nift statement syntax, optional
semicolons. Every checkpoint that adds or extends syntax checks the
documentation, legacy and the Runtime 2.0 parser, keeps documentation-derived
executable fixtures run against both engines, and records any
documentation/legacy disagreement for an owner decision before changing the
language. Syntax compatibility is part of the certification gates of every such
checkpoint. Owner-approved language changes (D-036–D-044) are listed as such and
never confused with compatibility regressions.

## Compatibility limitations (refused before any effect, not changed semantics)

The VM refuses these legacy-valid shapes before running anything (exit 1,
"no part of it ran"); `NIFT_VM=0` keeps legacy's behaviour. They are not
approved breaks and not completed behaviour.

| Shape | Legacy | Reason | Decision |
| --- | --- | --- | --- |
| `same()` with an argument that is not a plain binding name: a string literal (`same("f", "f")`), a call, an index, any expression; in every program | reads a string argument's text as a binding name first (`f := () => 1; same("f", "f")` is `true`; `same("a", "a")` without `a` is "unknown value or malformed expression: a"), and evaluates other argument shapes up to twice | `same:argument-shape` | D-063 (owner, 2026-11-01) |
| A reference cycle through an array, an object or a closure (`m.set(1, [m])`, an object holding a closure that captures it): see "Reference cycles are refused" above | accepted (its tags are never freed) | catchable `runtime.reference_cycle` at run time, before the store | D-NEXT-refsem #4 |
| A map, set or stack as a map key or set member, lookups included (`m.set(k, 1)` with `k := map()`, `s.add(s)`, `s.contains(t)`) | compares the tagged collections structurally (a key mutated later is looked up by its new contents) | `collection-mutable-key` | D-NEXT-refsem #6 |
| An object as a map key or set member | run-time error "key must be bool, number, or string" | refused before any effect (`collection-object-key`) | D-NEXT-refsem (round 1) |
| (review rounds 3-4) `stringify()`, `prettify()` or `highlight()` on a value whose inferred kinds include a struct instance, in a program (or session) that declares a struct, whatever the receiver (`C().stringify()`, `a[0].stringify()`, `m.get("k").prettify()`, `f().highlight()`, a loop element, a module or session expression): the VM cannot render an instance; a compatibility gap to eliminate, not intended behaviour **Removed in B3** (docs/runtime-2/checkpoints/b3-structs.md): the VM renders instances from their type descriptor, legacy-equal, private fields skipped | renders the instance as `C{v:0}` (a struct's own `stringify` method is not called) | `presentation-struct:<method>` (round 3: `presentation-mixed-struct`) | D-NEXT-refsem (rounds 3-4) |
| (review round 3) `stringify()`, `prettify()` or `highlight()` on a value that may be an array, object, map, set or stack, in a program (or session) that declares a struct (`[C()].stringify()`; also containers holding no instance): the VM cannot render an instance inside a container; a Phase B / VM capability candidate; a compatibility gap to eliminate, not intended behaviour **Removed in B3** (docs/runtime-2/checkpoints/b3-structs.md): the VM renders instances from their type descriptor, legacy-equal, private fields skipped | renders `[C{v:0}]` | `presentation-struct-contents:<method>` | D-NEXT-refsem (round 3) |
| (review round 4, known over-refusal) the same rule refuses a container that holds no instance, e.g. the nift-packages/http `capabilities()` shape (`tests/runtime2/diagnostics/changes/ret_multiline_http_shape.f`, valid output preserved in `tests/runtime2/refsem/pending/`): a temporary Phase A limitation Phase B must eliminate **Removed in B3** (docs/runtime-2/checkpoints/b3-structs.md): the VM renders instances from their type descriptor, legacy-equal, private fields skipped | prints `{"buffered_text":true,...}` | `presentation-struct-contents:stringify` | D-NEXT-refsem (round 4, coordinator/owner) |
| (B3, owner ruling 2026-11-05) An explicit `.stringify()`, `.prettify()` or `.highlight()` on a receiver that may be a struct instance (or is of unknown kind: a loop element, a parameter, a lookup, a session value) when some struct of the program, an imported module or any earlier session statement declares a public zero-parameter method of that name: choosing the method or the built-in needs run-time dispatch | renders the instance (legacy ignores user presentation methods) | `presentation-struct-method:<name>` | B3 (b3-structs.md). B4: scripts and modules choose at run time (above); sessions and unknown receivers keep the refusal |
| (**B5 admission regressions, D4; release blockers, not waived**; owner 2026-11-07 09:06 and 11:07) Four interactive-session programs that B4b production (17f7964c) ran legacy-equal are refused on B5, before any effect of the refused statement (earlier statements' output stays; the session stops; exit 1). Exactly: cp19 proof-cli `shared-callable-replacement` (also the minimal `fn(ap(v)){return v()};fn(mk()){a:=[()=>1];return {"f":()=>{q:=a[0];return ap(q)}}};o:=mk()` then `print(o.f())`), `field` (`a:=[1];fn(g(p)){return p.cb(0)+a[0]};warm:=g({"cb":v=>v+1})` then **a plain `a.push("bad")`** is refused), `element` (`a:=[1];fn(g(p)){q:=p[0];return q(0)+a[0]};warm:=g([v=>v+1])` then **a plain `a.push("bad")`** is refused) and `sibling-effects`. A plain later `a.push(...)` is refused when earlier code reads `a[0]`. The causes are not uniform: `shared-callable-replacement` and `sibling-effects`: a fact-proved historical call opens every exposed class and `infer_session.cpp:137` exposes both session-heap classes (a build without that exposure admits both, legacy-equal); `element`: that cause plus a second refusal (without the exposure statement 2 runs and statement 3 is still refused); `field`: a callable flowing through an object field (`session:callable-flow`), still refused without the exposure | the statements run | `session:current-callable-proof` (shared-callable-replacement, element), `session:callable-flow` (field, sibling-effects) | accepted for the B5 checkpoint only; release blockers (b5-refsem.md 11.1); the release audit evaluates the affected session operations as a group. Pinned in `tests/runtime2/cp19/session_proof_cli.py` EXPECTED_REFUSALS (fails when admitted again). The earlier list's `loop` case is not one of them: B4b production never admitted it (its seed statement was refused `reference-guard:lent`) |
| Field access through a value that may be a struct instance or something else (a field initialised with `""` that later holds an instance, read back: `x := a.next; x.v = 7`) | runs (structs are tagged strings) | `struct-value` | D-NEXT-refsem #8 |
| (B4b, **admission difference against B4a `0df0df6c` and `a42035c1`**, not unchanged behaviour) A string built-in without a runtime-kind check (`trim` and the related string built-ins) on an element of an array or object whose element union has more than 16 allocation sites: e.g. `glob` (`acc := [[" s "]]`, N helpers `fn(kI()) { acc.push([..., " s "]) ... }`, then `acc[1][1].trim()`), from 16 helpers on. Past 16 origins the contents read as anything (F1, b4b-element-types.md 11), and `trim` on anything is not admitted (B4a's admission, unchanged). B4a and `a42035c1` admitted it with a Static `trim` (correct output there: the elements are strings; on B4a by the stale per-binding type). Refused before any effect, exit 1 ("no part of it ran"), with and without the guard; the same on `NIFT_VM=require` and the default. Evidence: `tests/runtime2/b4b/admission/` (`glob15.f` admitted; `glob16.f`, `glob17.f`, `glob100.f` refused; each with the expected-future marker `recover:vm`) | runs | `builtin-receiver:trim` | provisionally accepted (owner, 2026-11-06 15:16). B5 implemented item B (runtime-kind checks through B4a's table, owner 2026-11-07 04:47), but it does not admit these: past 16 origins the contents read as anything, and the table still refuses an unknown receiver kind. Checked by the B5 fix-up: `glob16.f`/`glob100.f` are refused `builtin-receiver:trim` before any effect on B5 exactly as on 17f7964c; `glob15.f` runs legacy-equal on both. Still open as a B4b admission difference (release audit) |
