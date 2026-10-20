# CP12 phase A — Engine selection before execution (bail-and-replay removed, D-018)

Status: see `../progress.md`. Branch `runtime-2-cp12-dev` (local, not
pushed), rebased on `runtime-2` @`b411f3f` (site-dx and serve integrated; earlier bases `8957559`,
`825c90a`, `b61e3a5`; CP11 certified, D-018/D-019
recorded in `../decisions.md`). Phase B
(resolver coverage: globals, `GETDYN`, modules, structs, enums;
checkpoints.md CP12) is next and not started.

Owner decision D-018 (recorded in `../decisions.md` by the coordinator; the
per-case consequences are below): a function has **one** compiled
representation and every expression **one** meaning wherever it appears.
The earlier phase-A design reproduced legacy's two evaluators (a compiled
variant per evaluator, textual re-evaluation blocks, per-evaluator error
sites); that machinery is removed. Where legacy behaves differently by
calling context, either the difference is one of the three approved changes
(D-018a–c), or the unit is refused before execution (`context:*`).

## Objective (owner, D-013 removal gate)

| | Invariant | How it holds | Test |
| --- | --- | --- | --- |
| A | Single execution: every compilation unit executes at most once | `run_script_on_vm` returns "run legacy" only before anything executed; once the VM starts it returns the VM's exit status whatever happens. `NIFT_VM_STATS` reports `vm_executions` and `legacy_executions` (exactly one is 1) | `vm_engine_selection.py` (statistics, every printed line, the host-op trace `NIFT_TRACE_HOST`, an appended file); every harness fails a unit with other counts |
| B | Pre-execution selection | parse, Opaque check, compile and the kind inference decide; `NIFT_VM=classify` (no execution) names the same engine | `vm_engine_selection.py` (classify vs `NIFT_VM=1`), `vm_subset.py` / `vm_subset_gen.py` (unsupported suites) |
| C | No partial replay | there is no replay or re-evaluation code: no `Bail`/`NoAlias`/`CheckParam`/`JmpLarge` opcodes, no `Reeval` stop, no "replayed" outcome | every harness fails on `replayed`; `vm_former_replay.py` aims at every former replay reason |
| D | No duplicated effects | output streams (64 KiB chunks); every statement is evaluated once (D-018a); effectful programs (files, processes, ...) are refused before execution | `vm_engine_selection.py` (file write traced once, no duplicated line, the former re-evaluation call printed once by the VM), all suites compare complete stdout/stderr/exit |
| E | Explicit fallback | unsupported units are classified before execution with a recorded reason (`outcome: fallback`, `reason`, `execute_us: 0`, `vm_executions: 0`) | `vm_subset.py` UNSUPPORTED (incl. every `context:*` reason), `vm_subset_gen.py --suite unsupported`, `vm_engine_selection.py` |
| F | VM errors remain errors | program errors: the VM reports them itself (legacy's format, the failing expression's position, exit 1); unexpected failures: `NIFT_VM internal failure`, exit 70, never run on legacy | `vm_engine_selection.py` (`internal-failure`), unexpected operand kinds are internal failures (compiler defects), never guessed |

## Design

### One compiled representation (D-018)

* **Compiler.** One proto per function (`protos[f + 1]`), whatever calls it.
  Every user call passes the P5 text types of the parameters the callee can
  observe (call-graph fixpoint); every assignment uses the canonical
  text-and-value binding check (`CheckAssign`); errors are reported at the
  failing expression's start (`site(node) = node.begin`; statement-level
  errors at the statement). Removed: function variants, textual
  re-evaluation copies and their cold-block layout, `if`/`return`/index/
  caller error sites, `return: ` prefixes, the print-path re-resolution.
* **VM.** One semantics: CP09 canonical arithmetic (checked int64, exact
  beyond 2^53, `signed 64-bit integer overflow`, doubles otherwise) and the
  canonical messages (D-009#1) in every context. Removed: the prepared
  double arithmetic, `-0` negation, prepared messages (`invalid index`,
  `cannot change binding type: x`, `invalid index target`), `length()` on
  objects, `StopReason::Reeval`, `JmpLarge`, caller-site offsets, prefix
  counting.
* **Evidence only.** The compiler still knows which code legacy would run on
  its loop (prepared) evaluator (loop regions; functions called from them;
  `rt/prepared.h`). It uses this for two things: the context refusals below,
  and an instruction flag `kInstrLoopPath` that never changes what the VM
  computes. The VM statistics report `large_numbers` (arithmetic that met a
  number legacy calls large, |n| ≥ 2^53, as an operand or a result, or went
  out of range; the integer fast paths take only safe operands so none is
  missed), `large_in_loop_path` (of those, in code legacy runs on its loop
  evaluator: the flag on the instruction or on any active call),
  `large_lines` / `derived_lines` (per such loop-code operation, the index
  of the output line printed next; `derived_lines` when an operand was
  large) and `error_in_loop_path`. The harness policies require this
  evidence, line by line (review round 1).

### D-018 consequences

Every legacy behaviour that depends on the calling context, its single VM
semantics, and how the difference is handled. "Loop code" = code legacy runs
on its prepared evaluator (loop conditions and bodies, functions called from
them, transitively), except statements it runs textually.

| # | Legacy behaviour by context | Intended consistent semantics (VM) | Handling |
| --- | --- | --- | --- |
| 1 | Numbers in loop code are doubles: `print(w + 2)` with `w = 2^53 − 1` prints `9007199254740992` in a loop, `9007199254740993` at top level; a product overflowing int64 prints `6.4e+19` in a loop-called function and fails `signed 64-bit integer overflow` when called directly | CP09 everywhere (exact integers, overflow error) | **approved D-018a**; policy `D-018a numbers` (evidence `large_in_loop_path`) |
| 2 | A loop declaration/assignment whose value is large, or a compound assignment/increment with a large operand, is re-evaluated textually: its calls run twice (output twice) | evaluated once | **approved D-018a** (same policy: legacy's extra lines repeat lines also printed once) |
| 3 | `print(path)` with a missing element in loop code resolves the path again (keys evaluated twice) | keys evaluated once | **approved D-018b**; not observable in the compiled subset (keys with calls make legacy run the statement textually; the re-resolution's message is the canonical one), so no harness policy: such programs must be identical (`vm_d018.py` print-path cases) |
| 4 | Error positions: textual code reports the enclosing `if`, the `return` statement (plus a `return: ` prefix per `return` the error passes through) or the index expression; loop code reports the failing node with its parentheses, or the statement / outermost loop statement / caller's call site for statement errors | the failing expression (statement errors: the statement), no prefix | **approved D-018c** (D-009#5); policy `D-018c diagnostics` |
| 5 | Error wording in loop code: `invalid index`, `cannot change binding type: x`, `invalid index target` | canonical: `JSON array index 3 is out of range in 'a[k]'`, `JSON object has no key 'k'`, `cannot assign string to int binding 'x'`, `index out of range: 3` | **approved D-018c** (D-009#1); mapped only with evidence `error_in_loop_path` |
| 6 | `x = e`: textual code checks the type derived from the text and value, loop code the value's type (P4): `x = x + 0.5` on an int `x` fails differently; a double-typed binding holding a string (`b := "" + 1.5`) accepts `b = b` only textually | canonical text-and-value check | **refused** in loop code where the verdicts can differ: `context:assignment-type` (the text may give a double type while the value may be integral or not a number; exempt: numeric values assigned to an always-double binding, or `x = x` with a numeric value) |
| 7 | Parameters: a textual call types a parameter from the argument's text and value, a loop call from the value only (P5) | canonical (text and value) | **refused**: `context:parameter-type` (a loop call to a function that observes that parameter's type, with an argument that may differ) |
| 8 | `o.length()` on an object counts members in loop code, fails at top level | canonical error (`size()` counts) | **refused**: `context:object-length` (receiver may be an object, loop code) |
| 9 | `x %= y` in loop code is `fmod` on any numbers (no integer check, no finiteness check) | canonical `modulo requires integer-valued operands` | **refused**: `context:modulo-assignment` (an operand may be a fraction, loop code) |
| 10 | `s += t` on strings in loop code appends without the binding type check (a double-typed binding holding a string keeps it) | canonical check after every compound assignment | **refused**: `context:string-append-type` (target may be double-typed, loop code) |
| 11 | `-x` with integer `x == 0` gives `-0` in loop code, `0` at top level | `0` (CP09 negate) | **refused**: `context:negative-zero` (operand may be an integer, loop code; non-zero literals are constants) |
| 12 | Declarations in loop code typed from the value (not the text), aggregates rendered as JSON in `+` in loop code, loop bodies legacy abandons preparing, functions legacy cannot prepare | — | **refused** (unchanged from earlier phase A): `prepared-decl-type`, `type:+`, `prepared-unpreparable`, `prepared-fn-unprepared` |
| 13 | Recursion: legacy stops at 64 frames | 100,000-frame budget | approved D-001#4 (unchanged); policy `D-001#4 recursion` |

Rules 6–11 are the "refuse before execution" part of D-018: the excluded
behaviour is recorded above, the intended semantics is the canonical one
(already the VM's), and phase B or a later language checkpoint can admit
these units once legacy is no longer the oracle in loop code.

### Approved-difference policies (harnesses)

All differential harnesses (`vm_adversarial.py`, `vm_parity.py`,
`vm_engine_selection.py`, `vm_former_replay.py`) classify a legacy/VM pair
with `tests/runtime2/vm_policies.py`; rt-diff uses the same functions as
class rules (`tools/rt-diff/policies/cp12-vm.json`) and `vm_parity.py`
confirms each class approval against the VM statistics. A pair is identical
or matches exactly one named policy; anything else fails.

Review round 2 (the VM must never vouch for its own output): no policy
reads the VM's statistics as evidence; what a policy approves is checked
against legacy and against the program itself.

| Policy | Shape approved | Independent check |
| --- | --- | --- |
| `D-001#4 recursion` | legacy fails only with `callable recursion depth exceeded`; its output a prefix of the VM's; the VM completed or stopped with a program error | legacy's output and error |
| `D-018c diagnostics` | same stdout, both exit 1 with one diagnostic on the same file; messages equal once legacy's `return: ` prefixes are removed, or legacy's loop wording `invalid index` / `invalid index target` with the canonical message | the VM position equals the failing node derived from the CP10 AST (`tests/runtime2/vm_sites.py`: the unique node that can raise the message within the statement legacy reports — or, where legacy reports a loop statement or a call site, within the program — with constant operands evaluated exactly in Python to find which operation fails); every field legacy's wording omits (index, key) equals the constant the source gives at that node, otherwise not approved |

There is **no D-018a (numbers) policy and no deleted-line rule** any more.
Generated programs keep values within ±2^53 where legacy computes in
doubles (generator constants narrowed; the former-replay generator no
longer emits large numbers in loop-evaluator code, recursion beyond 64
frames, or loop index errors whose index the source does not fix). The
real programs that need D-018a / D-001#4 are pinned with results computed
independently of the VM (`tests/runtime2/vm_golden.py`, 108 programs from
the former-replay suite; the reviewer's D4 shapes in `vm_review_cp12.py`;
`vm_d018.py` and `vm_engine_selection.py` pinned cases). Loop-code
assignments whose binding-type check could fail are refused at compile time
(`context:assignment-error`), so legacy's loop wording "cannot change binding
type" is never approved. Exact integer arithmetic at the bounds is checked
for every int opcode against Python ints (`vm_int_bounds.py`, 2,682
programs). `vm_policy_negative.py`: the reviewer's mask.py and mask2.py
synthetic bugs (52) are rejected, 5 genuine shapes approved. The live
mutation gate (`make test-rt-vm-mutants`, see Reviews round 2) is the
acceptance criterion.

`tests/runtime2/vm_d018.py` runs each approved change on legacy (old) and
the VM (new) side by side — 13 live cases, including the same function
called directly where legacy and VM agree — and 18 boundary cases showing
each policy refusing the same difference without its evidence or outside
its shape (different stdout, other message, other binding name, earlier
position, missing evidence, extra VM line, non-repeated legacy line,
fallback outcome).

### Context consistency (owner requirement 5)

`tests/runtime2/vm_context_consistency.py`: 18 function bodies (arithmetic,
large numbers, indexes, `if`, assignment, division by zero, members, string
append, `length()`, negation, `%=`, helper calls, loops, arrays, recursion,
output, nested errors) × 7 arguments (int, fraction, string, array, 2^53−1,
negative), each called from 5 contexts: directly, inside a loop, from
another function, from another function inside a loop, in a loop condition.
Wherever the unit runs on the VM, stdout, error (message and position) and
exit status must be identical across contexts. Result: see Tests. The suite
found one compiler defect (a fused `if`/`while` comparison skipped the
ordering kind check; fixed).

### Kind inference (`rt/infer.h`, `rt/infer_types.*`)

Flow-insensitive fixpoint over the closed unit, a worklist over the call
graph (a unit is walked again only when its bindings, a parameter or a
callee's return grew; review round 1, D1): structural types (kinds of a
value; element type of arrays, positional types of literal arrays never
changed in place, member types of objects by key; depth-capped at 6),
bindings resolved with the compiler's scope rules, parameters as the union of
their arguments, returns as the union of return expressions (plus null when
the end is reachable). If the fixpoint exceeds its budget (64 walks per
unit + 1024) the unit is refused (`analysis-limit:kinds`); it is never
compiled with weakened checks. Something the compiler refuses anyway is
`kKindUnknown`, which every check treats as "may be anything" and refuses;
a probe compilation (discarded) only recovers the underlying refusal
reason for the statistics. The D-018 refusals add two
name-level analyses per scope (`compiler_unit.cpp`): bindings whose type
may be double (declared from a double-typed text or a value that may be a
fraction, parameters receiving such arguments; fixpoint over the call graph)
and bindings whose type is always double.

### Runner and statistics

`NIFT_VM=1`: compile; if refused, legacy runs the unit (it was never
touched); otherwise the VM runs it to completion, a program error (exit 1) or
an internal failure (exit 70). `NIFT_VM_FLUSH` (test-only) lowers the
streaming threshold. `NIFT_VM_STATS` outcomes: `vm`, `vm-error` (reason = the
message), `fallback`, `require-error`, `internal-error`, `compiled`
(classify); fields `vm_executions`, `legacy_executions`, `large_numbers`,
`large_in_loop_path`, `error_in_loop_path`, `large_lines`, `derived_lines`,
`evidence_truncated`.

## Refused constructs and old behaviour

Recorded for later language checkpoints (D-015); none is changed in legacy.
The `context:*` reasons are in "D-018 consequences" above.

| Reason | Construct | Old legacy behaviour |
| --- | --- | --- |
| `type:*` | an operation whose operand kinds may include one legacy rejects | errors; concatenation with an aggregate succeeds only in loop bodies (multi-line JSON) |
| `aliasing` | aggregate bound/returned/pushed/listed from a place | location reference |
| `path-in-call-argument` | a member or index read inside a user-call argument, except an argument that is itself a path with certainly-scalar kinds (stage 4, below) | legacy resolves such arguments as locations: an aggregate is shared with the callee; errors quote the raw argument text |
| `path-in-index` | a member or index read inside index brackets (`a[b[i]]`) | a failing inner index is reported as `unknown value or malformed expression: a[k` |
| `prepared-unpreparable` | an `if`/`while` condition or `return` expression legacy's prepared parser rejects, in loop code | legacy abandons preparing the loop body |
| `param-type-unpredicted` | (defensive) a parameter type read the analysis did not predict | — |
| `context:assignment-error` | (review round 2) an assignment or compound assignment in loop-evaluator code whose binding-type check could fail (the value's kinds include one the declaration's binding type does not accept) | legacy reports "cannot change binding type: x" at the statement, outermost loop statement or call site; canonical "cannot assign T to U binding 'x'" at the assignment |
| `analysis-limit:kinds` | (defensive) the kind inference did not converge within its budget (review round 1, D1) | — |

### Stage 4 (integration, records20k): scalar path arguments

Coordinator ruling, VM subset; no language change. A user-call argument
that is itself a member or index path (`pad(records[k].name, 10)`) is
compiled when CP12 inference proves its kinds certainly scalar: not empty,
no array or object, not Unknown (`element_of`, `src/rt/infer_types.cpp`,
from the Member/Index cases of `Inference::expr`, `src/rt/infer.cpp`). For
such a value legacy's location binding is observably a copy (probes: writes
and rebinding of the root do not reach the caller; the binding type is the
value's; the path is evaluated once, arguments left to right). Everything
else stays refused: aggregate or uncertain kinds, a path inside an
expression, in parentheses or in index brackets, and built-in and method
arguments keep their rules. A failing admitted path uses legacy's location
wording, quoting the whole argument text at its start (`JSON value 'a[i].k'
has no member 'k'`; index messages quote the same text); member on a
non-object and index on a non-container cannot occur (refused). The
existing wording elsewhere is unchanged (`print(o.zz)`: `value has no
member: zz`). The admission assumes that no write outside the unit changes
an element's kind; a unit with import or export is refused as a whole, and
this must still hold when live exports arrive (CP12B, D-046). Pins:
`tests/runtime2/vm_records_coverage.py` (PATH_ARGUMENTS, the refused
`path-*` cases) and mutants 10 (admission whatever the kinds) and 11 (the
value wording) in `vm_mutation_gate.py`.

Second ruling (coordinator, VM subset; no language change): the parameter
copy `out := p` in a prepared function (`pad`'s `out := s`) is also
admitted when bindings named `p` exist outside the function, provided every
one is certainly int typed: int text, or untyped text whose inferred kinds
are certainly int (`src/rt/prepared_outer.cpp`). Legacy types the copy when
preparing, from the binding `p` its scope lookup finds; probes show an
int-typed binding there types it from the value, like no binding, while a
string or double one wins whatever its origin (`s := v[0]` holding `"x"`
included). An integral number is int for legacy's value typing (`1e2`,
`2.0` held in an untyped binding); a `1e2` literal declaration is double
text. A string, double or not-certainly-int outer binding, or a parameter of
another function with that name, stays refused (`prepared-decl-type`); the
per-call condition (untyped or int argument text) is unchanged. The kinds
are now inferred before the prepared analysis (refusal order unchanged;
without converged kinds only an absent binding qualifies). Pins: the 42
outer-binding programs in `vm_records_coverage.py` and mutant 12 (a double
outer binding admitted: silent output divergence, rejected). records20k
(`records` at n = 5,000, 10,000, 20,000) now runs entirely on the VM.

## Tests

Final build `aeefaa7`, idle host (2 vCPU). Logs: `../evidence/cp12/`.

| Suite | Result |
| --- | --- |
| `make test-rt-vm` (unit 40,261 checks; prepared-parser differential; rt-lint; name table; VM corpus 78 + 300 generated + 300 mutants; rt-diff over the shared corpora on both engines, 813 entries; adversarial gate seed 1 at 600, 21 families; subset suites; both generated suites at 500; engine selection; former replay at 600/family; D-018; context consistency) | **PASS**: rt-diff 0 differing, 8 approved (3 D-001#4, 2 D-018c, 1 D-018c loop, each confirmed by VM statistics; 2 exact D-001#4 rules); adversarial 37,421 programs, **0 non-approved differences** (69 D-001#4, 1,636 D-018c); former replay 1,796 programs, 0 replayed, 0 failures (33 D-001#4, 66 D-018a, 321 D-018c); supported generator 500/500 on the VM; unsupported 500/500 refused before execution |
| `make test-rt-vm-sanitize` (ASan+UBSan, leaks on) | **clean**: same suites on the sanitized build (adversarial sampled 3,619, former replay 80/family) |
| `vm_d018.py` | 13 live old-vs-new cases + 18 policy boundary cases: PASS (`../evidence/cp12/d018-old-vs-new.txt`) |
| `vm_context_consistency.py` | 126 (body, argument) pairs × 5 contexts: VM results identical across contexts in every case; 68 pairs ran on the VM in all 5 contexts; **legacy itself is context-dependent on 63 pairs** |
| extra adversarial seeds 2 and 3 (count 200) | 32,221 programs each, 0 non-approved differences |
| `make test-rt-value`, `make test-rt-parser`, rt-lint, `legacy_names.py --check`, `certification_guard.sh` | PASS |

Review round 2 (final build): `test-rt-vm` PASS (rt-diff 813 entries, 0
differing, 7 approved; adversarial 37,421 programs, 0 non-approved, 99
D-018c + 69 D-001#4; former replay 1,645 programs, 292 D-018c; supported
generator 500/500 on the VM; golden 108; int bounds 2,682; policy negatives
52/5; review reproducers 87), `test-rt-vm-mutants` PASS (8/8 rejected),
`test-rt-vm-sanitize` clean (ASan+UBSan, incl. golden and int bounds), perf
guards (`test-load-sensitive`) PASS, callgrind hot paths unchanged (flat call
425, fib 285/call, loop 109), rt-lint and certification guard PASS. Extra seeds: adversarial 2,
3, 4, former replay 2, 3, supported/unsupported generators 1-4 at 500: 0
failures.

Review round 1 (final build, `make -j2`): `test-rt-vm` PASS (unit 40,303
checks; rt-diff 813 entries, 0 differing, 8 approved, each confirmed from
the entry's own statistics; adversarial 37,421 programs, 0 non-approved,
1,636 D-018c + 69 D-001#4, 14,654 on the VM; former replay 66 D-018a + 321
D-018c + 33 D-001#4, 1,392 on the VM; D-018 13 live + 22 boundary cases;
policy negatives 35 rejected / 10 approved; review reproducers 87/87),
`test-rt-vm-sanitize` clean (ASan+UBSan, leaks on; incl. the D2 bounds cases and the 87 reproducers), `test-rt-value`, `test-rt-parser`,
`test-rt-lint`, certification guard, rt-diff self-test: PASS. Coverage is
unchanged by the review fixes.

Defects found during the D-018 rework (fixed, with unit cases): a fused
`if`/`while` ordering comparison skipped the kind check (internal failure
on `"s" > 1`; context-consistency suite); the assignment exemptions of
`context:assignment-type` applied to a double-typed binding holding a
string (adversarial gate, typing/context families).

## Coverage

Coverage is measured, not a threshold (D-016). "Before" = phase A with the
dual compilation (`32198fe`, full run before the owner decision); "after" =
D-018 (`aeefaa7`). Same seeds and suites.

| Measure | CP11 (round 3) | CP12 A before D-018 | **CP12 A after D-018** |
| --- | --- | --- | --- |
| Shared corpora (rt-diff script entries, 786) | 81 VM + 44 replayed | 104 VM (81 vm + 23 vm-error) | **103 VM** (81 vm + 22 vm-error; 1 `context:assignment-type`) |
| General generator (seed 1, 300) | 22.7 % VM + 8.0 % replayed | 72 VM (24.0 %) | **72 VM (24.0 %)** |
| Adversarial gate (37,421 programs) | — | 15,180 on the VM | **14,654** on the VM (−526: `context:*` refusals) |
| Former-replay suite (1,796) | — (replayed) | 1,459 (81.2 %) | **1,392 (77.5 %)** |
| VM corpus (78) | 16 vm, 24 replayed | 16 vm, 11 vm-error | 16 vm, 10 vm-error |
| Supported-subset generator | 100 % | 100 % | 100 % |

## Performance

`benchmarks/runtime2/vm_compare.py --runs 5 --slow` at `aeefaa7`
(`../evidence/cp12/vm_compare.jsonl`); the CP11 binary (`bf32e4f`) re-run
on the same host right after (`../evidence/cp12/vm_compare-cp11.jsonl`).
Instruction counts are exact; wall times are medians of 5 (host idle but
load average decaying from the sanitizer run at the start). Running table:
`../scoreboard.md`.

| Measure | v4.12 | CP11 | **CP12 A (D-018)** | CPython 3.13 |
| --- | ---: | ---: | ---: | ---: |
| flat call (marginal instr; gate ≤ 2,994 and ≤ 2× CPython) | 14,994 | 461 | **426** (35× fewer than v4.12; 0.45× CPython) — **gate met** | 952 |
| loop iteration | 3,066 | 98 | **107** (+9: the binding type check after `i += 1` is now always emitted) | 387 |
| fib, per recursive call (top level / in a loop) | 184,439 / 14,766 | 471 / 471 | **287 / 287** (one proto; no per-call parameter type work when unobserved) | 615 / 518 |
| string append | 9,385 | 3,257 | 3,248 | 1,787 |
| fib(22) / fib(25), ms | 1,368 / 6,034 | 8.7 / 16.8 | **6.9 / 11.3** | 19.2 / 24.8 |
| 1M loop / 200k calls, ms | 553 / 354 | 28.0 / 13.5 | 30.4 / 15.0 | 92.7 / 41.6 |
| compile: small script (instr, µs) | — | 78,331, 34 | 93,796, 55 | — |
| compile: 500-function program (instr, µs; per function) | — | 29.9 M, 3,337 (6.7 µs) | **63.2 M, 7,163 (14.3 µs)** | — |
| cold start (trivial script), ms | 4.6 | 4.6 | 4.5 | 21.4 |
| peak RSS fib(22), KiB | 7,480 | 5,884 | 5,704 | 8,664 |
| end-to-end `records20k`, ms (VM outcome) | 1,390 | 1,324 (fallback) | 1,284 (fallback: `prepared-fn-unprepared`) | 44.9 |

Compile time doubled on the large program: kind inference (~10 M instr),
the unit analyses including the D-018 may-double fixpoint (~18 M),
preparability and place-call checks (~20 M). It stays at 14 µs per function;
no optimisation was attempted (owner priority: replay removal and coverage).

## Risks

* **Coverage moved to refusals.** The context refusals exclude loop code
  whose legacy meaning differs from the canonical one; they are
  conservative (kind- and name-level), so some units that would behave
  identically are refused. Measured below.
* **Approved differences are checked independently of the VM** (review
  round 2): D-018c against the CP10 AST and exact evaluation, D-001#4
  against legacy; D-018a only as pinned results computed in Python; the
  live mutation gate is the acceptance test.
* **D-018a consequences on control flow.** A loop branch taken on a value
  derived from a large operand can differ from legacy; no policy approves
  such a divergence (it fails a gate, visibly). Static refusal would need a
  magnitude analysis.
* **Diagnostics follow the canonical rules** (D-018c); full error parity is
  CP13.
* **Equality depth**: legacy crashes on very deep values where the VM
  answers.

## Reviews

### Round 1 (adversarial review of phase A after D-018)

Reviewer material: `nc2.py`, `nc3.py`, `edge.py`, `edge2.py`, `edge3.py`,
`big.py` ported as `tests/runtime2/vm_review_cp12.py` (87 programs, in
`test-rt-vm` and the sanitized run); `mask.py` and `strict.py` as
`tests/runtime2/vm_policy_negative.py`. Final state: every reproducer is
identical, refused before execution, or approved with per-line evidence;
none crashes, fails internally or differs unapproved.

| Finding | Fix | Test |
| --- | --- | --- |
| **D1** (high, silent miscompile): the kind inference gave up after 200 passes and marked every node Unknown, which passed `kinds_within` / `no_alias` / `type_mismatch_possible` and the operator checks: a 201-function call chain (even never called) switched the aliasing, type and context refusals off | Worklist fixpoint over the call graph (a unit is re-walked only when its bindings, a parameter or a callee's return grew); out of budget (64 walks/unit + 1024) the unit is refused, `analysis-limit:kinds`; Unknown is never permissive (widened to every kind, every check refuses on it; a discarded probe compilation only recovers the underlying refusal reason). 500-function chain: compile 183.5M → 22.4M instructions (inference 163M → 3.3M) | `vm_unit` `check_kind_inference` (chains of 199/201/500: aliasing, `context:negative-zero`, `type:<`, `context:assignment-type` refused; walk bound; refusal at a lowered budget); `vm_review_cp12.py` D1 (14 programs). Adversarial coverage unchanged (14,654 on the VM) |
| **D2** (high, invariant F; also in CP11): `INT64_MIN % -1` raised SIGFPE in the `Mod` fast path (`%`, `%=`, calls, loops) | `r == -1` takes the slow path (result 0, as legacy and CP09). Audit: `AddI` computed `x + offset` before its range check (signed-overflow UB at the int64 bounds), now checks `x` first; division is double with a zero check; negation of INT64_MIN is exact (Decimal); Add/Sub/Mul use overflow builtins. Separate commit `f580d08`; CP11 backport on local branch `runtime-2-d2-int64` (`09ee120`, latent there: operands beyond 2^53 bail before either path) | `vm_unit` `check_integer_bounds` (also under ASan+UBSan; without the fix vm-unit dies with SIGFPE); `vm_review_cp12.py` D2 (13) |
| **D3**: `-INT64_MIN` is an exact Decimal; print and concatenation stopped with an internal failure (exit 70) | `append_rendered` writes the Decimal's spelling (legacy prints `9223372036854775808`); indexing with it is out of range with legacy's messages | `vm_unit` cases; `vm_review_cp12.py` D3 (22) |
| **D4**: evidence only for large *results*: `print(big - big2)` (legacy 0, VM 1) and `m == m + 0` (false/true) in loop code were unapproved, and nothing tied evidence to lines | Large operands are recorded too: the first large number switches the dispatch to a checked table (integer arithmetic on slow paths; no cost without large numbers). Evidence per output line (`large_lines`, `derived_lines`). Static refusal was considered and not done: no magnitude analysis exists, and a conservative one would refuse every loop accumulator; per-line attribution is the fallback the review allows. Small-number or boolean consequences are approved only on a line with an attributed large operand; a branch that diverges on such a value is an unapproved difference (visible, never silent) | `vm_review_cp12.py` D4 (51); `vm_policy_negative.py` |
| **Policy breadth** (main risk): D-018a/D-018c approved real bugs whenever any evidence existed | Policies narrowed to per-line / per-error evidence (see "Approved-difference policies"). mask.py: 13/13 rejected before even with minimal evidence; the permanent suite gives each synthetic bug the evidence a genuine run carries: 35/35 rejected, 10 genuine shapes approved. Genuine approvals unchanged: former replay 66 D-018a + 321 D-018c + 33 D-001#4, adversarial 1,636 D-018c + 69 D-001#4, rt-diff 8 | `vm_policy_negative.py`, `vm_d018.py` (22 boundary cases) |
| **R2**: `vm_parity.py` matched rt-diff evidence by file base name (12 corpus scripts are `main.f`) | `rt-diff --b-vm-stats` collects each entry's own VM statistics and program; the D-018 class rules classify with them through `vm_policies.classify` (no assumed evidence); `vm_parity.py` confirms approvals from the entry's own record | `make test-rt-vm` rt-diff: 0 differing, 8 approved, each confirmed |
| **Compile cost** | `infer_text_type` memoised per node (7.69M → 3.14M via the compiler; 2.76M remain in `typed_names`' raw-text types); `collect_assigned` once per function body (main reuses the function sets). 500-function program: compile_unit 63.24M → 58.45M instructions (−7.6 %). Remaining: UnitCompiler analyses 17.2M (`compute_may_double` 7.3M), `infer_unit_kinds` 10.1M, `check_prepared_contexts` 8.0M, code generation ~20M | callgrind (`vm_compare.py --groups phases`) |

Hot paths (callgrind marginal instructions, exact): flat call 425 (426
before; gate kept), fib 285 per call (287), loop 109 (107: the D2 `AddI`
check), add_local 183 (180), if_branch 244 (236). Not material: no
scoreboard row; wall times were not re-measured (host not idle: another
agent's test runs).

### Round 2 (verification of round 1)

D1, D2, D3, D4 and R2 were closed. The policy narrowing was not: the VM
produced its own evidence, so a bug in an operation created the evidence
that approved its wrong output (`mask2.py`: 11 of 11 synthetic bugs
approved).

| Finding | Fix | Test |
| --- | --- | --- |
| D-018a self-attestation: `arith_slow` marked any large or failed result as evidence, so a wrong large value (`x 4` → `x 1152921504606846976`, `2` → `1e+300`) or a spurious overflow in small-number loop code approved itself, the latter dropping legacy's remaining output | **D-018a policy removed** from every differential gate. Generators keep values within ±2^53 where legacy computes in doubles (adversarial constants `1000000`/`1e15` → `1000`/`1e5`, general generator `123456789`/`4000000000` → `12345`/`40000`; the former-replay generator emits no large numbers in loop-evaluator code). The programs that genuinely need D-018a are pinned with independently computed results (below) | `vm_golden.py`, `vm_int_bounds.py`, mutants 2 and 3 |
| `derived_lines` per output line: every small value or boolean on such a line could change (`0 7` → `0 13`) | removed with D-018a | `vm_policy_negative.py` (mask2) |
| Deleted-line rule approved a dropped iteration that repeats its neighbour | removed; exactly-once evaluation is pinned (`vm_d018.py`, `vm_engine_selection.py` exact VM results) | mutant 4 |
| D-018c site check: an operator anywhere in the rest of the expression (`4 / 2 + 1 / z` at column 6 approved) | the VM position must equal the failing node derived independently from the CP10 AST (`vm_sites.py`): the unique candidate within legacy's reported statement (or the program for loop/call-site reports), constant operands evaluated exactly in Python to find the failing operation; no longer any "legacy position equals VM position" shortcut | mask2, mutant 5 |
| D-018c mappings did not check content (wrong index number; "cannot assign bool to array binding") | `invalid index` / `invalid index target` approved only when every omitted field equals the constant the source gives at that node (else not approved: those programs are pinned or not generated); legacy's binding wording is never mapped: such loop code is refused, `context:assignment-error` (inference made precise enough that the supported subset stays 100 % on the VM: literal arithmetic folded, a fraction ± or ÷ an integer stays a fraction) | mask2, mutants 6 and 7 |

**Independent oracles.** `vm_golden.py`: the 108 former-replay programs (seed 1) that needed D-018a (65), D-001#4 (33) or an unverifiable loop index (10), each with a stated reason; expected stdout from Python ints (W = 2^53 − 1, 2000000000², ...), error positions from the CP10 AST with exact evaluation or a written hand derivation, recursion results from the recursion's definition — legacy and the VM are not consulted. `vm_int_bounds.py`: 2,682 programs, every int opcode (+, −, *, %, unary −, `+=` `-=` `*=` `%=`, `+= k`, `++`, `--`) at 0, ±1, ±3, ±7, 1000, 32767, ±(2^53−1), ±2^53, 2^53+1, 2^62, 2^63−1, −2^63+1, −2^63, with and without the checked dispatch, against Python ints. The reviewer's D4 shapes (9) and the D-018 live cases are pinned the same way.

**Mutation gate** (`make test-rt-vm-mutants`, in `make test`, hence certify): one extra link of nift with `-DNIFT_VM_MUTANTS` (the mutant code exists only there; `src/rt/vm_mutants.h`), mutant chosen by `NIFT_VM_MUTANT`. Suites per run: int bounds, golden, review reproducers, D-018 cases, supported subset (100), vm_parity (corpus, 100 generated, rt-diff over the shared corpora), adversarial (40/120 per family), former replay (60/family). Unmutated: every suite passes. Result (`make test-rt-vm-mutants`, with rt-diff): unmutated passes every suite; all 8 mutants rejected —

| Mutant | Rejected by |
| --- | --- |
| 1 Sub → Add in the checked table | int bounds, golden, review, former replay |
| 2 large slow-path result off by 1000 | int bounds, golden, review, D-018, parity, adversarial, former replay |
| 3 spurious AddI overflow (small operands) | int bounds, golden, supported, parity, adversarial, former replay |
| 4 dropped loop iteration | int bounds, golden, D-018, supported, parity, adversarial, former replay |
| 5 wrong error column | int bounds, golden, review, D-018, parity, adversarial, former replay |
| 6 wrong index number in the message | golden, D-018, parity, adversarial, former replay |
| 7 wrong kinds in the assignment message | D-018, parity, adversarial, former replay |
| 8 Mod fast-path sign | int bounds, supported, parity, adversarial |

Approvals before → after (seed 1, the gate sizes): former replay 66 D-018a + 33 D-001#4 + 321 D-018c → 0 D-018a (65 pinned, 1 now refused) + 0 D-001#4 in the generated suite (33 pinned) + 292 D-018c (1,645 generated programs; 1,185 on the VM); adversarial 69 D-001#4 + 1,636 D-018c → 69 D-001#4 + 99 D-018c (12,918 on the VM, was 14,654: ≈1,500 loop binding-wording cases are now refused before execution, `context:assignment-error`); rt-diff 8 → 7 (3 D-001#4 class, 2 D-018c class, 2 exact D-001#4 rules; shared corpora 101 of 786 on the VM, was 103). Remaining policy approvals: D-001#4 recursion (legacy is the oracle up to its 64 frames) and D-018c diagnostics checked against the AST.

### Certify follow-up: FLAKY `test-rt-vm-mutants`

Certify @06b6617 (concurrent run) rejected the unmutated binary in the
gate's `parity` step although rt-diff reported 0 differing, 7 approved;
the exclusive rerun passed. Cause (reproduced by removing the file during
a run): `vm_parity.py` wrote `.build/rt-vm/stats-path` before rt-diff and
removed it afterwards; certify runs `test-rt-vm` and `test-rt-vm-mutants`
concurrently, both `vm_parity` runs shared that file, and the one finishing
second failed with `FileNotFoundError` after rt-diff had passed. CPU load
alone (busy loops, three runs) did not reproduce it. Harness-only: the file
is no longer used (rt-diff `--b-vm-stats` already supplies each entry's
statistics); what a VM difference fails on is unchanged. The gate now keeps
every suite's full output (`--logs`) and prints the tail when the unmutated
binary is rejected. Verified: both targets concurrently with a busy loop,
and `test-rt-vm-mutants` exclusively (see the record of the final runs).

## Commits

`968bcf0` remove bail-and-replay; `5bc14be` invariant and former-replay
suites; `040f541` prepared compound semantics and statement error sites;
`19d50d2` cold re-evaluation blocks; `75cf742` observability fixpoint;
`51fb2e6` record; `f081e47` quirks isolated; `32198fe` prepared string `+=`
— all superseded in part by D-018: `57fa508` one compiled representation
per function (dual compilation, re-evaluation and per-evaluator diagnostics
removed; context refusals; policies; new suites); `aeefaa7` assignment
exemption limited to numeric values; then evidence, scoreboard and this
record. Rebased onto `runtime-2` @`825c90a`; the hashes above are
pre-rebase (now `16ed61e`, `d8dfe12`, `964bd6c`, `129129c`, `f942b9c`,
`5ce2a6a`, `503c89f`, `c96e563`, `4140c03`, `be3b62b`, `19ffd71`).

Review round 1: `f580d08` D2 (INT64_MIN % -1, AddI; CP11 backport
`09ee120` on `runtime-2-d2-int64`), `8370e3f` D3, `158be8c` D1, `96e9fb2`
compile cost, `0bd5f06` per-line policies, operand evidence and R2,
`ad0de0d` checked dispatch for the operand evidence, then this record.
(Branch rebased by the coordinator onto `runtime-2` @`b61e3a5`; round-1
hashes are now `676dd28` D2, `7f0fbf9` D3, `54e041a` D1, `1e94f82`,
`83720e2`, `4d97ae7`, `7b863f4`.)

Review round 2: `de74f26` loop-assignment refusal and precise kinds,
`54a418a` policies checked against the program (D-018a removed), `96f3fe3`
generators within ±2^53, golden results and int bounds, `3a0637d` live
mutation gate, then this record.
