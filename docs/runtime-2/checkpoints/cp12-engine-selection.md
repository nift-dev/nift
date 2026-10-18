# CP12 phase A — Engine selection before execution (bail-and-replay removed)

Status: see `../progress.md` (phase A implemented; targeted tests pass; full
suites, sanitizer run, certification and performance measurements pending an
idle host). Branch `runtime-2-cp12-dev` (local, not pushed), from CP11
`bf32e4f`. Phase B (resolver coverage: globals, `GETDYN`, modules, structs,
enums; checkpoints.md CP12) is next.

## Objective (owner, D-013 removal gate)

After phase A these invariants hold and are enforced by tests:

| | Invariant | How it holds | Test |
| --- | --- | --- | --- |
| A | Single execution: every compilation unit executes at most once | `run_script_on_vm` returns "run legacy" only before anything executed; once the VM starts, it returns the VM's exit status whatever happens. `NIFT_VM_STATS` reports `vm_executions` and `legacy_executions` (exactly one is 1) | `vm_engine_selection.py` (statistics, every printed line, the host-op trace `NIFT_TRACE_HOST`, an appended file); every harness (`vm_parity`, `vm_adversarial`, `vm_subset_gen`, `vm_former_replay`) fails a unit with other counts |
| B | Pre-execution selection | Parse, Opaque check, compile and the kind inference decide; `NIFT_VM=classify` (no execution) names the same engine | `vm_engine_selection.py` (classify vs `NIFT_VM=1` on every case), `vm_subset.py`/`vm_subset_gen.py` (unsupported suites) |
| C | No partial replay | There is no replay code: no `BailReason`, no `Bail`/`NoAlias`/`CheckParam` opcodes, no "replayed" outcome | every harness fails on a `replayed` outcome; `vm_former_replay.py` aims at every former replay reason |
| D | No duplicated effects | Output streams (64 KiB chunks), errors are reported by the VM after the output already written; effectful programs (files, processes, ...) are refused before execution | `vm_engine_selection.py`: file write traced and appended exactly once, no duplicated output line; all suites compare complete stdout/stderr/exit |
| E | Explicit fallback | Unsupported units are classified before execution with a recorded reason (`outcome: fallback`, `reason`, `execute_us: 0`, `vm_executions: 0`) | `vm_subset.py` UNSUPPORTED, `vm_subset_gen.py --suite unsupported`, `vm_engine_selection.py`, `vm_former_replay.py` |
| F | VM errors remain errors | Program errors: the VM reports legacy's error (exit 1). Unexpected failures (broken invariant, flat memory budget, any exception): `NIFT_VM internal failure`, exit 70, never run on legacy | `vm_engine_selection.py` (`internal-failure` case: exit 70, only the VM's output); unexpected operand kinds report internal failures (a compiler defect), never a guess |

## Design

### What replaced each bail

CP11 stopped a run with a bail reason and replayed the unit on legacy. CP12
replaces every reason either by **exact VM behaviour** (the VM does what
legacy does, including its error) or by a **static refusal** (the unit is
refused before execution). The legacy behaviour comes from the two legacy
evaluators (textual and prepared: `cp11-vm-poc.md`, "Legacy's prepared
evaluator", P1–P11).

| Former bail (CP11) | Decision | Mechanism | Old legacy behaviour (recorded) |
| --- | --- | --- | --- |
| `runtime-error`: value-dependent errors (division/modulo by zero, modulo of a non-integral number, non-finite result, integer overflow, index out of range / negative / fractional, missing key or member, element assignment out of range, binding type change, `length()` on an object at top level) | **exact in VM** | error message, source position and `return: ` prefixes as legacy reports them, per evaluator ("Error sites" below); exit 1, output before the error kept | messages and positions differ per evaluator and statement shape (tables below) |
| `runtime-error`: kind errors (`1 - "a"`, `"a" < 1`, `-"a"`, `s += 1`, `s++` on a string, `print([1])`, `return [1]` at top level, `.size()` on a string, `.length()` on a number, member/index on a non-aggregate, `push` on a non-array, concatenation with an aggregate) | **refused** (`type:<op>`, `type:print`, `type:length`, `type:index`, `type:member`, `type:push`, `type:unary`, `type:compound-assignment`, `type:increment`, `type:element-assignment`, `type:script-return`) | static kind inference (`rt/infer*`) proves the operand kinds; an operation whose operands may have a kind legacy rejects or treats specially is refused | legacy errors, with evaluator-specific wording (`@if ordering comparisons require ... of the same type` in an `if`); in loop bodies legacy **renders aggregates as JSON in `+`** (`"a" + [1]` gives `a[\n1\n]`) where the top level fails |
| `unverified` P4: `x = e` typed from text (textual) or value (prepared) | **exact** | every instruction is compiled for the evaluator that runs it (Instr flag `kInstrPrepared`); textual: `CheckAssign` with the text type, prepared: `CheckType` with the value type; a function reachable from both is compiled twice (variants) | textual and prepared verdicts differ for integral doubles and text-typed bindings (`a := "" + 1.5` is a double binding holding a string) |
| `unverified` P5: parameters typed from argument text (textual call) or value (prepared call) | **exact** | a textual call leaves the argument's text type in the callee's parameter type register (frame register nparams + k) for every parameter whose type the callee can observe; the callee's `BindType` combines it with the value; prepared calls type from the value | legacy `bind_call_param` (text) vs `scope.emplace(param, value type)` |
| `unverified` P7: `length()`/`size()` on an object | **exact** | `size()` counts members everywhere; `length()` counts on the prepared path and is legacy's error `unknown value or malformed expression: <text>` on the textual path | legacy accepts `length()` on objects only in loop bodies |
| `unverified`: element assignment with another element type, or out of range | **exact** | type change allowed (both evaluators allow it); out of range: textual `index out of range: <index text>` / `assignment index does not address an array or object: <index text>`, prepared `invalid index target` | — |
| `number-range` P6 (|n| ≥ 2^53, integer −0) | **exact** | textual: CP09 arithmetic (checked int64, exact beyond 2^53, `signed 64-bit integer overflow`); prepared: doubles (−0 kept, no overflow) like `src/Ast.cpp`; a prepared declaration or assignment whose value is large, or a compound assignment / increment with a large operand, runs the statement's **textual code** (legacy re-evaluates it: `large_num` → `c.legacy(st.text)`), compiled next to it as a cold block (`JmpLarge`, `StopReason::Reeval` from the compound slow paths) | `print(w + 2)` in a loop prints `9007199254740992` (double) where top level prints `9007199254740993`; a loop statement `t := f(x) + 2` with a large value **calls f twice** (prepared, then textual) — reproduced exactly, see "Legacy quirks reproduced" |
| `aliasing` P8 (aggregate bound, returned, pushed or put in a literal from a place) | **refused** (`aliasing`) | kind inference: a declaration, assignment, return, push, element assignment, array item or object value written as a place that may hold an array or object | legacy makes a location reference (mutation through it changes the owner) |
| `recursion-budget` | **VM error** (approved D-001#4) | beyond 100,000 frames, or the memory budget exceeded deeper than 64 frames: the VM reports `callable recursion depth exceeded: <fn>` (exit 1, no `return:` prefix, like legacy's) | legacy fails at depth 65 with that error; the harness class D-001#4 approves the VM going further (completing, stopping at its budget, or ending in the program's own error deeper than legacy can go) |
| `output-limit` | **removed** | output streams; nothing is buffered for replay | — |
| `nesting-depth` (`==` deeper than 10,000 levels) | **exact** | the VM decides `==` at every depth (iterative, CP09 `equal`) | legacy answers up to ~30,000 levels and crashes beyond; such programs now get the answer instead of a crash |
| internal | unchanged | exit 70, reported, never run on legacy | — |

### Evaluator variants and error sites

* **Context of every instruction.** The compiler knows statically which
  legacy evaluator runs each statement: top-level code and functions called
  from it are textual; `while` conditions and bodies, and functions called
  from them, are prepared; inside prepared code a statement legacy's prepared
  parser rejects (`legacy_prepared_parse_accepts`, the CP11 port) runs
  textually (`StmtKind::Legacy`), as does a call with a rejected argument
  (`legacy_with_span`). A function is compiled per variant on demand (proto
  per (function, variant)); calls pick the variant of their context. An
  `if`/`while` condition or `return` expression the prepared parser rejects
  inside prepared code is refused (`prepared-unpreparable`: legacy abandons
  preparing the loop).
* **Error sites** (`compiler_rules.cpp`, verified by ~6,000 probe programs and
  the suites below), legacy's reported offset:
  * textual: the failing expression's start (parentheses excluded); inside an
    `if`/`else if` condition the `if`; inside a `return` expression the
    `return`, with a `return: ` prefix per textual `return` the error passes
    through (Instr flag; counted over the frame stack at the error); inside
    index brackets the index expression;
  * prepared: the failing node's span with enclosing parentheses
    (`src/Ast.cpp` `failure_origin`); errors with no failing expression
    (binding type change, compound operation, element assignment) at the
    statement of the outermost loop body that contains them, and in a
    function legacy runs prepared at the call in its caller;
  * `print(path)` on the prepared path: when the location is missing legacy
    resolves the statement again textually (its message, keys evaluated
    again): compiled as a cold textual block entered from the lookup.
* **Messages**: legacy's text per evaluator (`invalid index` vs `JSON array
  index 5 is out of range in 'a[k]'`; `cannot change binding type: x` vs
  `cannot assign double to int binding 'x'`; prepared compound assignment
  `%=` has no integer check and no finite check, ...).
* **Hot path**: flags and notes are read only on slow and error paths;
  re-evaluation blocks are laid out after the function body. Per iteration,
  `s = add(s, i); i += 1` executes one extra instruction (`JmpLarge` after a
  value whose size the compiler cannot bound); `i += 1` none (the check is in
  `AddI`'s slow path).

### Kind inference (`rt/infer.h`, `rt/infer_types.*`)

Flow-insensitive fixpoint over the closed unit: structural types (kinds of a
value; element type of arrays, positional types of literal arrays never
changed in place, member types of objects by key; depth-capped at 6),
bindings resolved with the compiler's scope rules, parameters as the union of
their arguments, returns as the union of return expressions (plus null when
the end is reachable). Something the compiler refuses anyway (an unresolved
name, an unsupported expression) is `kKindUnknown` and defers to that
refusal. Cost: compile time only.

### Runner and statistics

`NIFT_VM=1`: compile; if refused, legacy runs the unit (it was never
touched); otherwise the VM runs it to completion, a program error (exit 1) or
an internal failure (exit 70). `NIFT_VM_OUTPUT_LIMIT` and
`NIFT_VM_EQUALITY_DEPTH` are gone; `NIFT_VM_FLUSH` (test-only) lowers the
streaming threshold. `NIFT_VM_STATS` outcomes: `vm`, `vm-error` (reason = the
message), `fallback`, `require-error`, `internal-error`, `compiled`
(classify); new fields `vm_executions`, `legacy_executions`, `reevaluations`.

## Refused constructs (subset narrowed) and old behaviour

Recorded for later language checkpoints (D-015); none is changed in legacy.

| Reason | Construct | Old legacy behaviour |
| --- | --- | --- |
| `type:*` (see table above) | an operation whose operand kinds may include one legacy rejects or treats per evaluator | errors with evaluator-specific wording; concatenation with an aggregate succeeds only in loop bodies (multi-line JSON) |
| `aliasing` | aggregate bound/returned/pushed/listed from a place | location reference |
| `path-in-call-argument` | a member or index read inside a user-call argument (`f(a[i])`, `f(o.k)`; CP11 compiled read-only callees) | legacy resolves such arguments as locations (`arg_is_location`): the call takes the textual path, errors quote the raw argument text (`JSON value '(o.z)' has no member 'z'`) at positions that depend on the enclosing statement (after the call's `(` in a declaration, at the argument in `print`) |
| `path-in-index` | a member or index read inside index brackets (`a[b[i]]`) | a failing inner index is reported as `unknown value or malformed expression: a[k` (the textual path splits the text) |
| `prepared-unpreparable` | an `if`/`while` condition or `return` expression legacy's prepared parser rejects, inside prepared code | legacy abandons preparing the loop body |
| `print-path-nested` | `print(path)` nested in another expression inside prepared code | location re-resolution (above) |
| `param-type-unpredicted` | (defensive) a parameter type read the analysis did not predict | — |

Positional precision is limited to literal indexes (`p := [1, {"z": 4}];
p[1].z` compiles, `p[i].z` is refused `type:member`); objects read through a
non-literal key take the union of their members.

## Legacy quirks reproduced exactly (owner decision requested)

D-015 prefers refusal to machinery. The cases below are value-dependent (no
static refusal short of refusing every loop assignment with a call, which
would include the gated flat-call benchmark), so the VM reproduces them:

1. A prepared declaration/assignment whose value is a large number (|n| ≥
   2^53), or a compound assignment/increment with a large operand, is
   re-evaluated on the textual path: calls in it run twice (their output
   appears twice) and the value is the textual one. Candidate: evaluate once,
   exact integers everywhere.
2. `print(path)` in a loop with a missing location re-resolves the path
   textually (keys evaluated twice). Candidate: one evaluation.
3. Statement errors of a loop-called function are reported at the call in the
   caller; nested statements in loops at the outermost loop statement.
   Candidate: report at the failing statement (D-009#5 already approves this
   for the textual path).
4. Wording and position of errors differ per evaluator (CP13 diagnostics).

## Tests (targeted; host shared, `make -j1`)

| Suite | Result |
| --- | --- |
| `tests/rt/vm_unit.cpp` (errors with exact messages, refusals of former replay cases, streaming, recursion and memory budgets, code shape incl. variants and `JmpLarge`) | 40,252 checks, 0 failures |
| `vm_engine_selection.py` (invariants A–F, 12 cases with prints, file writes, host trace, errors after output, 200,000-line output, recursion budget, internal failure) | PASS |
| `vm_former_replay.py` (new: errors × 21 expression shapes × 9 statement kinds × 10 contexts, type changes, numeric boundaries, recursion, aggregates, methods, call chains, loops with textual statements, output) | seed 1, 600/family: 1,796 programs — 1,459 on the VM (742 vm, 717 vm-error), 337 refused before execution, **0 replayed**, 0 differences; seed 3, 1,500/family: 2,696 programs, 0 failures |
| `vm_subset.py` | 49 supported on the VM (`require`), 57 unsupported refused: PASS |
| `vm_subset_gen.py` (supported / former / unsupported) | seed 1, 300: supported 300/300 on the VM (100 %), former-replay cases: vm or vm-error with legacy's output or refused, 0 replayed; unsupported 300/300 refused before execution |
| `vm_adversarial.py` (CP11 gate, 21 families) | seed 1, count 100: 30,921 programs, 0 non-approved differences (47 approved D-001#4); seed 2 sampled (4,169): 0 |
| `vm_parity.py` (VM corpus 78 scripts + 300 generated + 300 mutants) | PASS; corpus outcomes 16 vm, 11 vm-error, 51 fallback (2 approved D-001#4) |
| ad-hoc probe matrix (error kinds × shapes × contexts; nested loops/ifs with re-evaluation) | 6,000 + 80 programs, 0 differences |
| `tools/rt-lint.py src/rt`, `legacy_names.py --check` | 0 problems; up to date |

Defects the suites found during phase A (all fixed): a textual call passing a
binding's type let that binding's `BindType` be dropped (stale type register;
found by `vm_former_replay`); prepared compound assignment semantics (`%=`
without integer check) and statement-error positions in nested loops (found
by the CP11 adversarial gate, `typed`/`shadow` families).

**Pending (host not idle):** `make test-rt-vm` in full (rt-diff over the
shared corpora with execution on both engines, 600-count adversarial gate),
`make test-rt-vm-sanitize`, `make -k test`/`make certify`, benchmarks.

## Coverage

Coverage is measured, not a threshold (D-016).

| Measure | CP11 (round 3) | CP12 phase A |
| --- | --- | --- |
| Shared corpora (rt-diff script entries, 786) | 81 VM, 44 replayed, 661 fallback | **104 VM** (81 vm + 23 vm-error), **0 replayed**, 682 fallback (new reasons: aliasing 19, type:push 10, other type:* 10, path-in-call-argument 1) |
| General generator (seed 1, 300) | 68 VM (22.7 %), 24 replayed (8.0 %), 208 fallback | **72 VM (24.0 %**, 5 ending in a program error), 0 replayed, 228 fallback (type:* 20: the programs' own kind errors, CP11 replayed them) |
| Supported-subset generator | 100 % (deliberate replay programs separate) | 100 % (subset narrowed: no member/index reads in user-call arguments) |
| VM corpus (78) | 16 vm, 24 replayed, 38 fallback | 16 vm, 11 vm-error, 51 fallback, 0 replayed |

## Performance

Not measured yet (shared host; owner instruction). Expected, from the code
shape: flat call +1 dispatched instruction per iteration (`JmpLarge`; CP11
461 instructions, gate ≤ 2,994 and ≤ 2× CPython); loop and add-assign
unchanged (re-evaluation checks in slow paths); fib: 2 instructions fewer per
call than CP11 (a parameter's text type is passed only when the callee can
observe it, a call-graph fixpoint; fib's is not, where CP11 computed and
checked one per call with `TypeMeet` + `CheckParam`); compile overhead: kind
inference (structural types, fixpoint), per-variant compilation, cold blocks
and preparability checks per statement; cold start and peak RSS: expected
within noise for small scripts. To be measured with `vm_compare.py` when the
host is idle: call, loop, fib, compile overhead, cold start, peak RSS.

## Risks

* **Diagnostics are reproduced by a model.** Positions and wording come from
  legacy's evaluators as observed and read; the probe matrix and the suites
  cover the compiled subset's shapes, but a shape the generators do not
  produce could still differ (as in CP11, differential evidence, not proof).
  The full error-parity work is CP13.
* **Two evaluators in the VM.** Variants double the code of functions called
  from both contexts; the evaluator choice per statement follows legacy's
  preparability (ported parser). Legacy changes there would need the port
  updated (`vm_prepared_parse` differential test).
* **Reproduced quirks** (above) are deliberate until the owner decides.
* **Equality depth**: legacy crashes on very deep values where the VM answers.
* Coverage loss is concentrated in aggregates read through places and paths
  in call arguments; phase B and later language checkpoints can widen it.

## Commits

`ae7363d` remove bail-and-replay (runner, variants, inference, error sites,
tests adapted); `b9589d0` invariant and former-replay suites (+ one fix);
`451eaae` prepared compound semantics and statement error sites; `ef0c22c`
cold re-evaluation blocks; the observability fixpoint; this record.
