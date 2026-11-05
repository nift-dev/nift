# Checkpoint B4a: operation admission, sound dispatch and presentation

Status: **candidate, revised after review, frozen for narrow re-review** (B4a). The review of
`c56c4093` (REJECT: H1, H2, M1-M4, L1-L3) is answered by commits B4 9 to B4 16 on top of it
(section 10); the sections below are corrected in place. Branch `cand/b4-dispatch`, built on the
accepted B3 candidate `4c7d0b40` (`cand/b3-structs`, not yet integrated; B3, B4 and B5 are certified
together, owner's batching workflow). B4b (per-site element and member types) starts only after
B4a's review (coordinator ruling 4).

Plan: Phase B plan §5.6, §5.7 and "B4"; owner decisions of 2026-11-02 (Q9, rendering ruling), the
2026-11-04 addendum ("B3 decisions (2026-11-05 23:13)": the B4 acceptance invariants), the owner's
B4 rulings (relayed 2026-11-06) and the coordinator's plan rulings 1-7 (2026-11-06).

Tip, tree and gate results: section 8.

## 1. Contract

**The admission interface** (`src/rt/op_admission.{h,cpp}`). For a built-in method call
`receiver.name(args)` the receiver's inferred kinds decide one of:

| Admission | When | Code |
| --- | --- | --- |
| Static | `bind_method` binds one native (unchanged) | `CallNative` |
| Dynamic | the kinds are a closed union of two or more *plain* families (null, bool, number, string, array, object), or of map/set/stack for `size`/`empty`, and `admit_dynamic_method` admits it (below) | one `CallNative` flagged `kDynamicDispatch` (c operand bit 15) |
| Refuse | anything else | the existing refusal reason, before any effect |

Dynamic admission (`admit_dynamic_method`):
- the name is a method of some plain family; no Unknown kind; no handle, shared, collection,
  struct, function or future kind beside plain families (collections only as their own union);
- per family: its native of that name (arity within its range; never a bytes native), or legacy's
  failure for that family (`dispatch_failure`, below); a `Refuse` failure refuses the call;
- at least one family has the native;
- if any family fails, every argument is unobservable (a name or a constant): legacy fails before
  evaluating arguments; and a malformed-expression report needs a one-line call text;
- every candidate passes `builtin_arguments_ok` (the CP14 W1/W2/W4 rules, D2 fault context).

The inference types a Dynamic call as the union of its candidates' results over their branches of
the receiver (`infer_builtins.cpp`), from the same admission: compiler and inference agree by
construction. A failing family produces no value.

**Failure classes** (`dispatch_failure`, the one place; legacy oracle: section 3):

| Family without the method | Class | Text |
| --- | --- | --- |
| string, number, array | Unsupported | `<name>: unsupported for this value` |
| null, bool, object | Malformed | `unknown value or malformed expression: <call text>` |
| array or object, `to_string` | Composite | `to_string: use stringify() for composite values` |
| number `to_double`, object `length` | Refuse | context-dependent in legacy (loop code against functions) |
| any non-plain family, `to_string` (interpolation only) | Unrenderable | B0's text by kind (`natives::unrenderable_text`) |
| any other non-plain family | Refuse | not admitted |

**Error class (owner ruling on invariant 4).** "Unsupported operations produce ordinary, correctly
positioned program errors consistent with the existing language contract, never internal runtime
failures." These failures are fatal ordinary errors (exit 1), the class of every other native
error and of legacy; no new catchable built-in error is added in B4 (catchability is outside
Runtime 2.0's release scope). The class is set in one place, `dispatch_failure_catchable`
(returns false). Position: the call, as every native error; a failure inside a return expression
carries no `return: ` prefix (the approved D-009#5 change, as for every native error;
`b4/dispatch/return_position.f`).

**Run time** (`vm_builtin.cpp`). At a flagged site, a method native receives only a receiver of its
own family: the dispatch precondition (`receiver_accept` mask) fails for any other kind, and
`redispatch` (out of line) selects the same-name native of the receiver's family
(`dispatch_sibling`) or raises the admission table's failure (`dispatch_failed`). A family with
neither (a kind admission did not admit) is a dispatch defect: counted process-wide
(`rt/dispatch_defect.h`, NIFT_VM_STATS `"dispatch_defects"`), fatal in checked builds
(`NIFT_RT_RELOCATE_ON_MUTATION`), and the unsupported text is raised. Unflagged sites are
compile-time proven and pay nothing (section 6); their natives keep their own defensive checks, and
B0's defensive paths (`scalar_to_string`, `add_assign_slow`) now count a defect when reached. The
CP19 session lowering flags its guarded branches, so its unguarded last branch is under the
precondition (control S10).

**The redirection table** (owner ruling 2: only between equivalent operations). Entries exist only
within one group, and the reviewer can check each:

| Name | Families | Why it is the same operation |
| --- | --- | --- |
| `contains` | string, array | legacy's `contains` of each family (substring / element); legacy selects by the receiver's kind |
| `empty` | string, array, object | legacy's `empty` of each family |
| `length` | string, array | legacy's `length` of each family (code points / elements) |
| `size` | array, object | legacy's `size` of each family |
| `to_int` | string, number | legacy's `to_int` of each family |
| `to_string` | string, number, bool, null | legacy's scalar `to_string` of each family (number, bool and null share one native) |
| `empty`, `size` | map, set, stack | legacy's collection `empty`/`size` (only these two: admission admits no other mixed-collection method) |

No entry crosses the plain and collection groups (a string's `contains` is not a map's; legacy's
loop-element `map.contains` is a tag-string test), and handle and bytes families have none.
`op_admission_unit` checks every built-in × every run-time kind: the selected native is of exactly
that family and name, no entry crosses groups, and the precondition mask agrees with `receiver_of`.

**B0's root cause, structurally** (`compiler_interp.cpp`). `scalar_only`, which kept the scalar bits
of a mixed kind set and bound `to_string` from them, is deleted (`part_kinds` keeps the whole set).
A part mixing one scalar family with kinds that have no text form converts through a flagged
`to_string`; those kinds fail from the dispatch (Unrenderable), never from a native's defensive
path. Output is unchanged (safety suite and fuzz, b3 disclose, `b4_dispatch.py` interpolation:
40 programs). Mutant A9 restores the narrowing and is caught.

**Struct dispatch** (`compiler_presentation.cpp`, `compiler_structs.cpp`, `vm_struct.cpp`):
- An explicit `stringify`/`prettify`/`highlight` on a script or module receiver that may be an
  instance of a struct declaring that method: the receiver is evaluated once; per declaring struct,
  `StructIs` (new opcode: the instance's descriptor version against that struct's descriptor; a
  version is unique per declaration and compile and survives isolates) runs the struct's own method;
  anything else (another struct's instance included) renders through the built-in descriptor
  renderer. The inference types the call as the built-in's string united with each such method's
  result. Sessions and receivers of unknown kind keep `presentation-struct-method`.
- A proven struct receiver calling a method its struct does not declare, by a name that is not a
  string method's, outside prepared code: legacy's ordinary error `struct has no method: <name>` at
  the call, before any argument (was refused `struct-no-method`). Elsewhere the B3 refusal
  `struct-no-method` stands (review H1): legacy runs `substr` on the instance's tag text in every
  context, and every string method there in prepared code (loop bodies, their callees, struct
  methods conservatively). Acceptance: the reviewer's struct generator (section 4).
- `this` (N3, review H2): outside a struct, a binding named `this` is an ordinary binding (the
  resolver's answer). Inside a struct declaration, a declaration or parameter named `this` was
  already refused (`binding-name-reserved`, `param-name-reserved`); a `for`-loop or `catch`
  binding named `this` is now refused too (`struct-this-binding`, `rt/binding_rules.h`). So every
  `this` the receiver analysis (`compiler_structs.cpp`, `infer_walk.cpp`) meets inside a struct is
  the instance. The c56c4093 record claimed N3 resolved; only the outside-a-struct half was: inside
  a method, `for (this : xs)` reached the enclosing instance's methods, private ones included.

**Presentation** (owner rulings; human-readable only: `nift eval --json` keeps B3's output, a
function's tag string and an atomic's "not serializable" error (review L1: the mode is passed to
the conversion, `eval_result_to_legacy(..., json)`); the table's `nift eval` column is without
`--json`;
all observational: no user code, no mutation, no message consumed, nothing awaited, no lock beyond
the registry lookup):

| Kind | Session display | `nift eval` | `$[x]` alone | `$[x]` mixed | stringify etc. |
| --- | --- | --- | --- | --- | --- |
| struct instance | `C{a:1}` (descriptor, private skipped) **new** | refused (unchanged; carried) | ordinary error **new** (was refused) | ordinary error (B0) | descriptor (B3) |
| map, set, stack | compact `map([...])` **new** | refused (unchanged; carried) | ordinary error **new** (was refused) | ordinary error (B0) | compact (=L) |
| function | ordinary error **new** (was the internal tag) | ordinary error **new** (was the tag) | refused (unchanged) | ordinary error (B0) | ordinary error |
| atomic | current value **new** | current value **new** | current value **new** (=L) | current value **new** (=L) | ordinary error |
| mutex, channel | ordinary error | ordinary error | refused | ordinary error | ordinary error |
| future | refused (CP22) | refused | refused | refused | refused |

Atomic contract: one sequentially consistent load through `shared_atomic_load`, the runtime's
established atomic access (print's); the value shown is one the atomic held at that instant; no
data race. `b4/concurrent/atomic_presentation.f` presents an atomic while a thread updates it; it is
in `test-runtime2-b4-tsan` (TSan lane, `test-rt-vm-sanitize`, combined certification; a local TSan
build is not cheap on this machine: not run locally).

**C1** (`module_graph.cpp`): an import exporting a name already bound *before* that import (an
earlier top-level declaration, a function included, or an earlier import's export) is legacy's
program error `import: export collides with existing binding: <name>` at the import, rejected before
running (`import-scope:collision`, the invocation-export precedent: legacy prints the earlier
statements' and the module's output first), **only when legacy certainly reaches the collision
check** (review M2): every statement legacy runs before it, in its order (the unit's earlier
top-level statements, its importers' up to their import, earlier and the imported modules' bodies),
cannot stop the program: a function, struct or enum declaration, a module's export (checked by
the graph itself), a fresh `name := <literal>` (arrays and objects of literals included; a number
only when the VM's literal admission accepts it: re-review N1), `print(<plain string>)`. A
main-file `export` may stop it (legacy: "export names no existing binding"). Otherwise
(a top-level `return`, a statement that may fail, a module that may fail) and for a later
declaration (legacy rebinds silently or reports a redeclaration; the CP25-R `006-main` unit) the
refusal `import-collision` stands.

**C3** (no code change, coordinator ruling 6). The `opaque:legacy-runtime-error` regions are parser
level, not admission decisions. Per family, as seen in the corpora:

| Family | Example | Legacy | Why it stays refused |
| --- | --- | --- | --- |
| power operator | `print(2 ** 10)` | `expression cannot be empty` at the operand | the VM parser does not structure `**`; reproducing legacy's text needs its tokenizer's slice |
| string literal index | `print("abc"[1])` | `unknown value or malformed expression: "abc"[1]` | the quoted text is legacy's textual slice of an opaque region |
| nested quotes in interpolation | `"nested=$[s + "b"]"` | malformed expression quoting the whole literal | same |
| postfix spacing | `a [0]` | malformed expression `a [0]` | same |
| form feed, vertical tab | `print(1)\f` | malformed expression at 1:1 | same (whitespace legacy does not skip) |
| smoke `core_language_correctness`, `v44_scalar_conversion` (s, a), `v46_b4_cp1` | mixed | legacy run-time errors in such regions | same; their other reasons also apply |

Compiling them as run-time errors would need the admission table to know legacy's exact text and
position for an unstructured region; it does not. Recorded, unchanged.

**Prepared code includes `for` loops' callees** (review M4, M3). Legacy runs loop code and the
functions called (transitively) from any loop, `while` or `for`, on its prepared evaluator.
`rt/prepared.cpp` marked only `while` callers; it now seeds `for` loops too, both when marking
prepared functions and when checking calls of functions legacy cannot prepare
(`prepared-fn-unprepared`). Every prepared-evaluator refusal (`prepared-*`, `string-arg-type`,
`context:*`) therefore covers `for` callees (the reviewer's t2, t3, t6, t7, t8 are refused
fixtures). The three context refusals B4 added use that marking (`loop_reachable()`):
- `context:object-length`: an object's (or unknown receiver's) `length()` (legacy's prepared
  evaluator counts the members; canonical code fails). Number `to_double` has no such gap: every
  such call is refused already (`builtin-receiver`).
- `context:number-to-int`: a fraction's `to_int()` error (legacy reports the statement).
- `context:collection-empty`: any collection `empty()` in prepared code (legacy answers false for
  an empty one). Broad (it also refuses a non-empty collection's `empty()`, which would agree) but
  sound (review L3).

The c56c4093 tip computed the `for`-callee set by a whole-program walk per call site
(`object_length_context()`): quadratic compile time (the reviewer's 3,451-line program: 1.4 s
against B3's 30 ms). The walk is deleted; section 6 has the compile-time measurements.

**Carried** (section 9): `nift eval` display of a struct or collection; sessions' presentation
dispatch; the B5 prerequisite on unflagged sites.

## 2. Approved changes (old against new, with the reason)

| Unit | Old (B3 tree) | New | Reason |
| --- | --- | --- | --- |
| `collections/refuse/mixed_kinds.f` → `parity/` | refused `collection-receiver:size` | `ran`, `0` (=L) | Dynamic dispatch (§1) |
| `b3/refuse/n1_{clear,insert,pop,remove,reverse,splice1,splice2}_present.f` → `b3/parity/` | refused `presentation-struct-method:stringify` | the built-in renders the value now at `xs[0]` (=L) | run-time choice (StructIs) |
| `b3/refuse/presentation_method_{mixed,param}.f` → `b3/vm/` | refused | the struct's own method for its instance, the built-in otherwise | owner ruling 2026-11-05 (explicit call runs the method) |
| `refsem/refuse/presentation_struct_review_i04.f` → `refsem/approved/` | refused | as above; legacy `before`, 6, 7 | the same |
| `refsem/refuse/presentation_struct_review_i05.f` | refused `presentation-struct-method:stringify` | refused `type:length` | the call's result may be the method's 7: `.length()` is not proven (sound) |
| `b3/modules/{n1_clear,n1_pop,n1_remove,n1_reverse,n1_splice2}_present`, `visible_method` | `reason` (refused) | `vm` transcripts | run-time choice, module instances included |
| `struct-no-method` units with a name that is not a string method's, outside prepared code (e.g. `c.size(arg())` at top level) | refused | `struct has no method: size` at the call (=L) | legacy fails there (review H1: the struct generator, every admitted program equal to legacy) |
| `struct-no-method` with a string method's name (`c.trim()`, `c.substr(1, 3)`), or in prepared code (loops, their callees, struct methods) | refused | refused (unchanged; the c56c4093 tip compiled them: H1) | legacy runs the string native on the tag text there |
| `for (this : xs) { this.stringify() }` outside any struct | refused `struct-private` | built-in render (=L) | N3 (the resolver's binding) |
| `for (this : xs)` / `catch (this)` inside a struct (the c56c4093 tip ran the enclosing instance's methods: H2) | compiled (wrong receiver) | refused `struct-this-binding` | review H2 |
| `collections/refuse/interpolation.f` → `approved/` | refused `interpolation:unknown-type` | ordinary error `map, set and stack values cannot be rendered as text` (legacy: its tag) | owner presentation ruling |
| `b3/disclose/interp_direct.f` | refused | ordinary error `struct instances cannot be rendered as text` (legacy: its tag) | owner ruling 2 |
| `safety/expected.json`: `i_atomic_*` (22 cases) | B0 error `opaque runtime value is not serializable` after the scalar part | the atomic's value (=L) | atomics by value |
| `safety/expected.json`: `h_atomic_*_session` (12), `h_atomic_session_tail` | B0 errors | the value shown | atomics by value |
| `safety/expected.json`: `h_map_session_bare_earlier`, `h_set_session_tail_earlier` | B0 errors | `map([])`, `set([])` | collections shown in a session |
| collections SESSIONS `collection_statement`, `collection_call_result` | refused `session:collection-result` | parity (`map([])`, `stack([])`) | the same |
| collections SESSIONS `collection_in_line` | refused | `nullset([])` (legacy: `null` + its tag) | the same; no tag |
| b3 SESSIONS `display-bare` | refused | parity `P{a:1,c:3}`; new `display-bare-user-method` (no user code runs) | owner ruling 2 |
| `tests/rt/vm_expression.cpp` (eval of `[x=>x, ...]`) | `\x1fnift:callable:lambda:1` | `opaque runtime value is not serializable` | owner: no internal tag |
| `modules-d041/export_clash`, `semantics-modules/export_collision`, `vm_modules_unit` clash.f, packages b08 | refused `import-collision` | program error `import-scope:collision`, legacy's error line | C1 (every earlier statement cannot stop the program) |
| an import collision after a top-level `return`, a statement that may fail, or in a module that may fail | refused `import-collision` | refused (unchanged; the c56c4093 tip reported the collision: M2) | legacy stops first or reports the other error |
| `nift eval --json` of a function / an atomic | tag string / not serializable | unchanged (the c56c4093 tip changed both: L1) | machine-readable output unchanged |
| `nift eval` (without `--json`) of a function / an atomic | the tag / not serializable | ordinary error / the value | owner presentation ruling |
| `vm_inventory_links` import-collision case | the declared-before shape | the declared-after shape (still the refusal) | C1 moved the old shape |
| object `length()` in a `for` loop's callee | compiled: malformed-expression error (legacy: the member count) | refused `context:object-length` | correctness (coordinator addition) |
| every `prepared-*`, `string-arg-type`, `context:*` shape in a function called from a `for` loop (t2, t3, t6, t7, t8) | compiled (legacy differs: double argument evaluation, `1.5` against a modulo error, `a0.5b` against a type error, other error positions) | refused, the reason of the `while`-callee shape | review M4 |
| fraction `to_int()` in loop code or its callees | compiled: error at another column | refused `context:number-to-int` | correctness (generator) |
| empty collection `empty()` in loop code or its callees | compiled: `true` (legacy `false`) | refused `context:collection-empty` | correctness (generator) |

Approved diagnostic difference (review M1, compatibility.md): a malformed method call on a call
receiver with arguments in prepared code (`mk(i).starts_with("a")`) reports the source text;
legacy reports its prepared evaluator's hoisted argument names (`starts_with(__nift_native_arg_5)`).
Same output, line, column and exit status. Legacy's numbering cannot be reproduced; golden
`b4/dispatch/malformed_call_receiver_loop.vm`.

`classification.tsv`: rows for `struct-no-method` (resolved), `presentation-struct-method:*`
(sessions and unknown receivers only), `collection-receiver:*`, `session:collection-result`,
`eval:collection-result`, `import-collision`, and the new `import-scope:collision` (I).

## 3. The legacy oracle

`scratchpad/b4/probe/tbl/legacy.json`: every plain-family method name (50) × 7 receiver kinds × 3
contexts (loop code, a function, a function called from loop code), `NIFT_VM=0` on the B3 binary.
The failure class is uniform across contexts except number `to_double` and object `length` (both
refused). The suite's enumeration (section 4) replays it, with a fourth context (top level), on
both engines.

## 4. Tests and evidence

`make test-runtime2-b4` (`tests/runtime2/b4_dispatch.py`, prerequisite `test-rt-op-admission-unit`):
- `b4/dispatch/*.f` (26; `struct_no_method_this` moved to `refuse/`, `malformed_call_receiver_loop`
  added): VM-compiled, equal to legacy, or a `.vm` golden for an approved difference
  that must still differ; no internal tag, private value or implicit user method in any output;
  receiver evaluated once and argument effects as legacy in success and failure (`receiver_once.f`,
  `all_natives_effect_args.f`, `no_effect_args.f`);
- `b4/refuse/*.f` (24): refused before any effect with their reason (review: five `for`-callee
  shapes, three `struct-no-method` shapes, five `struct-this-binding` shapes: method call, private
  method, rendering, field read, catch binding);
- `b4/c1/*/` (14): the C1 legacy differential per shape; the three review M2 shapes (`return`
  before the import, a failing statement before it, a failing module) and the five re-review N1
  shapes (an out-of-range integer, `1e999` in an object, a main-file `export(nosuch)`, a module's
  `1e999`, a second module's `9223372036854775808`) check that legacy gives its own error and never
  the collision;
- struct receivers (review H1; the reviewer's `gen_struct.py` in the suite): a proven struct
  receiver × every method name (91) × seven contexts (top level, statement, `for`, `while`,
  function, lambda, method) × three arities = 1,911 programs; every program the VM runs (867)
  equals legacy, D-009#5 aside (the `return: ` prefix and column, 213 in function bodies); never
  exit 70, `dispatch_defects` 0. On the c56c4093 binary it reports the H1 divergences (`--quick`:
  21 failures);
- compile scaling (review M3, re-review N2): 250 and 1,000 functions using the three `context:*`
  sites, and a natural-order call chain called from a `for` and from a `while` loop; four times the
  size must take at most eight times the compile time (functions: 4.1x on the tip, 14.5x on
  c56c4093; chain: 4.9x / 6.5x on the tip, 11.7x on `569e336f`, caught);
- `nift eval --json` (review L1): B3's exact output for a function and an atomic, nested too;
- `b4/concurrent/*.f`: the atomic presented while a thread updates it (three runs, legacy's output);
- SESSIONS and DISPLAYS: dynamic dispatch in sessions, and every new display path (struct, map,
  atomic, function in sessions and `nift eval`) with non-disclosure checks;
- interpolation: 40 programs (five scalar families × four kinds without a text form × two contexts);
- the spec × kind enumeration: 50 names × 15 receiver kinds × 4 contexts = 3,000 programs: never
  exit 70, never an internal tag, `dispatch_defects` 0, parity for the plain kinds. 681 run on the
  VM (702 on c56c4093, which compiled struct receivers' string-method names: H1; the B3 binary: 215).

The reviewer's wrong-kind generator (`review-b4/repro/gen1.py`, independent of the suite; run on
the tip after the fixes, not part of a gate): pass a (`for`, `for` callee) 20,274 programs, pass b
(eight other contexts, 6,000 sampled) : never exit 70, no tag leak, no internal text, 0 defects.
Of the programs the VM runs, 131 (a) and 57 (b) differ from legacy; every one gives the same VM
output on the B3 binary (pre-existing: presentation of handles as tags, CP17/B0; a single-kind
object's non-method text; `remove` argument column), except 4 of the M1 class (approved, §2).

`tests/rt/op_admission_unit.cpp`: admission decisions and failure texts; every built-in × every
run-time kind through the tables; 357 native calls through the table, none returns
`Status::Internal`.

`vm_adversarial_gen.py` family `mixedrecv` (2,100 programs; `vm_adversarial.py` fails on
`dispatch_defects > 0` too): 0 differences, 0 defects. It found the two context gaps of §1; on the
B3 binary it reports 2 differences (the `to_int` loop column, pre-existing).

**Evidence hierarchy** (owner ruling 3). The primary evidence that no runtime-reachable kind reaches
an internal failure is structural and adversarial: mutant A5 (a dispatch failure made internal is
caught by the suite), A4 (no redirection: natives meet foreign receivers), the 3,000-program
enumeration and the `mixedrecv` generator, and the B0 fixed-seed fuzz (33,512 programs). The
`dispatch_defects` counter is supporting evidence: it only sees flagged sites and B0's defensive
paths.

## 5. Mutants (`make test-runtime2-b4-mutants`; each must fail the suite or the unit)

| Mutant | File | Defect | Caught by |
| --- | --- | --- | --- |
| A1 | `natives/builtin.cpp` | the dispatch table omits arrays | suite 9, unit |
| A3 | `op_admission.cpp` | admission refuses every Dynamic case | suite 38, unit |
| A4 | `vm_builtin.cpp` | no redirection (a native meets a foreign receiver) | suite 61 |
| A5 | `vm_builtin.cpp` | a dispatch failure is internal (exit 70) | suite 61 |
| A6 | `op_admission.cpp` | unsupported and malformed texts swapped | suite 61, unit |
| A7 | `vm_struct.cpp` | StructIs ignores the instance's descriptor | suite 2 |
| A9 | `compiler_interp.cpp` | B0's narrowing restored | suite 43 |
| A10 | `compiler_structs.cpp` | struct-no-method refused again (precision) | suite 5 |
| A11 | `eval_result.cpp` | a function's display shows its tag again | suite 4 |
| A12 | `module_graph.cpp` | C1 off | suite 5 |
| A13 | `vm_builtin.cpp` | interpolation does not load an atomic | suite 2 |
| A14 | `prepared.cpp` | only `while` callers are prepared code (review M4) | suite 13 |
| A15 | `compiler_structs.cpp` | every undeclared struct method compiled as legacy's error (review H1) | suite 28 |
| A16 | `binding_rules.cpp` | a `this` loop or catch binding in a struct admitted again (text match, review H2) | suite 10 |
| A17 | `module_graph.cpp` | C1 whatever legacy runs before the import (review M2) | suite 9 |
| A18 | `module_graph.cpp` | any number literal and a main-file export cannot stop the program (re-review N1) | suite 6 |
| S6, S7 (B0, revised) | `natives/format_methods.cpp`, `vm_ops.cpp` with A9 | B0's defensive paths internal again, with the narrowing restored (B4 made them unreachable otherwise) | safety suite (54 and 46 cases exit 70) |
| S10 (control) | `compiler_session_native.cpp`, CP19 mutant 10 | the session's guarded branch forced to the string native | covered: the precondition redirects it, output equals legacy (before B4 the mutant stopped with exit 70) |

B3's D8 and D9 (implicit path dispatching to a user method; the built-in despite a user method)
remain and are caught (`test-runtime2-b3-mutants`). A2 (a stale per-binding element type after an
alias) belongs to B4b.

## 6. Local performance (callgrind, guidance only; Linode is authoritative in the combined certification)

Marginal instructions per iteration, the B3 binary against the B4a tip:

| Workload | B3 | B4a |
| --- | --- | --- |
| `run.py --groups ops`: loop, add_local, array_index, array_push, string_concat, if_branch | 117, 188, 436, 367, 3313, 249 | identical |
| ops: call, object_member, fib_call (both) | 566, 762, 393 | 567, 763, 394 |
| `vm_compare.py --groups mutation`: array_set, array_push_pop, array_insert_pop, map_set, set_add, stack_push_pop | 381, 1430, 2446, 1496, 1363, 2190 | identical |
| built-in method calls: `s.trim()`, `a.contains(2)`, `i.to_string()`, `o.has("a")` | 1069, 897, 1182, 864 | 1074, 903, 1187, 870 |
| a Dynamic `v.contains("a")` over `["xa", ["a"]]` (refused on B3) | — | 2844 per iteration (two calls) |

The +1 on call and fib appears in the legacy engine's column too (`vm_compare` fib: legacy 394
against 393), which B4 does not touch: binary layout, not VM work. The built-in call path pays about
5 instructions (the flag test and the window mask). **Trade-off (coordinator):** the precondition
on every built-in call cost about 15 more instructions per call (1.4%), so it runs only at flagged
sites; the redirection table is built on first use (built eagerly it cost 2.3M instructions at
startup). Website builds do not execute the scripting compiler or the VM.

**After the review fixes** (B4 9-16): the VM and the natives are unchanged, and so is run time:
the built-in rows above and the Dynamic `contains` measure the same on c56c4093 and the tip (1074,
903, 1187, 870; 2844). **Compile time** (review M3; `compile_us`, NIFT_VM_STATS, best of three):

| Program | B3 | c56c4093 | tip |
| --- | --- | --- | --- |
| reviewer's `M3_compile_quadratic.f` (3,451 lines, 150 functions) | 36.7 ms | 1,450.7 ms | 27.2 ms |
| suite scaling program, 250 functions | 5.9 ms | 33.9 ms | 6.0 ms |
| suite scaling program, 1,000 functions | 26.1 ms | 480.0 ms | 26.2 ms |

The tip is within noise of B3 (at most 1.02x), well inside the 1.5x bound; the suite's scaling
check guards the shape.

Call chains (re-review N2; the reviewer's `repro2/perf`, median of five): the prepared marking now
walks each newly marked body once (a worklist) instead of sweeping to a fixpoint.

| Program | B3 | `569e336f` | tip |
| --- | --- | --- | --- |
| 2,000-function chain called from a `for` loop | 32.6 ms | 304.8 ms | 33.8 ms (1.04x) |
| the same chain called from a `while` loop | 320.9 ms | 322.9 ms | 36.7 ms (0.11x) |
| `m3x4.f` (four times the M3 program) | 140.8 ms | 133.0 ms | 136.9 ms |

## 7. Size

Production (`src/`, against `4c7d0b40`): 37 files, +905 −109. New: `op_admission.{h,cpp}`,
`compiler_dispatch.{h,cpp}`, `compiler_presentation.{h,cpp}` (moved from `compiler_structs.cpp`),
`dispatch_defect.h`; opcode `StructIs`. Tests and Makefile: 151 files, +1,467 −312.

## 8. Gates (local; result files in `scratchpad/b4/gates/`)

**After the review fixes** (result files `r-*.result`): run on `dbe57017` (tree `22a990f8`; the
following commit changes this record only). Every gate: exit 0, git status clean before and after.

| Gate | Result |
| --- | --- |
| `test-rt-lint` | PASS rt-lint, boundary: 0 problems |
| `test-rt-build` | PASS (builder mutant B-M2 caught) |
| `test-rt-op-admission-unit` | 4027 checks, 0 failures |
| `test-runtime2-b4` | PASS b4-dispatch: 12636 checks, 0 failures (struct receivers 1,911 programs, 867 on the VM; enumeration 3,000, 681 on the VM: struct receivers' string-method names are refused again, H1; compile scaling 6.0 / 25.2 ms) |
| `test-runtime2-b4-mutants` | PASS: 16 caught or covered (A1 9, A3 40, A4 61, A5 61, A6 61, A7 2, A9 43, A10 4, A11 4, A12 5, A13 5, A14 13, A15 28, A16 10, A17 4 failing checks; S10 covered) |
| `test-runtime2-b3`, `-b3-mutants` | 468 checks, 0 failures; 11 caught |
| `test-runtime2-safety` | PASS fuzz: 33512 programs |
| `test-cp19-session-cli`, `test-cp19-functional` | PASS |
| `test-runtime2-packages` | 830 checks, 0 failed |
| `test-runtime2-vm-default`, `-refsem`, `-collections` | PASS (475, 278 checks) |
| `vm-modules-unit`, `vm-inventory-links`, `semantics-modules` | 179 checks; 10 cases; PASS |
| `vm-adversarial` | 39612 programs (default `--count 600`, seed 1), 0 non-approved differences. At `--count 5000` (seed 1; the re-review's run) there are 8 non-approved differences, all pre-existing (the B3 binary has 10), none in dispatch or presentation |

The table below is the c56c4093 set (kept for reference).

Through `scratchpad/b4/gate.sh` (tree identity, command, exit code, log sha256, git status before and after). The set ran on `ad4ef25d` (tree `6af73259`); the two mutant gates that failed there on harness file maps (B4 8a) ran again on `ce92a319` (tree `0ded3593`, harness-only change). Every gate below: exit 0, git status clean before and after.

| Gate | Result |
| --- | --- |
| `cp19-expression-unit` | CP19 expression ownership: 1439 checks, 0 failures |
| `semantics-modules` | PASS modules-d041 (legacy-today): 22 fixtures, 0 failure(s) |
| `test-c-abi-symbols` | PASS abi-consumer: built outside the tree, linked dynamically, 34 nift_* symbols resolved from the library at  |
| `test-cp19-functional` | PASS retained-reader numeric guard and legacy channels |
| `test-cp19-session-cli` | PASS retained-reader numeric guard and legacy channels |
| `test-production-deps` | PASS production-deps: 1488 production compile commands, no dependency in tests/ (-M, system headers included) |
| `test-rt-build` | PASS builder mutant B-M2 caught: 4 observed changes to a published value (rt build unit (release): 207 checks, |
| `test-rt-isolation` | PASS isolation: production cannot reach the storage-level test helpers |
| `test-rt-lint` | PASS rt-lint boundary: 0 problem(s) in 698 files (src, include, bindings, markuppp, minifypp and their include |
| `test-rt-mutation-protocol-mutants` | PASS M-B1 mutant 6 caught: tests/rt/mutation_protocol.cpp:399: CHECK failed: injected && unchanged |
| `test-rt-mutation-protocol` | mutation-protocol: 191 checks, 0 failures |
| `test-rt-op-admission-unit` | op-admission-unit: 4027 checks, 0 failures |
| `test-rt-protocol-compile-fail` | PASS compile-fail control compiles |
| `test-rt-refrep-mutants` | PASS refrep mutant 31 (src/rt/vm_report.cpp) caught:   case literal-nested-json: refusal '' output [{"a":[1,9] |
| `test-rt-refrep` | refrep-vm: 129 checks, 0 failures |
| `test-rt-value` | heap-limit-zero: PASS |
| `test-runtime2-args-cmd-mutants` | PASS mutant 6 (src/rt/compiler_invocation.cpp): caught, 15 failing check(s), first: FAIL read args before a to |
| `test-runtime2-args-cmd` | PASS heap: cmd ends with the live heap of the string literal |
| `test-runtime2-b3` | PASS b3-structs: 468 checks, 0 failure(s) |
| `test-runtime2-b4-mutants` | PASS b4 mutants: 12 caught or covered |
| `test-runtime2-b4` | PASS b4-dispatch: 9820 checks, 0 failure(s) |
| `test-runtime2-collections-mutants` | PASS collections mutants: 11 caught |
| `test-runtime2-collections` | PASS collections: 278 checks, 0 failure(s) |
| `test-runtime2-coro-module` | PASS coro-module: 0 failure(s) |
| `test-runtime2-cp14b-design-mutants` | SKIP UNVERIFIED COMPARATOR COVERAGE: design mutants: 1 comparator case(s) refused with method:sort on an unver |
| `test-runtime2-cp14b` | SKIP UNVERIFIED COMPARATOR COVERAGE: origins: 25 comparator case(s) refused with method:sort on an unverified  |
| `test-runtime2-cp22` | PASS cp22 print-order evidence |
| `test-runtime2-cp23` | PASS cp22 default: 87 case(s), 0 failure(s) |
| `test-runtime2-packages-mutants` | PASS mutant 25 (src/rt/module_graph.cpp): caught, 4 failing check(s), first: FAIL S c11 args exported before c |
| `test-runtime2-packages` | packages: 830 checks, 0 failed |
| `test-runtime2-refrep-cli` | PASS refrep-cli: 7 checks, 0 failure(s) |
| `test-runtime2-refsem-mutants` | PASS refsem mutants: 23 caught |
| `test-runtime2-refsem` | PASS refsem-a: 475 checks, 0 failure(s) |
| `test-runtime2-safety` | PASS runtime2 safety fuzz: 33512 programs (8512 exhaustive, 20000 sampled, 5000 session; seed 1; 1676 run twic |
| `test-runtime2-vm-default` | PASS VM-default policy |
| `vm-adversarial` (`--count 600`) | PASS vm adversarial: 39612 programs, 0 non-approved differences, 196 approved {'D-001#4 recursion': 73, 'D-009 |
| `vm-inventory-links` | PASS vm inventory links: 10 cases, 0 failure(s) |
| `vm-modules-unit` | rt vm modules unit: 179 checks, 0 failures |
| `vm-parity` | PASS vm parity |
| `test-runtime2-b3-mutants` (on ce92a319) | PASS b3 mutants: 11 caught (D8-D10 now built from compiler_presentation.cpp) |
| `test-runtime2-safety-mutants` (on ce92a319) | PASS safety mutants: 7 caught (S6, S7 built with A9) |

`test-runtime2-cp14b`, `-cp14b-design-mutants`: the comparator-profile UNVERIFIED/SKIP lines are
pre-existing and identical to B3 (design mutants 1-11 detected, 12 unverified).

**CP25-R** (`classify-all` over the release corpus, 589 units; outcome, reason, line, column,
reasons and code compared, the B3 binary against the B4a tip): **0 changes**. The C1 unit
(`006-main`, a declaration after the import) keeps `import-collision`, as ruled.

**`tests/runtime2` classify** (1,893 scripts, the B3 binary against the B4a tip): only the units of
section 2 change (plus the new `b4/` fixtures); no other fixture changes classification.


**After the review fixes** (the c56c4093 binary against the tip, same tree of fixtures):
- CP25-R `classify-all` (589 units): **0 changes** (outcome, reason, line, column, reasons, code).
  The `for`-callee marking adds no refusal in the release corpus.
- `tests/runtime2` classify (1,963 scripts): 16 changes, all new review fixtures, all from
  compiled (or another refusal) to a refusal before execution: `struct-this-binding` 5,
  `struct-no-method` 3, `import-collision` 3 (the M2 shapes; c56c4093 reported the collision),
  `context:assignment-error` 2, `context:modulo-assignment`, `context:string-append-type`,
  `prepared-fn-unprepared` 1 each. No pre-existing fixture changes classification
  (`modules-d041/export_clash` and `semantics-modules/export_collision` keep C1's program error:
  their modules declare an array literal, B4 13a).

## 9. Carried items

- **B4b** (after B4a's review; owner ruling 6): sound per-site element and member typing for arrays
  and objects across aliases, nested collections, functions, closures, sessions, modules and
  transfers, widening where precision cannot be kept; mutant A2; its own independent review before
  B5. **Q9 caveat** (coordinator ruling 4, for the owner): B4b is on B5's dependency chain. After
  the guard goes, per-binding element facts are unsound, and operators have no Dynamic form, so B5
  without B4b would either refuse arithmetic on array elements (new refusals) or reach unchecked
  opcodes. The nested-receiver invalidation (`xs[0].reverse()`, `o.a.reverse()`; both refused today,
  B6) is covered by B4b's per-site widening.
- **`nift eval --json` of a function** emits its internal tag string (`"\u001fnift:callable:..."`),
  as B3 did: a pre-existing JSON-mode behaviour, kept unchanged by B4 (review L1) and carried for an
  owner decision (re-review).
- `nift eval` display of a struct or map/set/stack stays `eval:collection-result`: its `--json`
  mode is machine-readable, and the presentation form would need the eval entry to know the mode.
- Sessions: `presentation-struct-method` (earlier schemas' instances), and `$[c]` of an earlier
  statement's instance stays `interpolation:unknown-type` (the binding's kinds are not proven).
- **B5 prerequisite (review; recorded for the owner).** The dispatch precondition runs at flagged
  sites only. That is acceptable only while the reference guard is on: today an unflagged site's
  receiver kind is a compile-time fact the guard keeps true. B5 does not remove the guard until,
  for every unflagged method-site class, either B4b's per-site typing is implemented and
  independently reviewed, or the dispatch precondition is in force at that class. (Site classes:
  aliases, nested collections, closures, sessions, modules, transfers; the precondition on every
  method native call costs about 15 instructions per call, section 6.)
- `for`-loop callees in `rt/prepared.cpp` (review M4): closed (section 1). `outermost_loops` (the
  loop-local declaration typing check) still visits `while` loops only: probes of the shapes it
  guards (`a := 2.5; for (...) { a := "s"; b := a }`) equal legacy in a `for` loop
  (`scratchpad/b4/fix/p/d1.f`, `d3.f`); not audited beyond that.
- `test-cp19-shared-eval` fails on the B3 binary too (`eval_entry.py` expects `NIFT_VM=require` in an
  unresolved-name refusal): pre-existing; not in the local gate list.
- Not run locally (combined B3–B5 certification): full `make test`, `test-rt-vm-sanitize` (with the
  new `test-runtime2-b4-tsan`), `test-build-reuse`, perf campaigns, whole-corpus classify.

## 10. Review of c56c4093 (REJECT) and the fixes

Fixes are new commits on top of `c56c4093` (no history rewritten):

| Finding | Fix | Commit | Evidence |
| --- | --- | --- | --- |
| H1 `struct has no method` compiled where legacy runs a string native on the tag | compiled only for non-string-method names outside prepared code; else `struct-no-method` (B3) | B4 10 | struct generator in the suite (1,911 programs, 0 non-approved differences); fixtures; A15 |
| H2 `this` resolved by text inside a struct (`for (this : xs)`) | `for`/`catch` binding named `this` inside a struct refused (`struct-this-binding`) | B4 11 | 5 fixtures; A16; record N3 corrected (section 1) |
| M1 malformed call-receiver text in loop code | kept; approved diagnostic difference | B4 12 | golden; compatibility.md |
| M2 C1 where legacy stops first | C1 only when nothing legacy runs first can stop it | B4 13, 13a | 3 fixtures with legacy differential; A17 |
| M3 quadratic compile (whole-program walk) | walk deleted; the prepared marking reused | B4 9 | section 6; suite scaling check |
| M4 `for` callees outside every prepared refusal | `rt/prepared.cpp` seeds `for` loops | B4 9 | t2, t3, t6, t7, t8 fixtures; A14 |
| L1 `nift eval --json` changed | JSON mode keeps B3's behaviour | B4 14 | exact B3 output checked |
| L2 stale header comment | corrected | B4 15 | `op_admission.h` |
| L3 `context:collection-empty` broad | recorded: broad but sound | B4 16 | section 1 |
| B5 prerequisite | recorded | B4 16 | section 9 |

**Re-review of `569e336f` (ACCEPT WITH CONDITIONS)** and the fixes (commits on top of it):

| Item | Fix | Commit | Evidence |
| --- | --- | --- | --- |
| N1 C1 admitted literals legacy rejects and a main-file export | a number literal only when the VM admits it; a main-file export may stop | B4 18 | five `b4/c1` fixtures (repro2 `M2r_*`) with legacy's error; A18 |
| N2 marking quadratic in call-chain depth | worklist | B4 19 | chain shapes in the scaling check; section 6 |
| §9 B5 wording | the coordinator's sentence | B4 20 | section 9 |
| vm-adversarial count, `--json` tag | stated exactly | B4 20 | sections 8, 9 |

Gates after the re-review fixes (result files `r3-*.result`; run on `07bc775f`, tree `85e03e1c`;
the following commit changes this record only; every gate exit 0, git status clean before and
after): `test-rt-lint`; `test-rt-op-admission-unit` (4027 checks); `test-runtime2-b4` (12648
checks, 0 failures); `test-runtime2-b4-mutants` (17 caught or covered; A17 9, A18 6 failing
checks); `test-runtime2-b3` (468 checks); `test-runtime2-safety` (33512 programs);
`vm-modules-unit` (179 checks); `semantics-modules`; the compile scaling check (functions 6.0 /
26.2 ms, chain-for 2.8 / 14.5 ms, chain-while 2.8 / 13.5 ms).

Classification (the `569e336f` binary against `07bc775f`): CP25-R `classify-all` (589 units):
0 changes. `tests/runtime2` (1,974 scripts): 5 changes, the five new N1 fixtures, from the
program error `import-scope:collision` to the refusal `import-collision`; no other script changes.
