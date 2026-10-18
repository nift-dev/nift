# CP12 phase A — Engine selection before execution (bail-and-replay removed, D-018)

Status: see `../progress.md`. Branch `runtime-2-cp12-dev` (local, not
pushed), rebased on `runtime-2` @`f3208f4` (CP11 certified). Phase B
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
  number legacy calls large, |n| ≥ 2^53, or went out of range),
  `large_in_loop_path` (of those, in code legacy runs on its loop evaluator:
  the flag on the instruction or on any active call) and
  `error_in_loop_path`. The harness policies require this evidence.

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

| Policy | Requires (evidence) | Shape approved |
| --- | --- | --- |
| `D-001#4 recursion` | `beyond_legacy_depth` | legacy fails only with `callable recursion depth exceeded`; its output a prefix of the VM's; the VM completed or stopped with a program error |
| `D-018a numbers` | `large_in_loop_path > 0`, outcome `vm`/`vm-error` | output lines differ only in their numbers; extra legacy lines repeat lines the VM printed (re-evaluated calls); the VM may stop with `signed 64-bit integer overflow` / `arithmetic result is not finite` where legacy went on (VM output then compared with a prefix of legacy's); otherwise stderr equal or D-018c |
| `D-018c diagnostics` | outcome `vm-error`; `error_in_loop_path` for the wording map and for an earlier VM position | same stdout, both exit 1 with one diagnostic on the same file; messages equal once legacy's `return: ` prefixes are removed, or (loop code only) legacy's loop wording maps to the canonical one for the same binding; the VM position is not before legacy's (or loop code) |

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

Flow-insensitive fixpoint over the closed unit: structural types (kinds of a
value; element type of arrays, positional types of literal arrays never
changed in place, member types of objects by key; depth-capped at 6),
bindings resolved with the compiler's scope rules, parameters as the union of
their arguments, returns as the union of return expressions (plus null when
the end is reachable). Something the compiler refuses anyway is
`kKindUnknown` and defers to that refusal. The D-018 refusals add two
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
`large_in_loop_path`, `error_in_loop_path`.

## Refused constructs and old behaviour

Recorded for later language checkpoints (D-015); none is changed in legacy.
The `context:*` reasons are in "D-018 consequences" above.

| Reason | Construct | Old legacy behaviour |
| --- | --- | --- |
| `type:*` | an operation whose operand kinds may include one legacy rejects | errors; concatenation with an aggregate succeeds only in loop bodies (multi-line JSON) |
| `aliasing` | aggregate bound/returned/pushed/listed from a place | location reference |
| `path-in-call-argument` | a member or index read inside a user-call argument | legacy resolves such arguments as locations: errors quote the raw argument text at statement-dependent positions |
| `path-in-index` | a member or index read inside index brackets (`a[b[i]]`) | a failing inner index is reported as `unknown value or malformed expression: a[k` |
| `prepared-unpreparable` | an `if`/`while` condition or `return` expression legacy's prepared parser rejects, in loop code | legacy abandons preparing the loop body |
| `param-type-unpredicted` | (defensive) a parameter type read the analysis did not predict | — |

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
* **Approved differences are policy-checked, not unbounded.** Each policy
  needs VM evidence and a narrow shape; `vm_d018.py` pins both sides.
* **Diagnostics follow the canonical rules** (D-018c); full error parity is
  CP13.
* **Equality depth**: legacy crashes on very deep values where the VM
  answers.

## Commits

`968bcf0` remove bail-and-replay; `5bc14be` invariant and former-replay
suites; `040f541` prepared compound semantics and statement error sites;
`19d50d2` cold re-evaluation blocks; `75cf742` observability fixpoint;
`51fb2e6` record; `f081e47` quirks isolated; `32198fe` prepared string `+=`
— all superseded in part by D-018: `57fa508` one compiled representation
per function (dual compilation, re-evaluation and per-evaluator diagnostics
removed; context refusals; policies; new suites); `aeefaa7` assignment
exemption limited to numeric values; then evidence, scoreboard and this
record.
