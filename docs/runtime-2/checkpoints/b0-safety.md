# B0: pre-release safety (cand/b0-safety)

Status: **candidate**, not reviewed, not integrated. Branch `cand/b0-safety`,
built on `weekend/r2` @`33904a6f` (reference semantics Phase A integrated).
Plan: Phase B plan §6 "B0"; owner decisions of 2026-11-02 (B0 ruling). B0 is
standalone: it changes no semantics of an admitted program that ran to
completion, no admission decision, legacy (`NIFT_VM=0`), the Phase A guard or
reference semantics.

Tests: `make test-runtime2-safety` (`tests/runtime2/safety/safety.py`, the
reviewed transcripts in `tests/runtime2/safety/expected.json`),
`make test-runtime2-safety-mutants` (`tests/runtime2/safety/mutants.sh`,
S1-S5). Both are prerequisites of `test-rt-vm`.

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
| Session display of a statement value with no shown form: a bare `m`, `mutex()`, a map from an earlier statement, `v.set("k", mutex())` (CLI `run_vm_session_statement`) | the session stops, exit 70 | the statement's ordinary error, the session continues (`nift eval` already reported it as an error, exit 2) |
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
| `format_methods.cpp` presentation receiver (argc 0), `to_string` receiver | | invariant: the compiler always passes the receiver; `to_string` binds to proven scalars only |
| `format_methods.cpp` `type`/`is_*` argument kind (`public_type_name` null) | | invariant: only host handles and cells lack a public type; host handles in these positions are refused (`host-handle-flow`) |
| `shared_methods.cpp` channel sentinel construction | | not an operand kind: an allocation failure building the Error |
| `host_file_handles_internal.h` `map_handle` default, `host_util.h` HostStatus::Internal | | invariant: wrong kinds are ordinary errors before resolution (`close: expected a file or stream handle`, `recv: expected a channel`: 21 probes); the remaining statuses (forged, foreign, protocol misuse) cannot be produced by a script (CP17 typed handles) |
| `vm_builtin.cpp` `Status::Internal` mapping | | the sink: unchanged, and reached by none of the above |

## 4. Implementation

Production, 7 files, +60/-3 lines: `natives/format_methods.cpp`,
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

## 5. Tests (`tests/runtime2/safety/`)

`safety.py` generates every case (no fixture files: nothing is added to the
corpora the classifiers walk) and checks, for every case: no exit 70; no
"internal failure", "compiler defect" or "unexpected operands" text; for an
ordinary error, the earlier output present, one positioned
`error: FILE:LINE:COL:` line (sessions: the session continues); for a
refusal, nothing run and a real position; and the whole transcript equal to
the reviewed `expected.json`. Cases marked legacy must also equal
`NIFT_VM=0` byte for byte.

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

Totals: 1,062 cases (615 ordinary errors, 419 refusals, 28 run to
completion).
