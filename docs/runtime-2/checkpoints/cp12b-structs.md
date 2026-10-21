# CP12 phase B — structs: legacy semantics (probed), draft

Status: probes and draft only (no implementation). Probes:
`tests/runtime2/semantics-structs/<probe>/p/main.f` with expected
stdout/stderr/exit recorded from legacy (D-015), run by
`tests/runtime2/semantics_modules.py --nift ./nift --dir semantics-structs`
(26 probes, legacy: PASS). `semantics.md` already records reference
semantics and construction; this extends it.

## Legacy semantics (verified)

| Area | Legacy behaviour | Probe(s) |
| --- | --- | --- |
| Declaration | `struct (Name) { field := init … fn(method(args)) { … } }`; also `@struct(Name)`; field initialisers are expressions | `basic` |
| Construction | `Name()` evaluates every field initialiser **at construction time**, with the bindings visible then (`n` changed between constructions changes the field); `Name(args)` requires a constructor `fn(Name(params))`, else `struct constructor argument count mismatch: Name` | `constructor`, `field_init_time` |
| Fields | read `p.x`; assign `p.x = v` keeps the field's binding type (`cannot change struct field type: x`); no new fields (`struct has no field: q`); unknown field read: `struct has no field: zz`; member assignment through an index path is rejected (`member assignment requires a struct instance: xs[0]`) | `basic`, `field_type`, `new_field`, `no_field`, `element_member_assign` |
| Reference semantics | instances are references: binding, passing and storing share the instance (`b.x = 9` is seen through `a`; a callee's `s.x = 3` too); nested struct fields are per-instance | `reference`, `nested` |
| Methods and `this` | `this` is the receiver, a const binding inside methods (`cannot assign to const binding: this`), unknown outside them; methods call each other through `this`; arity checked (`struct method argument count mismatch`) | `self_call`, `this_assign`, `this_outside`, `method_arity` |
| Free names in methods | dynamic, like other functions: a method sees a caller's local (`method_free_caller` prints the caller's `zz`) as well as globals | `method_free_global`, `method_free_caller` |
| Private members | `private` fields and methods are usable through `this` inside methods only; from outside: `private struct field: s`, `private struct method: hid` | `private_field`, `private_method` |
| Equality | **identity**: two instances with equal fields are unequal; an instance equals its aliases; never equal to an object | `equality` |
| Printing | `print(p)` fails (`print: value is not directly renderable`); `+` with a string fails (`string concatenation requires renderable scalar values`); `p.stringify()` renders `P{x:1,y:[1,2]}` | `print`, `concat`, `stringify` |
| Type queries | `type(p)` is `struct`, `is_struct(p)` true | `type` |
| Call results | a method on a call result (`P().a()`) is `unknown value or malformed expression` (CP10 Opaque) | `method_on_call` |
| Quirks | a second `struct (P)` with the same name makes `P()` produce a non-object (`cannot access member 'y' because the current JSON value is not an object`); a struct named like a function wins over the function | `redeclared`, `shadows_fn` |

## Draft design (for B4, after the owner reviews)

* Structs are a unit-level declaration: the resolver builds a struct table
  (fields with initialiser nodes, methods as protos with `this` as an extra
  leading parameter, constructor), refusing redeclaration and name clashes
  with functions (`struct-redeclared`, `struct-shadows-fn`: legacy quirks,
  recorded in the consistency register).
* Values: a heap object with a fixed field layout (field index resolved at
  compile time where the receiver's struct type is known from inference;
  otherwise a by-name lookup with legacy's messages), shared by reference;
  binding types per field enforced as for bindings.
* Equality by identity; printing and concatenation errors as legacy;
  `stringify` with legacy's `Name{…}` rendering (CP14 stringify path).
* Free names in methods follow the L25 decision: resolved lexically
  (globals, module bindings); caller-dependent lookups are refused
  (`free-name:dynamic-caller`), like functions.
* Private access checked statically where the receiver is `this`, otherwise
  at run time with legacy's messages.
* Construction evaluates field initialisers at construction time in the
  declaring scope (the probe shows they see the bindings current at the
  `Name()` call — with L25, top-level bindings are the lexical scope).

## D-045 review: nift.dev `docs/structs.html` vs legacy and the draft

The page's examples use the template form `@struct(name) { … }`; script land
drops the `@` (`docs/scripting.html`), so `struct (Name) { … }` is the
documented script form. The page's counter example run in legacy prints
`5`, then `5` / `6` after `copy(a)` — as documented.

| # | nift.dev | Legacy | Decision / draft | Status |
| --- | --- | --- | --- | --- |
| S1 | instances have **reference semantics**; binding or passing copies the reference; `copy()` makes an independent instance; examples rely on it ("shared traversal state", recursive fragment state) | references (probe `reference`) — agrees with the page | **D-040: value semantics, including structs** — contradicts the page and its examples | owner decision: D-040 vs the documented reference semantics (the examples would need `copy`-free rewrites or a handle type) |
| S2 | fields and methods are **implicitly visible inside methods**; parameters and locals shadow them; `this.x` names the receiver | agrees (`this.value = value` works; `step()` calls a private method implicitly) | the draft mentioned only `this`; implicit members added to the draft's scope rules | draft corrected below |
| S3 | "struct definitions and method execution follow normal lexical scoping … through their definition/call context" (ambiguous) | dynamic: a method sees a caller's local (`method_free_caller`) | D-038: lexical; caller-dependent lookups refused | record: the page's wording should name one rule |
| S4 | at most one constructor (method named like the struct) | agrees | draft agrees | — |

Draft correction (S2): inside a method, a free name resolves to a parameter
or local first, then to the receiver's fields and methods (private ones
included), then lexically (D-038); `this` names the receiver explicitly.

## Implementation (2026-10-21; D-040 V9, D-047 #2)

Compiled (`src/rt/structs.{h,cpp}`, `compiler_structs.{h,cpp}`,
`vm_struct.cpp`, kind inference in `infer_walk.cpp`). Instances are values
on the VM (copy-on-write `StructObject`); legacy's are references. Both
agree on every admitted unit because an instance never has two holders:

* it is created only by a declaration `p := Name(args)` (fields from their
  constant initialisers, then the constructor on the instance) and lives in
  that binding (a local, or a global used by functions);
* it is used only as the receiver of its fields (`p.x`, `p.x = v`, compound
  assignment, `++`/`--`) and its methods (`p.m(args)`); every other use of the
  binding is refused (`struct-value`: copies, arguments, returns, comparison,
  printing, `type()`), as is a construction anywhere else
  (`struct-construction`) and an assignment to the binding (`struct-assign`);
* a method receives its instance's absolute stack slot as a hidden first
  argument and reads and writes the instance in place, so `this.x`, implicit
  fields, `this.m()` and implicit method calls reach the caller's binding;
* field writes keep the field's binding type, with legacy's three wordings
  (`cannot change struct field type: x` through a member; `cannot assign A to
  B struct field 'x'` for an implicit field; `... binding 'x'` for its
  compound assignment);
* private members only through `this`; methods resolve no globals (they run
  wherever an instance is, not ordered by G3).

Legacy's loop evaluator fails on struct field access in loop bodies and in
functions called from loops ("value has no member", L42): refused there
(`context:struct-field`); method calls from loops are admitted (identical).
Not yet: non-constant field initialisers (L49, `struct-field-init`),
declarations after statements, in blocks or functions, exported structs,
instances in containers or passed to functions (D-047 #2 migration), the
`stringify()` rendering, `copy()`.

Evidence (development): `tests/runtime2/vm_structs.py` (in test-rt-vm):
35 shapes in five contexts, the 26 legacy probes pinned (5 on the VM), 12
further shapes, random struct programs (seeds 2-5 x 300: all identical to
legacy, ~80 % on the VM).

