# Checkpoint B4a: operation admission, sound dispatch and presentation

Status: **candidate, frozen for independent review** (B4a). Branch `cand/b4-dispatch`, built on the
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
- A proven struct receiver calling a method its struct does not declare: legacy's ordinary error
  `struct has no method: <name>` at the call, before any argument (was refused `struct-no-method`).
- N3: whether a receiver is the enclosing struct's `this` is the resolver's answer, not the
  receiver's text (`presentation_builtin_binds(..., bool inside)`, `Inference::own_this`).

**Presentation** (owner rulings; human-readable only, machine-readable serialization unchanged;
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
statements' and the module's output first). A later declaration keeps `import-collision` (legacy
rebinds silently or reports a redeclaration; the CP25-R `006-main` unit).

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

**Two context gaps closed** (coordinator addition; the generator of section 4):
- `context:object-length` now also covers a function called (transitively) from a `for` loop:
  `rt/prepared.h` marks only `while` callers, so an object's `length()` there compiled the canonical
  behaviour where legacy's prepared evaluator prints the member count
  (`UnitCompiler::object_length_context`). Number `to_double` has no such gap: every such call is
  refused already (`builtin-receiver`).
- In the same prepared contexts: a fraction's `to_int()` error (legacy reports the statement) is
  refused (`context:number-to-int`), and an empty collection's `empty()` (legacy answers false) is
  refused (`context:collection-empty`). Both were admitted divergences on the B3 tree.

**Carried** (section 9): `nift eval` display of a struct or collection; sessions' presentation
dispatch; `rt/prepared.h`'s `while`-only marking for its other refusals (review item).

## 2. Approved changes (old against new, with the reason)

| Unit | Old (B3 tree) | New | Reason |
| --- | --- | --- | --- |
| `collections/refuse/mixed_kinds.f` → `parity/` | refused `collection-receiver:size` | `ran`, `0` (=L) | Dynamic dispatch (§1) |
| `b3/refuse/n1_{clear,insert,pop,remove,reverse,splice1,splice2}_present.f` → `b3/parity/` | refused `presentation-struct-method:stringify` | the built-in renders the value now at `xs[0]` (=L) | run-time choice (StructIs) |
| `b3/refuse/presentation_method_{mixed,param}.f` → `b3/vm/` | refused | the struct's own method for its instance, the built-in otherwise | owner ruling 2026-11-05 (explicit call runs the method) |
| `refsem/refuse/presentation_struct_review_i04.f` → `refsem/approved/` | refused | as above; legacy `before`, 6, 7 | the same |
| `refsem/refuse/presentation_struct_review_i05.f` | refused `presentation-struct-method:stringify` | refused `type:length` | the call's result may be the method's 7: `.length()` is not proven (sound) |
| `b3/modules/{n1_clear,n1_pop,n1_remove,n1_reverse,n1_splice2}_present`, `visible_method` | `reason` (refused) | `vm` transcripts | run-time choice, module instances included |
| `struct-no-method` units (e.g. `c.trim()` on a proven struct) | refused | `struct has no method: trim` at the call (=L) | the admission table proves legacy fails there |
| `for (this : xs) { this.stringify() }` (private presentation method) | refused `struct-private` | built-in render (=L) | N3 |
| `collections/refuse/interpolation.f` → `approved/` | refused `interpolation:unknown-type` | ordinary error `map, set and stack values cannot be rendered as text` (legacy: its tag) | owner presentation ruling |
| `b3/disclose/interp_direct.f` | refused | ordinary error `struct instances cannot be rendered as text` (legacy: its tag) | owner ruling 2 |
| `safety/expected.json`: `i_atomic_*` (22 cases) | B0 error `opaque runtime value is not serializable` after the scalar part | the atomic's value (=L) | atomics by value |
| `safety/expected.json`: `h_atomic_*_session` (12), `h_atomic_session_tail` | B0 errors | the value shown | atomics by value |
| `safety/expected.json`: `h_map_session_bare_earlier`, `h_set_session_tail_earlier` | B0 errors | `map([])`, `set([])` | collections shown in a session |
| collections SESSIONS `collection_statement`, `collection_call_result` | refused `session:collection-result` | parity (`map([])`, `stack([])`) | the same |
| collections SESSIONS `collection_in_line` | refused | `nullset([])` (legacy: `null` + its tag) | the same; no tag |
| b3 SESSIONS `display-bare` | refused | parity `P{a:1,c:3}`; new `display-bare-user-method` (no user code runs) | owner ruling 2 |
| `tests/rt/vm_expression.cpp` (eval of `[x=>x, ...]`) | `\x1fnift:callable:lambda:1` | `opaque runtime value is not serializable` | owner: no internal tag |
| `modules-d041/export_clash`, `semantics-modules/export_collision`, `vm_modules_unit` clash.f, packages b08 | refused `import-collision` | program error `import-scope:collision`, legacy's error line | C1 |
| `vm_inventory_links` import-collision case | the declared-before shape | the declared-after shape (still the refusal) | C1 moved the old shape |
| object `length()` in a `for` loop's callee | compiled: malformed-expression error (legacy: the member count) | refused `context:object-length` | correctness (coordinator addition) |
| fraction `to_int()` in loop code or its callees | compiled: error at another column | refused `context:number-to-int` | correctness (generator) |
| empty collection `empty()` in loop code or its callees | compiled: `true` (legacy `false`) | refused `context:collection-empty` | correctness (generator) |

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
- `b4/dispatch/*.f` (26): VM-compiled, equal to legacy, or a `.vm` golden for an approved difference
  that must still differ; no internal tag, private value or implicit user method in any output;
  receiver evaluated once and argument effects as legacy in success and failure (`receiver_once.f`,
  `all_natives_effect_args.f`, `no_effect_args.f`);
- `b4/refuse/*.f` (11): refused before any effect with their reason;
- `b4/c1/*/` (6): the C1 legacy differential per shape;
- `b4/concurrent/*.f`: the atomic presented while a thread updates it (three runs, legacy's output);
- SESSIONS and DISPLAYS: dynamic dispatch in sessions, and every new display path (struct, map,
  atomic, function in sessions and `nift eval`) with non-disclosure checks;
- interpolation: 40 programs (five scalar families × four kinds without a text form × two contexts);
- the spec × kind enumeration: 50 names × 15 receiver kinds × 4 contexts = 3,000 programs: never
  exit 70, never an internal tag, `dispatch_defects` 0, parity for the plain kinds. 702 run on the
  VM (the B3 binary: 215).

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

## 7. Size

Production (`src/`, against `4c7d0b40`): 37 files, +905 −109. New: `op_admission.{h,cpp}`,
`compiler_dispatch.{h,cpp}`, `compiler_presentation.{h,cpp}` (moved from `compiler_structs.cpp`),
`dispatch_defect.h`; opcode `StructIs`. Tests and Makefile: 151 files, +1,467 −312.

## 8. Gates (local; result files in `scratchpad/b4/gates/`)

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
| `vm-adversarial` | PASS vm adversarial: 39612 programs, 0 non-approved differences, 196 approved {'D-001#4 recursion': 73, 'D-009 |
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


## 9. Carried items

- **B4b** (after B4a's review; owner ruling 6): sound per-site element and member typing for arrays
  and objects across aliases, nested collections, functions, closures, sessions, modules and
  transfers, widening where precision cannot be kept; mutant A2; its own independent review before
  B5. **Q9 caveat** (coordinator ruling 4, for the owner): B4b is on B5's dependency chain. After
  the guard goes, per-binding element facts are unsound, and operators have no Dynamic form, so B5
  without B4b would either refuse arithmetic on array elements (new refusals) or reach unchecked
  opcodes. The nested-receiver invalidation (`xs[0].reverse()`, `o.a.reverse()`; both refused today,
  B6) is covered by B4b's per-site widening.
- `nift eval` display of a struct or map/set/stack stays `eval:collection-result`: its `--json`
  mode is machine-readable, and the presentation form would need the eval entry to know the mode.
- Sessions: `presentation-struct-method` (earlier schemas' instances), and `$[c]` of an earlier
  statement's instance stays `interpolation:unknown-type` (the binding's kinds are not proven).
- Release audit: `rt/prepared.h` marks only `while` callers as prepared-reachable; legacy also runs
  a `for` loop's callees on its prepared evaluator. B4 closed the three context gaps the generator
  showed (object length, fraction to_int, collection empty); its other refusals (prepared-decl-type,
  string-arg-type, the `context:*` family) were not audited against `for` callees.
- `test-cp19-shared-eval` fails on the B3 binary too (`eval_entry.py` expects `NIFT_VM=require` in an
  unresolved-name refusal): pre-existing; not in the local gate list.
- Not run locally (combined B3–B5 certification): full `make test`, `test-rt-vm-sanitize` (with the
  new `test-runtime2-b4-tsan`), `test-build-reuse`, perf campaigns, whole-corpus classify.
