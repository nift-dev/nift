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
| ~~`map()`, `set()` and `stack()` are values, not references~~ (**superseded by D-NEXT-refsem**: collections are references again, legacy-equal within an isolate). Historical row: `b := a`, arguments, returns, closure reads, container insertion/extraction, struct fields and thread transfer give independent (deep) copies; self-insertion stores a snapshot instead of failing with "would create a cyclic reference"; `same()` on a collection is an error; threads accept collections. Migration: return or re-assign a collection that a function fills (`m = fill(m)`), keep shared state in one binding, drop `copy()`/`deepcopy()` used only to separate copies | D-063 | weekend collections |
| `same()` on ordinary values is an error: arrays bound to names (`a := [1]; same(a, a)`: legacy `true`; two arrays: `false`) and (until D-NEXT-refsem) structs and collections, which are now compared by identity, legacy-equal; a plain object name errors in both. Migration: compare with `==` | D-040 #4, D-063; structs and collections superseded by D-NEXT-refsem #2 | weekend collections |
| `copy(x)` is shallow (Nift v4.2's contract): a new outer struct instance or collection whose contents are shared (an array or object stays a value until Phase B); legacy v4.4+ copies deeply. `deepcopy(x)` preserves sharing inside the copied graph (a collection reached twice is copied once); legacy duplicates it per occurrence. The two-argument filesystem `copy(src, dst)` is unchanged. Migration: use `deepcopy()` where an independent nested value is needed | D-NEXT-refsem #3 | refsem Phase A |
| A refused reference cycle is a **catchable** Error (code `runtime.reference_cycle`; legacy's text where legacy refuses: "set/push would create a cyclic reference", "assignment would create a cyclic reference: <field>"); legacy's refusal is fatal. The receiver is unchanged | D-NEXT-refsem #4 | refsem Phase A |
| Threads and parallel `async` accept collections and struct instances, copied as one alias-preserving graph (two aliased arguments alias one copy in the worker); legacy refuses them ("non-transferable resource") | D-NEXT-refsem #5 (D-063 #6) | refsem Phase A |
| `same()` on mutex, atomic, channel, future and thread compares identity (the same resource or task: `true`; independently created: `false`), where legacy errors "same: operands must be identity-bearing values" | D-063 (owner, 2026-11-01) | weekend collections |
| A `for ((k, v) : m)` loop over a map visits the entries captured at loop entry; insertions, removals and value updates made by the body do not change the active snapshot (legacy iterates the live entry vector: shows updated values, shifts after a removal, undefined past the end). A visited entry may already be removed from `m`: removing it again fails with "remove: key not found". Migration: read the map again after the loop; guard removals with `m.contains(k)` | D-063 (owner, 2026-11-01) | weekend collections |

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
| (review round 3) `stringify()`, `prettify()` or `highlight()` on a value that may be a struct instance or another kind (`for (x : [C(), map()]) { print(x.stringify()) }`, a parameter given both, in a function, closure or imported module): dynamic dispatch of presentation methods over mixed struct/collection receivers, a Phase B / VM capability candidate | renders the instance as `C{v:0}` (a struct's own `stringify` method is not called) | `presentation-mixed-struct:<method>` | D-NEXT-refsem (round 3) |
| Field access through a value that may be a struct instance or something else (a field initialised with `""` that later holds an instance, read back: `x := a.next; x.v = 7`) | runs (structs are tagged strings) | `struct-value` | D-NEXT-refsem #8 |
