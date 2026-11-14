# B0: pre-release safety (cand/b0-safety)

Status: **candidate**, review round 1: the independent review of `8a9dc923`
rejected it on one HIGH finding (F1, section 7); not integrated. Branch `cand/b0-safety`,
built on `weekend/r2` @`33904a6f` (reference semantics Phase A integrated).
Plan: Phase B plan §6 "B0"; owner decisions of 2026-11-02 (B0 ruling). B0 is
standalone: it changes no semantics of an admitted program that ran to
completion, no admission decision, legacy (`NIFT_VM=0`), the Phase A guard or
reference semantics.

Tests: `make test-runtime2-safety` (`tests/runtime2/safety/safety.py`, the
recorded transcripts in `tests/runtime2/safety/expected.json`, and
`tests/runtime2/safety/fuzz.py`, a fixed-seed sweep),
`make test-runtime2-safety-mutants` (`tests/runtime2/safety/mutants.sh`,
S1-S7). Both are prerequisites of `test-rt-vm`.

## 1. Contract

**The rule.** An admitted program never stops with an internal VM failure
(exit 70, "NIFT_VM internal failure", "unexpected operands (compiler
defect)") because of an operand kind it can produce at run time. Two
categories, chosen per operation:

| Category | When | What the program sees |
| --- | --- | --- |
| Unsupported VM capability | the compiler can establish it cheaply from information it already has | a pre-effect whole-program refusal with a truthful position (`not compiled for the VM (reason)`, "no part of it ran"); in a session, the statement is refused |
| Supported operation, invalid run-time operand | the kind is only known at run time, or the language defines the operation for every value and rejects some | an ordinary program error at the operation's real position, after the earlier output; exit 1 (a piped session reports the statement's error and continues, exit 0, as legacy's sessions do) |

No new static analysis: B0 adds no admission rule and no refusal. Where an
operation is already refused before effects (an existing reason), B0 keeps
that refusal. Error text follows existing VM wording; matching legacy's text
or error order is not required (owner priorities, 2026-11-01), though both
fixes below are legacy-equal where legacy has an error at all.

**Catchability.** Nift's `try`/`catch` catches the recoverable errors (a code
in `natives::is_recoverable_code`: I/O, channel, cancellation, reference
cycle, schema rejection). Every other program error is fatal on both engines:
`pop: array is empty`, a function value's `opaque runtime value is not
serializable`, legacy's `unknown value or malformed expression`. Both B0
categories here are that ordinary fatal kind: the `try` does not catch them,
exactly as for the equivalent function-value and legacy errors
(`h_*_try`, `h_function_try_control`, `m_args_push_try` against NIFT_VM=0;
`catch_control` shows `try` does catch a recoverable error). Making them
catchable would be a language change, not a safety fix.

**Internal stays internal** only for a genuine compiler or runtime invariant
that no program can reach by construction (section 3 says why for each).
Heap-budget stops (`memory budget exceeded`) keep their existing exit-70
contract (refsem `heap/`): they are a resource stop, not an operand kind.

## 2. The two defects

### (a) Presenting a handle: an ordinary runtime error

`mutex()`, `channel()`, `atomic<int>()`, `atomic<bool>()` values (and
futures) reached `serialize_at`'s `default:` (`natives/format_methods.cpp`),
set `bad_kind`, became `Status::Internal`, then exit 70 (`vm_builtin.cpp`),
alone or at any depth in an array, object, map or stack, through
`stringify`, `prettify` and `highlight`, at top level, in functions,
closures, imported modules and piped sessions.

**Decision: category 2.** Presentation is a supported operation defined over
every value; CP17 abolished the string encoding of handles, so an opaque
value has no presented form, and the VM already reports that as an ordinary
error for the other opaque kind, functions: `opaque runtime value is not
serializable`. Handles and futures now take the same path. A pre-effect
refusal was not chosen even where the compiler can see the handle (`m :=
mutex(); m.stringify()`): it would be a second rule for the same operation
(a function value is a run-time error today), it is new admission work
(a handle three containers deep, a parameter, a map get), and the kind is in
general only known at run time (`fn(show(x)) { return [x].stringify() }`).

The same defect class, also exit 70 on `weekend/r2`, fixed the same way:

| Path | Before | Now |
| --- | --- | --- |
| `validate(schema, v)` with a function, handle, future, map/set/stack or struct instance inside `v` (`json_methods.cpp` `to_document`) | exit 70 | `validate: opaque runtime value is not serializable`; `validate: map, set and stack values are not JSON serializable`; `validate: struct instances are not JSON serializable` |
| Session display of a statement value with no shown form: a bare `m`, `mutex()`, a map from an earlier statement, `v.set("k", mutex())` (CLI `run_vm_session_statement`) | the session stops, exit 70 | the statement's ordinary error, the session continues (`nift eval` already reported it as an error, exit 2). For a mutator statement such as `v.set("k", mutex())` the error is about displaying its result: the mutation has already happened when it appears (legacy shows the handle's tag) |
| Session statement tail: `m := mutex(); m`, `x := 1; s` with an earlier collection (`vm_session_render.cpp`) | exit 70 | `opaque runtime values cannot be rendered as text in $[m]`; `map, set and stack values cannot be rendered as text in $[s]` (the wording of its Error/bytes neighbours) |

The shown-result converter (`eval_result.cpp`) reports a handle or future as
`opaque runtime value is not serializable` instead of `typed handles have no
legacy representation` (the `nift eval`, session display and command
interpolation `$[...]` messages; only the text changes there). `print(h)`
and `print([h])` were already ordinary (`print: value is not directly
renderable`); `"$[h]"` was already refused (`interpolation:unknown-type`);
host handles (`file`, `ifstream`, `ofstream`) and tasks/futures/threads in a
presented position are already refused before effects (`host-handle-flow`,
`coro-future-flow:*`); `json_stringify` is not a script built-in on either
engine (`callable:json_stringify`). The suite records each of those.

### (b) A wrong-kind mutator receiver: an ordinary runtime error

`print("before"); for (a : args) { a.push(1) }` with one or more script
arguments stopped with `array mutation receiver` (`vm_array_mutation.cpp`).
The general class: a statement-form `x.push(v)` whose receiver is a loop
binding, a selector or a const binding is lowered to `ArrayMutate` without a
receiver kind proof (`compiler_stmt_expr.cpp`); any loop over strings,
numbers, null, objects, set or stack members, `range()`, or another
collection reached it (`["s"]`, `range(2)`, `set()`, `stack()`, a function,
a closure, a module, a session).

**Decision: category 2** (owner ruling). `op_array_mutate` reports a
non-array receiver as `unknown value or malformed expression: a.push(1)` at
the call: the wording `OpCode::Push` (`push_value`) already uses for the same
condition, and legacy's own error at the same position, so the
fixtures are byte-identical to `NIFT_VM=0` (`m_args_push_*`, `m_push_*_loop`,
`m_push_rangeloop`). The compiler attaches the call text as the instruction's
note (`compiler_array_mutation.cpp`, one line; no admission change). A const
binding's check instruction (`c=100`, which carries the binding name) has
the fallback `<method>: receiver is not an array`; no admitted program
reaches it with a non-array (const non-scalars are refused, `declare-const`,
`type:push`).

The other array mutators (`pop`, `insert`, `remove`, `splice`, `sort`, with
or without a comparator, `reverse`, `clear`), map/set/stack mutators and
reads (`set`, `remove`, `add`, `push`, `pop`, `clear`, `get`, `contains`,
`top`) and struct field stores (`p.v = 1`) on a string, number, object, null,
array or other-collection receiver through a loop element, a parameter or a
map get are already refused before effects (`method:*`, `builtin-receiver:*`,
`collection-receiver:*`, `type:push`, `struct-value`,
`static-error:member-assign`, the Phase A guard) or are well-kinded; the
suite records each. `args` in a piped session stays refused
(`reference-guard:shared-part`, the recorded Phase A over-refusal).

## 3. Sweep

Method: every `Status::Internal`, `internal(...)`, `StopReason::Internal`
and `bad_kind` in `src/rt/natives/*`, `vm_builtin.cpp`, `vm_array_mutation.cpp`,
`vm_keyed.cpp`, `vm_struct.cpp`, `vm_ops.cpp`, `vm_iter.cpp`, the
collection/sort helpers, and the paths found reachable while probing
(session display and render). Evidence: 955 probe programs (receivers of
every kind through loop elements, set/stack/range loops, `args`,
parameters, map gets, closures, modules, sessions), run on the
`weekend/r2` binary and on this branch; the 57 that exit 70 on `weekend/r2`
are exactly the rows marked "fixed", and none exits 70 here. The
generated suite (1,062 cases) is the permanent form of that evidence.

| Site | Reached by | Status |
| --- | --- | --- |
| `format_methods.cpp` `serialize_at` default, Handle/Future | stringify/prettify/highlight of a handle at any depth | **fixed** (S1) |
| `format_methods.cpp` `serialize_at` default, Struct | presentation of an instance | invariant: `presentation-struct` / `presentation-struct-contents` refuse every presentation in a unit that declares a struct (`unit_may_hold_structs`, whole linked unit and session structures); no declaration, no instance |
| `format_methods.cpp` `serialize_at` default, Cell | | invariant: a cell is never a first-class value (`vm_report.cpp` "cell reached a native") |
| `json_methods.cpp` `to_document` default (Function, Handle, Future, Collection, Struct) | `validate({}, v)` with `v` holding one | **fixed** (S3) |
| `CLI.cpp` session display, `eval_result_to_legacy` failure, `return 70` | a session statement value with no shown form | **fixed** (S4) |
| `vm_session_render.cpp` `append_rendered` failure | a session statement tail holding a handle or an earlier collection | **fixed** (S5); Struct: refused (`session:collection-result`) |
| `vm_array_mutation.cpp:16` receiver | statement `x.push(v)` on a loop/selector/const binding | **fixed** (S2) |
| `vm_array_mutation.cpp` splice "receiver changed during replacement" | | invariant: the `c=1` second step is emitted only when `late` is true, a compile-time constant false outside mutants R22/R23 |
| `vm_array_mutation.cpp` method default, `vm_keyed.cpp` method out of range, `vm_struct.cpp` opcode, `vm_collection.cpp` opcode, `vm_iter.cpp` for mode / iter opcode | | invariant: operand fields come from the compiler's own tables |
| `vm_array_mutation.cpp`, `vm_keyed.cpp`, `collection_*`, `vm_iter.cpp`, `vm_coro_ops.cpp` "memory budget exceeded" | | not an operand kind: the existing heap-budget stop |
| `vm_keyed.cpp` receiver not a collection | | invariant: `KeyedMutate` is emitted only for a bare binding whose kinds are exactly one of map, set, stack (`keyed_method_call`); 360 probes (wrong collections and non-collections through loops, parameters, map gets) are refused or well-kinded. The VM does not re-check the sub-kind (a B4 admission-interface item) |
| `vm_ops.cpp` arithmetic, compound assignment, ordering, negation, unary plus, index, element assignment, member, length/size | | invariant: each opcode is emitted after `kinds_within` (`type:*` refusals); 203 probes over imprecise operands (args, set/stack/range loops, mixed arrays, parameters, map gets) refused or ordinary |
| `vm_iter.cpp` array element/object key/lookup sources, safe index/key/atom | | invariant: the per-step opcodes read the loop's own snapshot register that `ForPrep` checked (non-iterables are ordinary `@for` errors there; 80 probes) |
| `collection_sort.cpp` sort receiver, `collection_helpers.cpp` collection receiver/method/phase, `vm_collection_sort.cpp` location/controller | | invariant: emitted only after `kinds(receiver) == kKindArray` (`collection_method_call`) and, for a comparator sort, not on a loop or selector binding |
| `natives/array_methods.cpp` (19), `string_methods.cpp` (17), `object_methods.cpp` (9), `number_methods.cpp` (2), `bytes_methods.cpp` (6), `collection_methods.cpp` (7) receivers and arities | | invariant: `bind_method` binds a built-in only when every possible receiver kind matches (else `builtin-receiver:*`, `method:*`, `collection-receiver:*`); bytes are refused entirely (`builtin-bytes`) |
| `format_methods.cpp` presentation receiver (argc 0) | | invariant: the compiler always passes the receiver |
| `format_methods.cpp` `scalar_to_string` receiver (review F1; the first version of this table wrongly said `to_string` binds to proven scalars only) | interpolation of a value whose kinds mix a scalar and a non-scalar: `compiler_interp.cpp` `scalar_only` keeps the scalar bits and binds `to_string` from them | **fixed** (S6); a string receiver stays internal: the lowering appends a string part directly and never calls `to_string` for it |
| `vm_ops.cpp` `add_assign_slow` "compound assignment" | the same narrowing to string (`["s", mutex()]`): the interpolation appends the part with `AddAssign` | **fixed** (S7) for a string target and a non-renderable part; other operand pairs: invariant (`+=` is emitted after `kinds_within`) |
| other kind-narrowing helpers that drop kinds before binding a native | | none found: `scalar_only` is used only by `compiler_interp.cpp` (lines 152, 188, 192, 266), all ending at the two rows above; the other `& ~kKind...` tests (`compiler_assign`, `compiler_call`, `compiler_expr`, `compiler_keyed`, `compiler_stmt_expr`, `compiler_structs`, `builtin_bind`) require the whole kind set to be inside the receiver set, they never drop kinds |
| `format_methods.cpp` `type`/`is_*` argument kind (`public_type_name` null) | | invariant: only host handles and cells lack a public type; host handles in these positions are refused (`host-handle-flow`) |
| `shared_methods.cpp` channel sentinel construction | | not an operand kind: an allocation failure building the Error |
| `host_file_handles_internal.h` `map_handle` default, `host_util.h` HostStatus::Internal | | invariant: wrong kinds are ordinary errors before resolution (`close: expected a file or stream handle`, `recv: expected a channel`: 21 probes); the remaining statuses (forged, foreign, protocol misuse) cannot be produced by a script (CP17 typed handles) |
| `vm_builtin.cpp` `Status::Internal` mapping | | the sink: unchanged, and reached by none of the above |

## 4. Implementation

Production (round 0), 7 files, +60/-3 lines: `natives/format_methods.cpp`,
`natives/json_methods.cpp`, `vm_array_mutation.cpp`,
`compiler_array_mutation.cpp` (the note), `vm_session_render.cpp`,
`eval_result.cpp`, `CLI.cpp`. Mutants are compile-time flags on the touched
object (`NIFT_SAFETY_MUTANT=n`), as the refsem and collections mutants are:

| Mutant | File | Restores |
| --- | --- | --- |
| S1 | `natives/format_methods.cpp` | `Status::Internal` for a presented handle or future |
| S2 | `vm_array_mutation.cpp` | the internal failure for a non-array mutator receiver |
| S3 | `natives/json_methods.cpp` | `Status::Internal` for those kinds in `validate` |
| S4 | `CLI.cpp` | exit 70 for a session value with no shown form |
| S5 | `vm_session_render.cpp` | the internal failure for a handle/collection statement tail |
| S6 | `natives/format_methods.cpp` | `Status::Internal` in `scalar_to_string` (review F1) |
| S7 | `vm_ops.cpp` | the internal failure for a non-renderable part appended to an interpolation |

## 5. Tests (`tests/runtime2/safety/`)

`safety.py` generates every case (no fixture files: nothing is added to the
corpora the classifiers walk) and checks, for every case: no exit 70; no
"internal failure", "compiler defect" or "unexpected operands" text; for an
ordinary error, the earlier output present, one positioned
`error: FILE:LINE:COL:` line (sessions: the session continues); for a
refusal, nothing run and a real position; and the whole transcript equal to
`expected.json`. Cases marked legacy must also equal `NIFT_VM=0` byte for
byte.

`expected.json` is a recorded snapshot (`safety.py --update`), summarised and
spot-checked by its author, not a hand-reviewed expectation per case. What
binds is the runner's invariants (no exit 70, no internal text, the earlier
output, a real position, nothing run on a refusal) and the legacy-matched
cases; the snapshot only makes any later change of a transcript visible.

- Handles: `mutex`, `channel`, `atomic<int>`, `atomic<bool>`; alone, in an
  array, an object, a map and a stack, at depth 1 and 2; `stringify`,
  `prettify`, `highlight`; top level, function, closure, imported module,
  piped `-i` session (540 cases); `validate` per kind and container; `print`,
  `print([h])`, interpolation (`"$[h]"`, `"$[[h].stringify()]"`) in all five
  forms; `json_stringify`; a parameter, a map get, a loop element; session
  bare values and statement tails; `try`/`catch`; host handles, futures and
  threads (refused); controls (function value, a caught recoverable error).
- Mutators: `for (a : args) { a.push(1) }` with 0, 1 and 2 arguments, in a
  function, a closure, nested, in `try`, in a module, in a session; every
  array mutator on a string, number, object and null receiver through a loop
  element, a parameter, a map get and a session loop, and through set,
  stack and `range()` loops; map/set/stack mutators and reads on eight
  receiver kinds through a loop element, a parameter and a map get; a stack
  element `push`; struct field stores on four wrong receivers through a loop,
  a mixed loop, a parameter, a map get and `args`.

- Mixed-kind interpolation (review F1): `"i$[v]"` where `v` is `1` or `"s"`
  first and then a mutex, channel, atomic, map, set, stack, function or
  struct instance, through a loop, a parameter, a closure, `pop`, a
  `map.get`, a piped session, and a struct returned from a module (101 cases).

Totals: 1,163 cases (715 ordinary errors, 420 refusals, 28 run to
completion).

`fuzz.py` (the reviewer's generator, adapted): 16 value kinds x 11 wrappers
(alone, array, object, depth 3, depth 4, a mixed array, map, stack and their
nestings) x 14 delivery routes (direct, parameter, the two mixed parameter
routes, closure, module, map get, `pop`, loop, the two mixed loops, channel,
returned from a branch, assigned in an `if`) x 38 operations (presentation,
`validate`, `print`, interpolation, `type`, equality, container methods, every
mutator, index and member stores, `+=`). Fixed seed 1: every kind x route x
operation with the value alone (8,512), 20,000 sampled programs over the whole
space, 5,000 sampled as piped sessions; about 80 s on this host. It asserts
only the invariants (no exit 70, no internal-failure text) and runs in
`test-runtime2-safety` after `safety.py`.

## 6. Local runs (`89dace52`, tree as committed; through the host's heavy-job gate)

Every step ran alone, in this order, each exit 0:

| Gate | Exit |
| --- | --- |
| `make nift`, `make test-rt-lint` (0 problems) | 0, 0 |
| `test-runtime2-safety` (1,062 cases), `test-runtime2-safety-mutants` (base passes; S1-S5 caught: 532, 23, 23, 27, 6 cases exit 70) | 0, 0 |
| `test-rt-vm` prerequisites, one by one: `test-runtime2-vm-corpus-complete`, `.build/rt/vm-unit`, `vm-globals-unit`, `vm-modules-unit`, `vm-run`, `vm-prepared-parse`, `.build/rt-parse-dump`, `test-runtime2-cp13b`, `test-rt-source-map`, `test-rt-error-oom`, `test-cp25r`, `test-runtime2-cp16-n1`, `-inventory-links`, `-inventory-host-mutant`, `-kind-bits`, `-closures-s1`, `-closures-s1-stress`, `-closures-s1-mutants`, `-cp12c-mutant-controls`, `-for-s2`, `-for-s2-mutants`, `-capture-origins`, `test-rt-vm-policy-tooling`, `-cp14b`, `-cp14b-sort`, `-cp14b-witnesses`, `-d040-witnesses`, `-cp14b-profile`, `-cp22`, `-coro-module`, `-cp17-n2`, `-cp17-tripwire`, `-collections` (273), `-collections-mutants`, `-refsem` (463), `-refsem-mutants` (22 of 22), `-vm-default`, `-args-cmd`, `-packages` (828) | 0 each |
| the 32 `test-rt-vm` recipe lines, one by one (unit binaries, rt-lint, legacy_names, `vm_parity --gen 300 --rt-diff`, adversarial selftest and `--count 600`, subset, subset_gen 500, builtins, host, records_coverage, path_wording, known_refused, engine_selection, former_replay 600, d018, context_consistency, policy_negative, review_cp12, golden, int_bounds, inventory, chain, modules 100, module_host_order, structs 100, globals 200, semantics_modules (both dirs), modules_d041 `--legacy-today`, cp12b_inventory selftest) | 0 each |
| `test-runtime2-args-cmd-mutants`, `-packages-mutants`, `-packages-legacy` (packages: all three lanes), `-cp14b-design-mutants` (1-11 detected; 12 unverified comparator profile, as on Linode), `test-cp19-functional`, `test-cp19-session-cli`, `test-runtime2-cp23` | 0 each |

Classify diff against the `weekend/r2` binary (`33904a6f`, rebuilt: up to
date): `NIFT_VM=classify` over every `.f` in `tests/runtime2` (1,816 units)
and CP25-R `classify-all` over the release corpus (589 units, reasons and
positions compared, temporary paths normalised): **0 changes**. B0 changes no
admission decision. Not run locally: sanitizer/TSan lanes and the Linode
website-build and presentation micro-benchmark (plan §6 B0).

## 7. Review round 1 (review of `8a9dc923`)

Verdict REJECT on one HIGH finding; decisions (a) and (b), S1-S5, the
spot-checked internal returns, the B4 sub-kind note, the classify diff and
the gates held.

**F1 (HIGH): interpolating a mixed-kind value exited 70 after output.**
`print("b")` / `for (v : [1, mutex()]) { print("i$[v]") }` printed `b`,
`i1`, then `unexpected operands ... built-in to_string`. The interpolation
lowering's `scalar_only` drops the non-scalar kinds of a mixed set and binds
`to_string` (or, for a string part, a plain append) from the scalar rest;
`scalar_to_string` and `add_assign_slow` treated the other kinds as a
compiler defect. Routes: a loop over a mixed array, a parameter called with
a scalar then a handle/collection/function/module struct, `pop`, `map.get`,
a closure. Fix (no admission change): both now raise an ordinary error for
a value with no text form (`natives::unrenderable_text`):

| Kind | Text | Source of the wording |
| --- | --- | --- |
| function, handle, future | `opaque runtime value is not serializable` | presentation (functions, B0 handles) |
| map, set, stack | `map, set and stack values cannot be rendered as text` | the B0 session-render text without its `in $[...]` |
| struct instance | `struct instances cannot be rendered as text` | the same pattern |
| array, object | `cannot render a JSON array as text; select an element first` (object: member) | legacy's session wording without its `$[...]` (not reachable today: aggregates are refused, `interpolation:non-scalar`) |
| Error, bytes | `Error values cannot be rendered as text`; `bytes values cannot be rendered as text; decode as UTF-8 first` | the session-render texts without `in $[...]` |

The error is positioned at the interpolated expression; not catchable (an
ordinary error, section 1). Section 3 corrects the `to_string` row and adds
the append row; no other kind-narrowing helper drops kinds before a native.
Fixtures `i_*` (section 5), mutants S6 and S7, and the reviewer's generator
as a regression gate (`fuzz.py`).

Fuzz evidence, seed 1, 33,512 programs: on `8a9dc923` **134** break the
invariants (all file mode, all interpolation: 79 `built-in to_string`, 55
`compound assignment`, through the `loopmix`, `loopmixs`, `param2`, `params`
and `pop` routes); on the fix 0.

Round 1 runs (this commit's tree, through the heavy-job gate), each exit 0:
`make nift`; `make test-rt-lint` (0 problems); `test-runtime2-safety`
(`safety.py` 1,163 cases, then `fuzz.py` 33,512 programs, 0 broken);
`test-runtime2-safety-mutants` (base passes; S1-S7 caught: 532, 23, 23, 27,
6, 54, 46 cases exit 70); `test-runtime2-vm-default`; `test-runtime2-refsem`
(463 checks); `vm_host.py` (160 pass, 5 refused); `vm_builtins.py`;
`test-cp19-session-cli`. The same `fuzz.py` on the `8a9dc923` binary: exit 1,
134 programs broken. Classify diff against the `weekend/r2` binary
(`33904a6f`): 0 of 1,816 `tests/runtime2` units, 0 of 589 CP25-R units.
Round 1 production change: 3 files (`natives/format_methods.{h,cpp}`,
`vm_ops.cpp`), no compiler change.

## 8. F1 follow-up (owner approval of the bounded correction, 2026-11-02)

The review of `8a9dc923` stays a **REJECT** (section 7); `fe2ec7fc` is the
bounded correction, under a narrow independent re-check. This section adds
records and fuzz-runner checks only: no production change.

### Root cause: (ii) unsafe dispatch, not (i) incorrect inference

The inference is right: for `for (v : [1, mutex()]) { print("i$[v]") }` the
binding's kinds are {int, shared handle}. Evidence: the same `v` in every
other operation is refused because of the handle bit (`v + 1`: `type:+`;
`-v`: `type:unary`; `v.length()`: `type:length`; `v.trim()`:
`builtin-receiver:trim`; probe set `k1`), and a `v` that is only a mutex is
refused in the interpolation itself (`interpolation:unknown-type`), so the
handle bit is present where interpolation reads it. The dispatch drops it:
`interpolation_kind` returns `scalar_only(binding->decl_kinds)`
(`compiler_interp.cpp:152`; also `:188`, `:192` for `??` and `?:`, and
`:266` on the part), and `scalar_only` (`:39`) returns `kinds & kKindScalar`,
the scalar bits of a mixed set; `to_string` is bound from that ({int}), or,
for {string}, the part is appended directly. `NIFT_VM_DUMP=1` shows the
emitted `CallNative ... 51` (to_string) on the mixed part. The file's own
contract (its header: "unproven or mixed families use
interpolation:unknown-type") is what the narrowing breaks.

**Still true after B0:** the compiler still selects the scalar path for a
mixed kind set; B0 makes that path safe at run time (an ordinary error,
never Internal) and changes no admission. The structural fix is assigned to
**B4** (operation admission interface): a proven kind compiles to the
specialised operation, an uncertain kind to a checked dynamic operation,
an unsupported one is refused; for interpolation that means a mixed set is
either refused before effects (as the exact non-scalar kind is) or lowered
to a checked conversion. B4 must carry a mutation test for the invariant
**"no native returns Internal because of an imprecise kind"**: a mutant that
narrows a receiver's kinds (as `scalar_only` does) must be caught by the B4
suite, and the B0 fuzz gate (`fuzz.py`) stays in `test-runtime2-safety` as
its run-time backstop.

### Which kinds `$[...]` supports (the VM's contract)

`compiler_interp.cpp`: only an expression whose scalar proof succeeds is
compiled; aggregates are `interpolation:non-scalar`, unproven or mixed
families `interpolation:unknown-type`, host handles `host-handle-flow`. So
the VM renders exactly one scalar family per part: number, string, bool or
null. Per kind (probe set `ik`: `v := K; print("x$[v]")` for the exact kind;
`for (v : [1, K])` and `for (v : ["s", K])` for the mixed one, after this
commit's production tree `fe2ec7fc`):

| Kind | Exact kind on the VM | Mixed with 1 / "s" on the VM | Legacy (`NIFT_VM=0`) | Status |
| --- | --- | --- | --- | --- |
| int, float | rendered | with 1: rendered (`x1`, `x5`); with "s": refused `unknown-type` | rendered | the mixed scalar family is the existing interpolation coverage gap, not B0 |
| string | rendered | with "s": rendered; with 1: refused `unknown-type` | rendered | same gap |
| bool, null | rendered | refused `unknown-type` (both) | rendered | same gap |
| array, object | refused `interpolation:non-scalar` | refused `non-scalar` | error after output (`print: parameter expression must resolve to a scalar value`) | equal unsupported status; never reaches B0's text |
| map, set, stack | refused `unknown-type` | ordinary error `map, set and stack values cannot be rendered as text`, after `x1`/`xs` | renders the tag `\x1fnift:collection:N` | **compatibility gap** (D-063: no VM shown form for a collection) |
| struct instance | refused `unknown-type` | refused `context:struct-new` in this form; through a parameter or a module function ordinary error `struct instances cannot be rendered as text` | renders `\x1fnift:struct:N` | **compatibility gap, assigned to B3** (struct rendering) |
| function | refused `unknown-type` | ordinary error `opaque runtime value is not serializable` | renders `\x1fnift:callable:lambda:N` | **compatibility gap** (an internal tag string) |
| mutex, channel | refused `unknown-type` | ordinary error `opaque runtime value is not serializable` | mutex: `\x1fnift:mutex:N`; channel: not a legacy callable | **compatibility gap** for mutex (CP17 abolished the handle string encoding) |
| atomic | refused `unknown-type` | ordinary error `opaque runtime value is not serializable` | renders the atomic's **value** (`x1`) | **compatibility gap** (legacy reads the value; the VM needs `.load()`) |
| file / stream handle | refused `host-handle-flow` | refused `host-handle-flow` | renders `\x1fnift:file:N` | compatibility gap (CP17) |
| future | refused `coro-future-flow:String` | refused `coro-future-flow:String` | renders `\x1fnift:async:N` | compatibility gap |
| Error | not constructible in an interpolated position on the VM today (`catch` with that body: `opaque:legacy-runtime-error`) | same | legacy errors before (`[].pop()`) | B0's text is defensive only |
| bytes | refused `builtin-bytes` (bytes values are not on the VM) | same | legacy errors before (`encode`) | B0's text is defensive only |

So B0's ordinary errors cover only kinds the VM does not render in any form:
for every kind the exact path renders (number, string, bool, null), the mixed
path renders it too (the `1` or `"s"` element prints before the error), and
no exact-renderable kind ever reaches `unrenderable_text`. Where the exact
kind is refused before effects and the mixed one fails at run time, both
report the same unsupported status; the difference is only *when* it is
established, which is the dispatch issue B4 owns. No legitimate conversion
is rejected, so no production change was needed. The compatibility gaps
above are recorded as gaps, not intended behaviour: structs to B3; the tag
renderings of collections, callables, mutexes, files and futures, and the
atomic's value, have no Phase B checkpoint today and are for the owner to
assign.

### Fuzz gate (owner requirements)

`fuzz.py` now checks, for each of the 33,512 programs: (1) **internal** no
exit 70; (2) **leak** no internal diagnostic in stdout *or* stderr; (3)
**lost** output printed before the failing operation is present (a run that
failed at a line after its `print("b")`, or in a module function called
after it, has printed `b`; a session not refused printed `b` and `after`; a
refused session statement after the print line still shows `b`); (4)
**determinism** every 20th program (1,676) runs twice with identical exit
status, stdout and stderr; (5) **crash** any exit status outside the
documented {0, 1}, a signal, an abort or sanitizer report, and a timeout
(30 s) fail; none counts as a refusal.

`fuzz.py --selftest` (also in `test-runtime2-safety`) makes each check fire
on a crafted fake binary: exit 70; internal text on stdout; an error at a
line after the print with no output; exit 3; SIGSEGV; an AddressSanitizer
report; a 30 s sleep against a 2 s timeout; an alternating output; and a
clean ordinary error fires nothing. Results, seed 1: the fix `fe2ec7fc`:
PASS, 0 violations (1,676 repeated identically); `8a9dc923`: FAIL, 134
violations (each both `internal` and `leak`).

### On `expected.json`

As section 5 says, `expected.json` is a recorded snapshot, not proof of
correctness: it was produced by `safety.py --update` from the candidate
binary. The binding evidence is the runner's invariants, the
legacy-matched cases, the mutants and the fuzz gate.

## 9. Recorded transcript changes

- **C145 (D-106, D-107):** two entries of `expected.json` were updated with `safety.py --update`; no other entry and
  no program changed. The F6 refusal `legacy-method-precedence` (a plain object's `insert` call; legacy: "unknown
  value or malformed expression") replaces the run-time error the VM gave before:

| Case (program unchanged, tests/runtime2/safety/safety.py) | Recorded before (C134 transcript) | Recorded now |
| --- | --- | --- |
| `m_insert_object_loop`: `print("b")` / `xs := [{"k": 1}]` / `for (a : xs) { a.insert(0, 1) }` / `print("after")` | exit 1; stdout `b\n`; stderr `error: p.f:3:16: value has no member: insert` | exit 1; stdout empty; stderr `error: p.f:3:16: not compiled for the VM (legacy-method-precedence): calling an object field whose name legacy reserves as a method is not supported by the Runtime 2.0 VM yet` and the standard notes (the program was not executed; `NIFT_VM=0 nift p.f`; the issues URL) |
| `m_insert_object_mapget`: `print("b")` / `m := map()` / `m.set("k", {"k": 1})` / `a := m.get("k")` / `a.insert(0, 1)` | exit 1; stdout `b\n`; stderr `error: p.f:5:1: value has no member: insert` | exit 1; stdout empty; the same refusal at `p.f:5:1` |

  Details: checkpoints/c145-literal-args.md.
