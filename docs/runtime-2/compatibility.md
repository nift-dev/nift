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
| An array or object program whose output would change once arrays and objects are references (Phase B): a mutated copy (`b := a; b.push(2)`, the former D-040 witnesses), a mutating function given a live array (`f(a)` that pushes, then reading `a`; `x = f(x)` stays admitted), an array stored in a collection or returned by a closure and then mutated, a mutated value read out of a collection or returned by an element method | copies (or location-aliases) as before | `reference-guard:<route>` | D-NEXT-refsem #9 |
| (review round 1, whole-program guard) An array or object mutated while it is lent: an array mutated in its own `for` loop (`for (x : a) { a.push(x) }`, `a[i] = x * 2` inside it: Phase B iterates the live array), a binding mutated by a callee, closure, module function or later session statement while a call holds it (`fn f(x) { a.push(2); print(x) }; f(a)`), a later argument mutating an earlier one, a callback mutating its receiver; a parameter captured by a closure that outlives the call; a function used as a value that mutates its parameter. Also refused although Phase B would print the same (the guard has no index reasoning or call graph): an in-place update of the array a `for` iterates, a write through the loop binding (`for (row : grid) { row.push(0) }`, which writes through today), and a global array mutated by helper functions then passed to a reading function | copies (or location-aliases) as before | `reference-guard:lent` (or `shared`, `argument`) | D-NEXT-refsem #9 (round 1) |
| An object as a map key or set member | run-time error "key must be bool, number, or string" | refused before any effect (`collection-object-key`) | D-NEXT-refsem (round 1) |
| (review round 2) An array or object reached by iterating a map, set or stack and then mutated through the loop binding (`for (x : st) { x.push(9) }`, nested loops, a copy of the collection, a loop over a collection parameter): Phase B would change the stored element | copies | `reference-guard:collection-element` | D-NEXT-refsem (round 2) |
| (review rounds 3-4) `stringify()`, `prettify()` or `highlight()` on a value whose inferred kinds include a struct instance, in a program (or session) that declares a struct, whatever the receiver (`C().stringify()`, `a[0].stringify()`, `m.get("k").prettify()`, `f().highlight()`, a loop element, a module or session expression): the VM cannot render an instance; a compatibility gap to eliminate, not intended behaviour **Removed in B3** (docs/runtime-2/checkpoints/b3-structs.md): the VM renders instances from their type descriptor, legacy-equal, private fields skipped | renders the instance as `C{v:0}` (a struct's own `stringify` method is not called) | `presentation-struct:<method>` (round 3: `presentation-mixed-struct`) | D-NEXT-refsem (rounds 3-4) |
| (review round 3) `stringify()`, `prettify()` or `highlight()` on a value that may be an array, object, map, set or stack, in a program (or session) that declares a struct (`[C()].stringify()`; also containers holding no instance): the VM cannot render an instance inside a container; a Phase B / VM capability candidate; a compatibility gap to eliminate, not intended behaviour **Removed in B3** (docs/runtime-2/checkpoints/b3-structs.md): the VM renders instances from their type descriptor, legacy-equal, private fields skipped | renders `[C{v:0}]` | `presentation-struct-contents:<method>` | D-NEXT-refsem (round 3) |
| (review round 4, known over-refusal) the same rule refuses a container that holds no instance, e.g. the nift-packages/http `capabilities()` shape (`tests/runtime2/diagnostics/changes/ret_multiline_http_shape.f`, valid output preserved in `tests/runtime2/refsem/pending/`): a temporary Phase A limitation Phase B must eliminate **Removed in B3** (docs/runtime-2/checkpoints/b3-structs.md): the VM renders instances from their type descriptor, legacy-equal, private fields skipped | prints `{"buffered_text":true,...}` | `presentation-struct-contents:stringify` | D-NEXT-refsem (round 4, coordinator/owner) |
| (B3, owner ruling 2026-11-05) An explicit `.stringify()`, `.prettify()` or `.highlight()` on a receiver that may be a struct instance (or is of unknown kind: a loop element, a parameter, a lookup, a session value) when some struct of the program, an imported module or any earlier session statement declares a public zero-parameter method of that name: choosing the method or the built-in needs run-time dispatch | renders the instance (legacy ignores user presentation methods) | `presentation-struct-method:<name>` | B3 (b3-structs.md). B4: scripts and modules choose at run time (above); sessions and unknown receivers keep the refusal |
| (integ/refsem-a; owner ruling "args/cmd x reference guard, Option 2, 2026-11-01", the guard frozen) A mutation of the script invocation binding `args` (`args.push(x)`, `args.sort()`) in a script: legacy runs the earlier statements, then fails with "cannot mutate const array: args"; the VM refuses before any effect as `reference-guard:unresolved`. **A temporary diagnostic-classification difference; correcting the misleading diagnostic is a pre-release obligation.** Evidence: `tests/runtime2/refsem/pending/args_push_after_output.*`, `args_sort.*` | const error after earlier output | `reference-guard:unresolved` | D-NEXT-refsem (integration) |
| (same ruling) A loop over `args` in a piped session (`nift -i -e '...; for (a : args) {...}' a`): a temporary Phase A over-refusal (the session rule assumes a later statement may mutate the array); Phase B must admit it. Evidence: `tests/runtime2/refsem/pending/args_session_loop.*` | runs | `reference-guard:shared-part` | D-NEXT-refsem (integration) |
| (same ruling) `b := args; b.push(x)`: legacy aliases `args` and grows it; weekend/r2's VM copied. Refused (`reference-guard:inflow`). Phase B must establish the final reference-and-immutability contract: aliasing must not bypass a genuinely immutable binding, and a successful mutation is not assumed to be the right answer. Evidence: `tests/runtime2/refsem/pending/args_copy_push.*` | aliases (`args` grows) | `reference-guard:inflow` | D-NEXT-refsem (integration) |
| Field access through a value that may be a struct instance or something else (a field initialised with `""` that later holds an instance, read back: `x := a.next; x.v = 7`) | runs (structs are tagged strings) | `struct-value` | D-NEXT-refsem #8 |
