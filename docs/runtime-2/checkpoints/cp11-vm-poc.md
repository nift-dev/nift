# CP11 — VM proof of concept (GO/NO-GO)

Status: see `../progress.md` (implemented, review rounds 1, 2 and 3 fixed;
certification pending). **Gate verdict: GO** (see "Gate", re-measured after
review round 3), with the risks and limits listed below. Integrated on
`runtime-2` (CP09 + CP10 certified with their review fixes; rebased for
round 3 on `runtime-2` @`c4c5894`, which adds feature-sha256) as branch
`runtime-2-cp11i`. Round 3 was the last review round (D-015).

## Objective
Show, with real compiled Nift programs, that the approved architecture (D-001:
register bytecode VM, 16-byte values, heap frame stack, whole-unit fallback)
makes calls at least 5× cheaper than v4.12 and no more than 2× CPython,
with output identical to the legacy engine on every unit the VM runs.

## Scope
- New in `src/rt/`: `opcodes.h`, `proto.*`, `codegen.*`, `resolver.*`,
  `compiler.*` (statements, entry), `compiler_expr.*` (expressions),
  `compiler_rules.*` (admission and binding-type rules), `literal.*`,
  `text_type.*`, `native.*`, `render.*`, `vm.*`, `vm_ops.*`,
  `script_runner.*`, and since review round 1 `alias.*` (argument aliasing
  analysis) and `legacy_names.*` (`legacy_names.cpp` is generated from the
  legacy evaluator source by `tools/rt-vm/legacy_names.py`), since review
  round 2 `prepared.*` (static refusal of prepared-evaluator shapes) and
  `prepared_parse.*` (exact port of legacy's prepared expression parser as
  a recogniser), since round 3 `unit_check.*` (the unit's tree depth is
  bounded, iteratively, before any analysis); all lint-clean.
  `src/rt` is now linked into `nift` (`RT_LINK_SOURCES` in the Makefile).
- `src/CLI.cpp`: one hook in `run_script_source` (file, `-e`/`-c`, stdin
  scripts; not when an interactive shell follows, only the native target).
- Tests and tools: `tests/rt/vm_unit.cpp`, `tests/rt/vm_run.cpp`,
  `tests/rt/rt_vm_tool.h`, `tests/runtime2/vm-corpus/cp11/` (75 scripts +
  `EXPECT`), `tests/runtime2/vm_gen.py`, `tests/runtime2/vm_parity.py`,
  `tests/runtime2/vm_adversarial.py` + `vm_adversarial_gen.py` (review round
  1), and since round 2 `tests/rt/vm_prepared_parse.cpp`,
  `tests/runtime2/vm_subset.py`, `vm_adversarial_gen2.py`,
  `vm_shadow_gen.py`, and since round 3 `vm_adversarial_gen3.py` and
  `vm_subset_gen.py` (the generated supported-subset and unsupported suites,
  D-016); `tools/rt-vm/nift-vm`, `tools/rt-vm/legacy_names.py`,
  `tools/rt-diff/policies/cp11-vm.json` (plus class policies in
  `tools/rt-diff/rt-diff`), `benchmarks/runtime2/vm_compare.py`.
- CP09/CP10 files: unchanged by CP11 itself; review round 2 made two
  labelled follow-up commits (CP10: `\f`/`\v` inside statements are Opaque;
  CP09: iterative equality/hashing, exact live-heap accounting
  `heap_account.*`, reference safety enforced in sanitizer builds), each
  recorded in its own checkpoint record. All of `src/rt` is linted.

## Design

### Opt-in and fallback
| Setting | Behaviour |
| --- | --- |
| `NIFT_VM` unset or `0` | Nothing new runs (one `getenv`); `nift` behaves exactly as before |
| `NIFT_VM=1` | Eligible scripts run on the VM; everything else, and every run-time bail, runs on legacy unchanged |
| `NIFT_VM=require` | Never uses legacy: an ineligible script or a bail is an error (`error: <file>:<l>:<c>: …`, exit 1). Used for every VM measurement |
| `NIFT_VM=classify` | Test tooling: parse and compile only, report `compiled`/`fallback` (with `NIFT_VM_STATS`), run nothing |
| `NIFT_VM_STATS=1` / `=<path>` | Test tooling only. One JSON line per script (stderr / appended to the file): outcome, fallback or bail reason, position, parse/compile/execute µs, protos, instructions, `beyond_legacy_depth`. With `=1` the line is written to stderr **before** a replay runs, so stderr then differs from legacy's; harnesses use `=<path>` |
| `NIFT_VM_DUMP=1` | Test tooling only: bytecode listing on stderr, also written before a replay |
| `NIFT_VM_OUTPUT_LIMIT=<bytes>` | Test-only: lowers the 64 MiB output buffer limit (D7 regression tests) |
| `NIFT_VM_HEAP_BUDGET=<bytes>`, `NIFT_VM_EQUALITY_DEPTH=<levels>` | Test-only: lower the memory budget (1 GiB) and the equality depth (10,000) (review round 2 R5/R6 tests) |

Unexpected VM failures (an internal error such as a bad opcode, `std::bad_alloc`
or any other C++ exception) are reported as `error: <file>: NIFT_VM internal
failure: …` with exit status 70 and are **never** replayed on legacy (owner
directive: unexpected failures are not disguised as legacy runs).

The compilation unit is the whole script (D-001). Fallback is decided in two
places:
1. **Static.** Parse errors (legacy then reports its own identical
   diagnostic, CP10), any Opaque node, and any construct, native or spelling
   outside the subset (table below) leave the unit to legacy before anything
   runs.
2. **Run time (bail and replay).** The subset has no effects other than
   `print`, and VM output goes to a buffer that is written only when the run
   completes. When the VM reaches a case it does not reproduce, it stops,
   discards the buffer and the whole unit is run on legacy from the start, so
   legacy produces every diagnostic (text, position, partial output) itself.
   Bail reasons (an enumerated set). The VM decides each one at run time.
   In general the deciding fact is a computed value; some instances are
   decidable from literals alone and are **not** refused by the compiler
   (CP11 does no constant evaluation): `print(1 / 0)`, `print("a" < 1)`,
   `print(-"a")` (`runtime-error`), a top-level `x := 1; x = 2.0`, `f(1.0)`
   (`unverified`: an integral double literal), `o := {"k": 1};
   o.length()` (`unverified`); `[1][5]` and `{"k": 1}.length()` are refused
   anyway (postfix on a literal). The subset has no effect other than
   buffered output, so every replay is side-effect free (D-013); the replay
   mechanism is removed in CP12 (D-013 removal gate). The reasons:
   `runtime-error` (anything legacy reports as an error: legacy produces its
   own diagnostic), `number-range` (a computed integer or double at or beyond
   2^53, integer −0; see "Numbers"), `aliasing` (a value bound or returned
   from a place turns out to be an aggregate, where legacy makes a location
   reference), `unverified` (assignment or argument types where the textual
   and prepared legacy paths disagree for this value — P4/P5 in the
   inventory; a different type assigned to an array element; `length()`/
   `size()` on an object), `recursion-budget` (frame, register or memory
   budget beyond legacy's depth), `output-limit`, `nesting-depth` (`==` on
   values nested deeper than the VM decides). Review round 2 removed
   `static-error` (statically known errors are now refused before execution)
   and the N1 declaration check (now a static refusal).
   A replay is not observable except in time (the VM work is wasted) and in
   when output appears (at the end instead of progressively). **Nothing is
   ever committed before the run completes under `NIFT_VM=1`** (review D7):
   when the buffer reaches 64 MiB the run stops (`output-limit`), the buffer
   is discarded and legacy runs the unit from the start. Cost: up to 64 MiB
   of buffer memory, and for programs printing more than 64 MiB the VM work up
   to that point is wasted (the program then runs entirely on legacy). Only
   `NIFT_VM=require` (which never replays) flushes early.

### Instructions and code
8-byte instructions (`op`, three 16-bit operands). An RK operand with bit 15
set names a constant. 54 opcodes (`opcodes.h`): loads, `Move`; `Add Sub Mul
Div Mod` (RK operands), `AddI` (immediate), `AddAssign`; comparisons; fused
`JmpNot{Lt,Le,Gt,Ge,Eq,Ne}` for conditions; `Jmp JmpFalse JmpTrue JmpNotNull`;
`Call CallNative Return ReturnNull TopReturn Halt`; `NewArray NewObject
ObjectSet GetIndex SetIndex GetMember Push Length`; binding-type
instructions `BindType CheckType CheckAssign CheckParam TypeMeet`; `NoAlias`,
`Bail`. A `Proto` is immutable after compilation (code, per-instruction source
offsets, constant pool, register and parameter counts). Dispatch is computed
goto on GCC/Clang with a portable `switch` (`-DNIFT_RT_VM_SWITCH`); integer
fast paths are inline, every other case calls an out-of-line slow path
(`vm_ops.cpp`). Unread `BindType`s are removed after code generation.

### Frames and calls
- Registers live on one heap register stack (`std::vector<rt::Value>`),
  frames on a heap frame stack (`{return pc, base, proto}`). A call pushes a
  frame and continues in the same dispatch loop: **Nift→Nift calls never
  recurse on the C++ stack** (unit test: 90,000 nested calls on a thread with
  a 256 KiB stack).
- Calling convention: the caller evaluates the arguments into consecutive
  registers `a … a+n−1`; the callee frame starts at `a` (arguments are passed
  in place, no copy); the result is left in the callee's `R[0]`, which is the
  caller's `R[a]`. When the destination is the newest register the window
  starts there and the result needs no move. On return the callee's other
  registers holding heap values are released.
- Arity is checked at compile time (a mismatch compiles to `Bail`).
  Arguments are copies by COW (`rt::Value` refcounts): callee mutation is
  invisible to the caller (semantics.md "Copy at calls").
- Recursion depth is a frame budget of 100,000 frames (semantics.md
  Recursion row, approved change D-001). Beyond it the VM bails and legacy
  reports "callable recursion depth exceeded" (same message family). Programs
  between 65 and 100,000 frames deep succeed on the VM where legacy fails
  (approved, D-001#4). The harnesses classify this difference automatically:
  `vm_adversarial.classify_difference` / `vm_parity.py` and the rt-diff class
  policy `cp11-recursion-budget-class` approve it only when legacy fails with
  exactly that diagnostic, the VM completes with legacy's output as a prefix
  of its own, and the VM statistics show `beyond_legacy_depth`.
- Memory (review R1, round 2 R5): the register stack is capped at 16M
  values; the whole run is bounded by `heap_budget` (1 GiB): the **exact**
  live rt heap of the isolate (`heap_account.h`, CP09: every string,
  array, object, decimal, error, at any nesting depth, counted when created
  or resized and uncounted when freed) plus the register and frame stacks.
  It is checked after every instruction that can allocate — all of them
  slow paths (concatenation, in-place append, array/object construction,
  push, member set) — and when the stacks grow; no new hot-path test.
  Exceeded deeper than legacy's 64 frames: `recursion-budget` bail
  (legacy then reports its recursion error, approved D-001#4). Exceeded at
  a depth legacy also reaches: an internal failure, reported (exit 70),
  never replayed. Either way the process stops itself at the budget instead
  of being killed (the round-2 repro, `[[s]]` per frame, now stops at about
  2,600 frames; it was OOM-killed). The budget is not a promise that the
  host has that much memory: on a host with less free memory than the
  budget the kernel can still kill the process first.

### Natives
Bound to integer ids at compile time: `print` (`NativeId::Print`). The
methods `push`, `length()` and `size()` compile to `Push`/`Length`
(receiver-specific, review D3: `length()` on strings and arrays, `size()` on
arrays; `size()` on strings, numbers, bools and null and `length()` on
numbers, bools and null are legacy errors and replay; objects replay because
legacy's `length()` on an object works only in loop bodies). `push` takes
exactly one argument and an array receiver (anything else is a legacy
error). A user function, parameter or binding whose name legacy may
dispatch specially is a fallback (review D4/D5; candidate language change
D-014): the table `legacy_names.cpp` (206 names) is generated from the legacy
evaluator source — `call_args` heads, call-prefix tests, comparisons against
callee/name variables, string-only brace lists such as `native_heads`, the
`is_*` predicate loop and `reserved_binding_name` — plus keywords, and
`make test-rt-vm` fails when it is stale.

### Legacy semantics the VM reproduces
- **Numbers.** CP09's canonical numeric tower, inside the domain where the
  legacy top-level path (int64) and loop path (doubles, textual re-evaluation
  of results ≥ 2^53) agree: every integer below 2^53 in magnitude, no integer
  −0 (`0 * -1`, `-4 % 2`, `-0`). Outside it the VM bails.
- **Binding types.** A binding keeps a legacy type (null, bool, int, double,
  string, array, json…). Legacy derives it from the declaring expression's
  *text* as well as its value (`Parser::expression_type`, ported to
  `rt/text_type.cpp`): `v := "" + 1.5` is a double binding holding a string,
  `d := 1.0` is a double binding holding an integral value, `y := x` takes
  x's binding type. Plain assignments are checked with the text-derived type
  at top level and the value type in loop bodies; where the two verdicts
  differ the VM bails. Arguments whose text type disagrees with their value
  bail (legacy types parameters from argument text). A declaration whose
  text refers to another binding is typed from that binding at run time;
  where legacy prepares it against other bindings (loop-local shadows,
  loop-called functions) it is refused before execution (review round 2,
  M1/N1; see "Legacy's prepared evaluator").
- **Rendering.** Strings as-is; numbers through the legacy
  `RuntimeValue::dump`; `true`/`false`/`null`. Arrays/objects bail.
- **Strings.** `s += t` appends in place when the string is unshared (legacy
  does the same), so 320k appends stay linear.
- **Places.** Declaring/assigning/returning/pushing an aggregate taken from a
  place bails (`NoAlias`): legacy creates location references there. A
  member/index argument (`f(o.k)`, `f(a[0])`, parenthesised too) is a legacy
  location reference as well (review D6): `alias.cpp` marks each parameter
  the callee may mutate or rebind (method calls other than
  `length()`/`size()`, index assignment, `=`/compound/`++` targets, passing
  it on to such a parameter, to a fixpoint) and a call passing a place to
  such a parameter falls back (`alias-argument`); read-only callees compile.
- **Decimal literals.** Legacy parses them as JSON and keeps the spelling of
  zero-valued decimals with a fraction (`0.0`, `0.00`, `0.0e5`; negation
  gives `-0.0`) and of exponents of magnitude ≥ 300; such literals fall back
  (review D1).

### Supported subset vs fallback

| Feature | VM | Legacy (fallback) |
| --- | --- | --- |
| Literals: integers < 2^53 without leading zero; non-zero decimals with ≤ 15 significant digits, magnitude < 2^53 and exponent magnitude < 300; strings without `\`, `$`, control bytes, invalid UTF-8; `true` `false` `null` | ✓ | other spellings (exact decimals, zero-valued decimals `0.0`/`0e5`, `1.0e-300`, `007`, escapes, interpolation) |
| `x := e`, `x = e`, `+= -= *= /= %=`, `++`/`--` on names | ✓ | `const`/`immut`, destructuring, `&= \|= ^=`, increments inside expressions |
| `a[i] = v` with `i` a local name or an unsigned integer literal written directly in the brackets (array, existing element, same type) | ✓ (else bail) | any other index — computed, parenthesised, member, call, decimal, string — (`index-target`, review round 3: legacy reads a target whose text contains `.` as a member path); compound and `++`/`--` element targets; member assignment |
| `+ - * / %`, comparisons of one operator, `&&` `\|\|` `!` `??`, `?:` in parentheses, unary `-` `+` | ✓ | `!` ending the left side of an operator (legacy reads `!x + 1` as `!(x + 1)`); a comparison whose operand is a comparison, in any order, parenthesised or not (`comparison-chain`, review round 3); `?:`, `??` and object literals inside an `if`/`while` condition (CP10 Opaque) |
| `if` / `else if` / `else`, `while`, `break`, `continue` as the last statement of their block | ✓ | `for`, `try`/`catch`, `throw`; a `break`/`continue` followed by another statement in its block (legacy ignores the jump; D2, candidate change D-014) |
| `fn` declarations (top level, before the first other statement), calls, recursion, `return` (also at top level) | ✓ | `fn[async]`, `@fn`, nested functions, lambdas, struct/enum/import/export, functions referring to non-local names (dynamic scope, CP12); function, parameter or binding names in the generated legacy name table (D4/D5); a member/index argument to a parameter the callee may mutate (D6) |
| Arrays/objects: literals, `a[i]`, `o.k`, `o["k"]`, `.push(x)` on arrays, `.length()` on strings/arrays, `.size()` on arrays | ✓ (other receivers replay) | every other method or built-in (`sort_by`, `map`, `stringify`, `split`, …), `?.`, `?[`, spread, postfix on literals, whitespace before postfix, trailing commas, duplicate object keys |
| `print(x)` | ✓ | every other native; bare expression statements other than calls |

### Supported subset (specification, after review round 3)
A script runs on the VM only if every function and every top-level
statement is inside this subset; otherwise the whole script runs on legacy
before anything executes. Four suites pin it (D-016):
`tests/runtime2/vm_subset.py` SUPPORTED (49 programs covering every item
below at top level, in loop bodies, in functions called from top level and
from loops: each must run entirely on the VM under `NIFT_VM=require` with
legacy's exact output) and UNSUPPORTED (57 programs, one per refused
construct or context: each must fall back before execution with the
expected reason), and the generated `tests/runtime2/vm_subset_gen.py`
suites: `supported` (random programs built only from this specification in
every context; 100 % must run on the VM, except programs that deliberately
end in a documented run-time condition, which must replay with that reason)
and `unsupported` (one of 38 excluded constructs injected into a supported
program in a random context; refused before execution, nothing executed).

Supported:
- Literals: integers below 2^53 without leading zeros; decimals other than
  zero-valued spellings, with ≤ 15 significant digits, magnitude < 2^53 and
  exponent magnitude < 300; strings without `\`, `$`, control bytes or
  invalid UTF-8 (`"…"` and `'…'`); `true`, `false`, `null`; array and object
  literals (double-quoted keys, no duplicates, no trailing commas).
- Statements: `x := e` (not `const`/`immut`/patterns), `x = e`,
  `+= -= *= /= %=`, `++`/`--` on names, `a[i] = v` on a local array where
  the brackets hold exactly a local name or an unsigned integer literal,
  `a.push(v)`, call statements, `if`/`else if`/`else`, `while`,
  `break`/`continue` as the last statement of their block, `return`
  (functions and top level).
- Expressions: `+ - * / %`, comparisons of exactly one operator (no
  operand of a comparison is itself a comparison, parenthesised or not),
  `&& || ! ??`, `?:` in parentheses (not inside an `if`/`while` condition), unary `-`/`+`, `a[i]`, `o.k`, `o["k"]`, `.length()` on strings
  and arrays, `.size()` on arrays, `print(x)`, calls to top-level functions.
- Functions: `fn(name(params))` at top level before the first other
  statement, recursion (100,000-frame budget), referring only to their own
  parameters and locals.

Excluded contexts (refused even though each construct alone is supported):
- a declaration whose text refers to another binding, in a function
  reachable from a loop body or condition, or in a loop where that name is
  (re)declared inside the outermost enclosing loop (`prepared-decl-type`);
- a function reachable from a loop whose body contains a bare `return`, or
  an `if`/`while` condition or `return` expression legacy's prepared parser
  rejects — e.g. `?:`, a comparison chain, `(a ?? b)`, an object literal
  with a string value (`prepared-fn-unprepared`);
- a string-literal argument to a user function whose unquoted text types as
  a double or names a binding visible at the call (`string-arg-type`);
- a member/index argument to a parameter the callee may mutate
  (`alias-argument`); a `break`/`continue` followed by statements in its
  block (`jump-not-last`); names in the generated legacy name table;
- `\f`/`\v`, NEL, NBSP, U+2028/9, BOM, NUL, DEL and other control bytes
  inside a statement (CP10 Opaque);
- statically known errors: redeclaration in one scope, wrong argument
  count, member assignment, `push` with other than one argument
  (`static-error:*`);
- review round 3: element assignment with any other index (`index-target`);
  a comparison whose operand is a comparison (`comparison-chain`); a unit
  whose syntax tree is deeper than 256 nodes (`nesting-limit`, checked
  iteratively before any other analysis, so no input can exhaust the stack
  during compilation); `?:`, `??` and object literals anywhere inside an
  `if`/`while` condition (CP10 Opaque; made explicit here after the
  generated supported suite found the specification imprecise).

### Legacy's prepared evaluator (review round 2 inventory)

Legacy has a second evaluator. When a `while` statement starts
(`src/ParserTemplate.cpp` `@while(` handler, ~line 1705), its condition is
parsed by `nift::ast::parse_expression` (`src/Ast.cpp`, struct `P`) and its
body is prepared once by `prepare_loop_body` (ParserTemplate ~1123) into
statement trees executed by `execute_prepared` (~1670–1700). A user function called from prepared
code is translated and prepared once per program on its first prepared
call (`c.call`, ~1284–1350; cache `prepared_callables_`) and runs on the
same evaluator. Everything else (top-level statements, `if` at top level,
functions called from them, a loop body whose preparation failed) runs on
the textual evaluator (`ParserExpression.cpp`, `ParserScript.cpp`).
Reading both end to end gives these differences (the policy is the owner's:
refuse statically where the meaning depends on the evaluator; the intended
future behaviour is the owner's decision and is not implemented):

| # | Prepared path (source) | Textual path | Handling in CP11 |
| --- | --- | --- | --- |
| P1 | Declaration type fixed at preparation: `decl_type = expression_type(text after :=)` against the bindings visible **when the body is prepared** (ParserTemplate `prepare_loop_body`, `$[` leaf; executed `bt = dt==3 ? 3 : dt==2/-1 ? value type : dt`) — for a loop, at its outermost header (loop-local shadows not yet declared); for a loop-called function, in the **caller's** scope chain before its own parameters exist (M1, N1) | `nift_binding_type_from_text` at run time against the current bindings (ParserExpression ~4636) | **refused** `prepared-decl-type`: in loop-called functions every declaration whose text refers to a binding; in other loops, a declaration referring to a name (re)declared anywhere inside the outermost enclosing loop. Otherwise the binding visible at the header is the one the VM resolves (exact). The N1 run-time check is removed |
| P2 | Preparation failure: `c.call` returns false when `translate_function_program` or `prepare_loop_body` fails, after the arguments were evaluated; `nift::ast::evaluate` (Ast.cpp `Kind::Call`) then evaluates the whole call text on the textual path, evaluating the arguments again (M2). Failures: a bare `return` (`@__bare_return()` is not a prepared statement), an `if`/`else if`/`while` condition or `return` expression the prepared parser rejects (P3) | n/a | **refused** `prepared-fn-unprepared` for every function reachable from a loop body or condition (call-graph fixpoint) whose body contains one of them; acceptance is decided by `rt/prepared_parse.cpp`, an exact port of the prepared parser (differential test against `src/Ast.cpp`, 100,136 texts, 0 differences) |
| P3 | Prepared grammar (Ast.cpp `P`): no `?:`; one comparison per level (`a < b == c` rejected); `(…)` holds `logical_or` (so `(a ?? b)` is rejected); `!`/`-`/`+` bind to the next unary; `isspace` (incl. `\f`, `\v`) between tokens; object literals only with plain `"key"`s and non-string values; integers beyond 2^53 rejected; `strtod` numbers (hex accepted); 8,192-byte / 256-bracket / 128-recursion limits; a call argument it rejects sends that call to the textual path | CP10 grammar (textual) | rejection matters only through P2 (refused). Where both accept, the trees agree for the compiled subset (equality vs relational nesting and `!` precedence are CP10 Opaque; `\f`/`\v` are Opaque since M4; hex/escapes are not compiled) |
| P4 | Plain assignment checks the **value** type (`assign_plain`: `nift_binding_type(v)`) | text-derived type (`nift_binding_type_from_text`, ParserExpression ~2261/2280) | **replay** `unverified` when the two verdicts differ (CheckAssign) — remains: it depends on whether a computed number is integral at run time |
| P5 | Parameters bound by **value** type (`scope.emplace(param, nift_binding_type(arg))`) | `bind_call_param`: text type of the argument; a quoted argument is typed from its **unquoted** text, looked up as a binding name in the caller's dynamic scope (M3) | string literals: **refused** `string-arg-type` when the unquoted text types as a double or names a binding visible at that call (globals, the calling function and every function that can be below it); other arguments: **replay** `unverified` when text and value types differ (CheckParam) — remains, value-dependent |
| P6 | Numbers as doubles; results at or beyond 2^53 re-evaluated textually (`large_num`) | int64/StrNumber | **replay** `number-range` (value-dependent, unchanged) |
| P7 | `length()` on objects works; other method dispatch via `native_method` | `length()` on objects is an error | **replay** `unverified` for `length()`/`size()` on objects (receiver kind is a run-time property) |
| P8 | Declaring from a place makes a location reference / shares the slot (`bind_location`, aggregate `b := a`); returns of places | location references too | **replay** `aliasing` when the value is an aggregate (kind is a run-time property); place arguments to mutating callees refused (D6) |
| P9 | `break`/`continue` stop the block at the next statement (`execute_body` checks `pending_control_`) | the rest of the block runs (D2) | **refused** `jump-not-last` (D2) |
| P10 | Errors carry prepared-view positions and messages | textual positions | every run-time error replays on legacy (`runtime-error`), which reports its own diagnostic |
| P11 | `+=` on a string appends in place when unshared; native calls go through temporaries and the textual evaluator; recursion limit 64 (`callable_call_depth_`) | same values / same limit | identical results; nothing to refuse |

Eleven differences: four refused statically by this round (P1, P2 with P3,
P5 string literals, plus M4 in the parser), one already refused (P9), five
kept as enumerated, side-effect-free replays because the deciding fact is a
run-time value (P4, P5 non-literal arguments, P6, P7, P8) and P10 replays
every error by design; P11 needs nothing.

## Tests

Results after review round 3 on `runtime-2-cp11i` @`c147f35` (idle 2-vCPU
host except where noted). Round 2's figures are in git history.

| Suite | Content | Result |
| --- | --- | --- |
| `tests/rt/vm_unit.cpp` | outputs, fallback reasons (round 3 adds `index-target`, `comparison-chain` and admitted forms), bail reasons, literal decoding, Int rendering = legacy `dump` (20,000 values), frame budget, memory budget, deep equality (9,999 decided, 10,001 bails), 90,000 nested calls on a 256 KiB stack, ten 100,000-deep inputs compiled on a 512 KiB stack, a 50,000-statement list, output limit, generated name table, code shape | 40,236 checks, 0 failures |
| `tests/rt/vm_prepared_parse.cpp` | the prepared-parser port against legacy's `nift::ast::parse_expression` | 100,136 checks, 0 differences |
| `tests/runtime2/vm_subset.py` | SUPPORTED: 49 programs over the whole subset in four contexts (round 3: element targets, every comparison shape, gcd in loop contexts), `NIFT_VM=require`; UNSUPPORTED: 57 programs (round 3: 12 more); equality memory (two 1M-element arrays and a 9,000-deep pair under a 50 MB budget: peak RSS ≤ budget + 8 MiB) | all supported run entirely on the VM with legacy's output; all unsupported fall back before execution with the expected reason; peak RSS ~37 MB (pre-fix build: 86 MB, fails) |
| **Supported-subset generator** (`vm_subset_gen.py --suite supported`, D-016 suite 1) | random programs built only from the specification, every context; feature census (76 required features must each be generated) | seed 1, 500 programs (`make test-rt-vm`): **467 vm (93.4 %), 33 deliberate replays** (runtime-error 20, aliasing 9, number-range 4 — each where the program ends in a documented run-time condition), **0 unexplained outcomes, 0 differences**; seed 3, 1,000: 936 vm + 64 deliberate, 0 failures; sanitized seed 2, 80: 76 + 4, 0 failures |
| **General generator** (`vm_gen.py` via `vm_parity.py`, D-016 suite 2; original distribution restored) | 300 programs + 300 mutants, legacy vs `NIFT_VM=1` | 0 differences. **Coverage (seed 1): 68 of 300 ran on the VM (22.7 %)**, 24 replayed (8.0 %: runtime-error 13, number-range 8, unverified 3), 208 fell back (69.3 %: `opaque:irregular-legacy-parse` 192 — mostly `?:`/`??` in conditions —, prepared-fn-unprepared 7, prepared-decl-type 7, static-error:arity 2). Seed 2 (sanitized, 150): 19.3 % VM. No threshold |
| **Unsupported generator** (`vm_subset_gen.py --suite unsupported`, D-016 suite 3) | one of 38 excluded constructs injected into a supported program in a random context (top, `if`, loop, function, loop-called function) | seed 1, 500 programs: 500 refused before execution with the expected reason, `execute_us` 0, legacy's output; seed 1, 600: same; sanitized seed 2, 80: same |
| `tests/runtime2/vm-corpus/cp11` | 78 scripts (round 3 adds `review3_f1_*`, `review3_f2_single_ok`), legacy goldens, expected outcome per script: 16 on the VM, 24 replay, 38 fall back | identical to legacy, every outcome as expected |
| `tests/runtime2/vm_adversarial.py` (seed 1, count 600; in `make test-rt-vm`) | 37,421 programs in 21 families (round 3: `elemtarget` 2,175, `cmpchain` 3,420, `deeptree` 20) | **0 non-approved differences**; 33 approved D-001#4; 0 endless. The three new families on the round-2 build (`3a3c0be`): 1,155 / 2,728 / 8 failures (8 crashes) |
| rt-diff, legacy vs `tools/rt-vm/nift-vm` | 813 entries | **0 differing**, 2 approved (D-001#4), 0 unused policies |
| `make test-rt-vm` | all of the above | PASS in 3 min 28 s |
| `make test-rt-vm-sanitize` | ASan+UBSan (no recovery, leak detection on, relocate-on-mutation reference check): unit tests, prepared-parser differential, corpus + 150 generated + 150 mutants, adversarial gate seed 2 sampled to ≤ 250 per family (3,619), **round 3 adds** `vm_subset.py` and both generated suites (80 each, seed 2) | clean, 0 non-approved differences, 34 min (`../evidence/cp11/sanitize.txt`) |
| Reviewer material, rounds 1–3 | round 1: case files `c1`–`c12`, kept `fy1`/`fz1`–`fz4`, `acorp` (1,222), `fuzz2`/`fuzz3` (1,000 each): 3,658 programs; round 2: nine repros, `mem`, generators `g_prep`, `g_dup`, `g_dup2`, `g_ctl`, `g_ws`, `g_big`, `g_bin`, `g_num`, `g_order`, `g_sanmix`, `g_mix`, `g_typed` × 4 seeds: 86,786; round 3: four repros, case files `t1`–`t22` (1,191), kept `diffs-*`, `deep/`, `mem/`, `gen3` × 15 seeds (22,500), `gp1` (36,288), `gprec1`/`gnum` × 2 seeds: 72,080 (`../evidence/cp11/reviewer-material-round3.txt`) | **0 non-approved differences**: 24 approved D-001#4, 42 outcome-only (deep/large and memory inputs whose legacy run takes minutes or crashes legacy: all fall back or compile as expected, none crashes the compiler) |
| Lint, certification guard | `tools/rt-lint.py` (all of `src/rt`, self-test); `tests/runtime2/certification_guard.sh` | 0 problems; PASS |
| `make test-rt-value`, `make test-rt-parser`, `make test-rt-value-sanitize`, `make test-runtime2-vm-corpus` | CP09/CP10 suites | pass (1,867,405 property checks; parser negative gate 1,957 programs 0 failures; paren oracle 0 divergent after the equality-chain probe was dropped, see "Reviews"; stale references abort under ASan; VM corpus 179 scripts) |

Defects found before the review by the CP11 differential tests (the review's
findings are under "Reviews"):
1. **Silent miscompile (fixed):** binding types were taken from values only;
   legacy also types bindings from the expression text (3 of the first 300
   generated programs: legacy failed "cannot assign string to double
   binding", the VM printed output). Fixed by porting `expression_type`.
2. A post-pass removed a `BindType` still read as a text type (bail only, no
   wrong output; caught by the corpus `EXPECT`; regression test added).
3. Quadratic `s += t` (rt-diff timeout on `str320.f`); fixed by in-place append.

### Coverage over the shared corpora (rt-diff script entries)

| Outcome | CP11 as reviewed (758) | integrated, before fixes (757) | after review round 1 (757 + 9) | after review round 2 (757 + 9 + 11) | **after review round 3** (757 + 9 + 11 + 3; + 6 new shared) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Ran on the VM | 79 | 78 | 78 (+1) | 75 (+1, +3) | **75** (+1, +3, +2; +0) |
| Replayed on legacy after a bail | 53 | 53 | 53 (+3) | 42 (+2, +1) | **41** (+2, +1, +0; +0) |
| Fell back before running | 626 | 626 | 626 (+5) | 640 (+6, +7) | **641** (+6, +7, +1; +6) |

Round 3 on the same 757 scripts: one replay (`escaped.f`, an extracted
smoke script with `a["x"] = []`, which replayed as an object index
assignment) is now refused (`index-target`); no script that ran on the VM
changed. The six new shared entries come from feature-sha256 (the rebase
onto `c4c5894`) and all fall back (`sha256`, `fn[async]`, `try`, …).
**Coverage metric (D-016):** the general generator runs 22.7 % of its
programs on the VM (seed 1, 300 programs; 8.0 % replay, 69.3 % fall back);
the supported-subset generator runs 93.4 % entirely on the VM and the rest
are its deliberate run-time conditions.

Round 1 cost no coverage on the shared corpora. Round 2 on the same 757
scripts: three `call-locals-*` probes move from VM to `prepared-decl-type`
(a loop-called function declaring from its parameter), `official-bfs.f` and
`loop_callee_error_location.f` change fallback reason, and the 10
`static-error` replays (plus `decl_types.f`, an `unverified` replay) become
static fallbacks. Round 1's corpus of its own regression scripts: one
replay (`review_n1`) became a fallback. The integration with the fixed CP10 parser
cost one script (`opaque`). Main fallback reasons: `fn-after-statement` 212,
`fn[async]`/`@fn` 55, `import` 53, `struct` 25, `inject` 24, `sort_by` 19,
Opaque 18, `map` 16+9, `try` 15, lambdas 14. Per-script list:
`../evidence/cp11/rt-diff-vm-outcomes.json`. Most real programs still run on
legacy; CP12–CP16 widen the subset.

On the reviewer's typed fuzz corpus (`fuzz2`/`fuzz3`, 1,000 programs each,
the review's seed scheme): before the fixes 154/226 completed on the VM,
220/302 replayed, 608/446 fell back (33/6 differing); after, 72/217 on the
VM, 96/319 replayed, 828/446 fell back (0 differing). `fuzz2` deliberately
generates zero literals and mid-block jumps, which are now refused.

## Performance

Re-measured after review round 3 on the final build (`c147f35`) with
`benchmarks/runtime2/vm_compare.py --runs 5 --slow` on an **idle** host
(nothing else running; round 2 was measured on a shared host). Instruction
counts (callgrind) are deterministic and gated. Raw data:
`../evidence/cp11/vm_compare.jsonl` (earlier rounds' files are in git
history). CPython 3.13.16, function-local code.

### Instructions per operation (marginal, N=4,000 vs 8,000)

| Operation | v4.12 | legacy (this branch) | VM round 1 | VM round 2 | **VM round 3** | CPython 3.13 | VM vs v4.12 | VM / CPython |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loop iteration | 3,066 | 3,129 | 104 | 98 | **98** | 397 | 31.3× fewer | 0.25× |
| Add-assign | 4,957 | 5,002 | 184 | 179 | **179** | 649 | 27.7× fewer | 0.28× |
| **Flat call** (gate) | 14,994 | 13,414 | 467 | 461 | **461** | 953 | **32.5× fewer** | **0.48×** |
| Array index | 5,356 | 5,401 | 380 | 381 | **381** | 703 | 14.1× fewer | 0.54× |
| Array push | 4,568 | 4,613 | 286 | 326 | **326** | 526 | 14.0× fewer | 0.62× |
| Object member | 5,240 | 5,303 | 347 | 348 | **348** | 806 | 15.1× fewer | 0.43× |
| String append (`t = t + "x"`) | 9,385 | 9,450 | 3,182 | 3,257 | **3,256** | 1,797 | 2.9× fewer | 1.81× |
| If + modulo | 6,006 | 6,050 | 233 | 223 | **223** | 829 | 26.9× fewer | 0.27× |
| fib call, top level (per call) | 184,321 | 159,234 | 465 | 471 | **471** | 457 | 391× fewer | 1.03× |
| fib call, inside a loop (per call) | 14,766 | 14,760 | 465 | 471 | **471** | 427 | 31.4× fewer | 1.10× |

Each "operation" includes the loop around it (`run.py` templates). CPython's
fib figure varies a lot between runs (300–710 per call over the runs so
far; hash randomisation changes its dictionary work); its flat-call figure
varied 947–980.

**Hot-path cost of round 3: none.** Round 3 changes only compile-time
checks and the out-of-line aggregate-equality walk; every VM figure above
is identical to round 2 (string append −1). Round 2's note follows.

**Hot-path cost of round 2.** No instruction was added to the bytecode of
these loops; the flat call, loop, add-assign and if operations got 5–10
instructions cheaper (the removed sampled frame-memory scan and smaller
`Vm::run`; flat call 467 → 461). Costs: **array push +40 (+14 %)** — every
push now recounts the array's footprint for the exact memory budget
(`heap_retally`) and then checks the budget (`memory_ok`); **string
append +75 (+2.4 %)** — concatenation allocates and is followed by the
budget check; fib +6 (+1.3 %) and array index +1 are code-generation
effects. The memory check is one thread-local load and compare after each
allocating slow path; the `Call` hot path is unchanged.

### Whole process (wall, median of 5; minimum in parentheses) and peak RSS

Idle host (round 3).

| Program | v4.12 | legacy | **VM** | CPython | Peak RSS KiB (v4.12 / legacy / VM / CPython) |
| --- | ---: | ---: | ---: | ---: | --- |
| fib(22) | 1,266 ms (1,200) | 1,058 ms (1,023) | **6.2 ms** (6.0) | 15.8 ms (15.2) | 7,276 / 7,912 / 5,620 / 8,692 |
| fib(25) | 5,354 ms (5,244) | 4,799 ms (4,515) | **15.6 ms** (15.4) | 22.9 ms (22.2) | 7,432 / 7,996 / 5,624 / 8,648 |
| 1M-iteration loop | 490 ms (478) | 483 ms (481) | **24.5 ms** (22.7) | 58.4 ms (57.8) | 6,724 / 7,232 / 5,472 / 8,636 |
| 200k calls | 341 ms (300) | 299 ms (279) | **11.4 ms** (11.1) | 27.5 ms (27.1) | 6,780 / 7,316 / 5,592 / 8,696 |
| Startup (`x := 1; print(x)`) | 3.6 ms (3.5) | 3.9 ms (3.6) | **3.7 ms** (3.5) | 15.7 ms (15.4) | 6,620 / 7,068 / 5,352 / 8,636 |

Peak RSS of the VM is within 0.3 MiB of rounds 1–2 (allocator variation).

### Startup, compilation and execution separated

| Program | Source bytes | Parse instr | Compile instr | Execute instr | Whole VM process instr | Legacy translation instr (reference) | Parse / compile / execute µs | Cold process | In-process: compile / first run / warm run µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | --- |
| startup | 16 | 5,551 | 78,338 | 389 | 2,574,506 | 4,197 | 19.6 / 35.9 / 6.3 | 3.39 ms | 59 / 5.3 / 0.2 |
| fib(22) | 84 | 21,893 | 111,395 | 26,966,032 | 29,592,105 | 15,401 | 22.0 / 54.4 / 2,888 | 6.1 ms | 75 / 2,716 / 2,808 |
| 1M loop | 61 | 20,291 | 95,980 | 179,000,743 | 181,608,699 | 11,290 | 23.3 / 49.9 / 22,758 | 26.9 ms | 73 / 22,922 / 24,898 |
| 200k calls | 98 | 28,676 | 125,743 | 92,200,737 | 94,847,812 | 17,325 | 26.9 / 59.6 / 7,731 | 14.4 ms | 86 / 8,038 / 7,800 |
| 500 functions (66 KB, generated) | 66,296 | 16,045,730 | 29,891,783 | 1,839 | 50,113,511 | 8,122,025 | 1,859 / 3,135 / 20.2 | 8.3 ms | 4,902 / 8.4 / 1.3 |

(Round 3, idle host; earlier rounds' values are in git history. Round 3
compile cost: +3k–8k instructions for small scripts and +2.1M (+7.5 %) for
the 500-function program — the iterative depth pre-pass and the iterative
prepared-context walks, which allocate an explicit stack per walk.)

- **Process startup** is ~2.5M instructions for both engines, almost all in
  the dynamic loader (symbol lookup and relocation of the 5 MB binary); the VM
  adds ~0.1M (parse + compile + atom table) for small scripts.
- **Parsing** is CP10's (its review fixes cost +10–15 % here: 5.1k → 5.5k
  instructions for the startup script, 14.4M → 16.0M for the large program).
- **Compilation.** The review checks first doubled the compile cost of a
  small script (72k → 168k instructions: the generated name table was a hash
  set built on first use); it is now a sorted constant array, the aliasing
  analysis runs only when a call passes a place argument, and a
  declaration's text type is inferred once: 72.5k for the startup script
  (was 72.1k), 97k for fib(22) (92k). The 500-function program compiles in
  20.8M instructions (was 17.4M: name-table lookups for every function,
  parameter and binding name, and the loop-typing check), 314 per source byte (parse 241/byte); legacy's
  up-front translation is 123/byte but it re-parses expression text on every
  evaluation. Its functions are now named `g<k>` (`f32`/`f64` are FFI type
  names in the generated table).
- **Compilation, round 2.** The prepared-context analysis (`prepared.cpp`:
  call-graph marking, loop regions, preparability of loop-called functions,
  string-argument typing) adds 3–20k instructions to small scripts (startup
  72.5k → 75.2k, fib(22) 97k → 107k, 200k calls 101k → 118k) and 7.0M to the
  500-function program (20.8M → 27.8M, 420 per source byte); a first version
  cost 15M there (std::function visitors, eager name sets) before the
  speed-up commit.
- **Cold vs warm.** Cold process time = startup + compile + first run. In
  process, later runs of the same compiled Program were 1–9 % faster than
  the first (1M loop 25.2 → 23.0 ms, 200k calls 8.2 → 8.2 ms, fib(22)
  3.0 → 2.9 ms) on the idle host; the 30 % reported before the review was
  load noise.

## Gate

Predefined gate (checkpoints.md CP11), measured with the `run.py ops`
methodology (marginal callgrind instructions per iteration, N=4,000 vs 8,000),
re-measured after review round 3 on the final build (`c147f35`, idle host)
with `NIFT_VM=require` (genuine VM execution; a fallback or bail would be
an error):

| Criterion | Required | Measured | Result |
| --- | --- | --- | --- |
| Flat call ≥ 5× fewer instructions than v4.12 | ≤ 2,994 (v4.12 14,994 here; CP00 14,970) | **461** (32.5× fewer; round 2: 461, round 1: 467, as reviewed: 464) | **met** |
| Flat call ≤ 2× CPython function-local | ≤ 1,906 (2 × 953 this run; CP00: ≤ 1,980) | **461** (0.48×) | **met** |
| Parity on every compiled unit | identical stdout/stderr/exit (approved D-001#4 recursion budget only) | As reviewed: **not met** (round 1 found seven silent miscompiles the corpora did not exercise). After the fixes: 0 non-approved differences over rt-diff 793 entries, VM corpus 64, 600 generated/mutants, the adversarial gate (19,969 programs, seed 1; 1,912 sanitized, seed 2) and the reviewer's own programs | met after round 1; round 2 found three more miscompile classes (M1–M3) and a parser byte class (M4) the gate did not exercise; after round 2: 0 non-approved differences over rt-diff 804 entries, VM corpus 75, 600 generated/mutants, the adversarial gate (31,792 programs in 18 families, seed 1; 3,099 sanitized, seed 2), the subset suites and both rounds' reviewer material; round 3 found one more in-subset miscompile (F1), an admitted out-of-subset shape (F2), a compile-time crash (F3) and an uncounted allocation (F4); after round 3: 0 non-approved differences over rt-diff 813 entries, VM corpus 78, 600 generated/mutants, the adversarial gate (37,421 programs in 21 families), the three D-016 suites and all three rounds' reviewer material | **met after fixes** (certification pending) |

**Verdict: GO** (unchanged after round 3; certification pending). Recursive calls (fib) cost
471 instructions per call, near CPython (366–710 per call over the runs);
loops are 31× cheaper than v4.12 and ~4× cheaper than CPython.

### Where the remaining time goes (flat call; profile from round 1 at 467 instructions/iteration, now 461)
`callgrind_annotate` (`../evidence/cp11/profile-call.txt`, round 1): `Vm::run` 420,
`Value` move-assignment 26, `Value::release` 14, `Value::integer` 7 per
iteration (before the review: 410 / 26 / 21 / `reserve_registers` 7). Per
iteration the VM executes 11 instructions in the caller loop (compare-jump,
two argument moves with parameter-type checks, call, type check, move,
`AddI`, type check, jump) and 2 in `add` (add, return). Further easy wins,
not needed for the gate: dropping argument moves when the argument is
already in place, a specialised `Return` that skips register clearing for
frames with no heap values, and a non-refcounting store for scalar results.

## Risks
- **Coverage.** 75 of the 757 shared-corpus scripts run on the VM (78
  after round 1; 79 of 758 before the CP10 integration; unchanged by round
  3); the general generator runs 22.7 % of its programs on the VM (the
  D-016 coverage metric); most real programs need constructs from CP12–CP16. Round 2 refuses three more shared scripts
  (`call-locals-*`: a loop-called function declaring `a := x + 1` from its
  parameter) and turns 10 static-error replays into fallbacks. The owner
  accepted a narrower subset (round 2 direction). The performance
  evidence is for the subset only; template, filesystem and process code is
  untouched.
- **Replay** costs time on programs that bail late (the VM work is wasted),
  and output appears at the end of a VM run instead of progressively (opt-in
  only). Since review D7 a replay is always exact: nothing is committed
  before completion, and a program printing more than 64 MiB is stopped at
  the limit and replayed on legacy from the start (cost: the buffer memory
  and the VM time until the limit; such programs effectively run on legacy).
- **Parity evidence is differential.** Three review rounds found miscompile
  classes the corpora and generators did not exercise (round 1: seven;
  round 2: three — M1 typing against preparation-time bindings, M2 double
  argument evaluation, M3 text-typed string arguments — plus a parser byte
  class; round 3: F1 member-path element targets, F2 equality chains). The
  round-3 supported-subset generator is the first suite built from the
  written specification rather than from found shapes; it is still a
  generator, not a proof.
- **CP10 equality-chain grouping (open, CP10/CP12).** Outside conditions the
  CP10 parser groups `a != b == c` differently from legacy (the paren oracle
  reports it); the VM refuses every comparison chain, so no unit runs with
  that tree, but the parser itself still produces it. Recorded for the
  parser/language checkpoint; the round-3 corpus probe was dropped from the
  oracle corpus for that reason (as round 1's `!x + 1` probe). The round-1 record wrongly called M1 a "residual case": it was a
  miscompile class (303 of 4,800 targeted programs differed).
- **Legacy context-dependent semantics.** Legacy's second (prepared)
  evaluator is now inventoried from source ("Legacy's prepared
  evaluator": eleven differences), not only found by testing; each is
  refused statically or reduced to an enumerated value-dependent replay. The
  inventory covers the constructs of the compiled subset; widening the
  subset (CP12+) must extend it. The refusals over-approximate where the
  prepared path runs (every loop, every function reachable from one).
- CP09: reference safety is enforced in sanitizer builds (relocate on
  mutation; the VM's uses were audited, see `cp09-value-model.md`), equality
  and hashing are iterative; `hash_number` inconsistency is unused here.
- CP10's review fixes are integrated (`runtime-2` @`4287adb`); the CP11
  compiler checks for the same shapes remain as a second line.
- **Build gates now see `src/rt`.** Linking `src/rt` into `nift` puts its
  files in the zero-warning gate (`test-warnings`) and the sanitizer/TSan
  builds. Not re-run for this round: `make -k test`/`make certify` on the
  integrated branch (left to certification).
- **Memory budget.** Exact accounting of the rt heap (64-byte granules,
  object and owned capacities) plus the VM stacks, checked after every
  allocating instruction, so one instruction can overshoot the budget by at
  most its own allocation (e.g. one string concatenation). It does not
  include allocator overhead, the 64 MiB output buffer, the parse tree or
  the program. Exit 70 is guaranteed only if the host can actually provide
  the budget (1 GiB); with less free memory (or a tighter cgroup) the kernel
  can still kill the process first. A non-recursive program that legitimately
  needs more than 1 GiB of rt heap gets a reported failure where legacy
  might succeed (legacy uses more memory per value, so this is unlikely).
  Since round 3 (F4) aggregate equality needs O(depth) working memory
  (≤ 240 KiB at the 10,000-level equality depth), not O(elements); it is a
  fixed allowance like the stacks of the process, not counted.
- **Deep equality.** `==` deeper than 10,000 levels bails and legacy
  decides (correct but slow: legacy builds such values in quadratic time);
  beyond about 50,000 levels legacy itself crashes, and so does the replay.
- **Name table breadth.** The generated table is deliberately broad (it also
  contains FFI type names such as `f32`, `i8`, metadata keys and keywords);
  refusing a harmless name costs coverage only. A name legacy dispatches
  through a context the extractor does not scan would be missed; the names
  family of the adversarial gate probes every identifier literal of the
  legacy source in seven roles to catch that.
- Not verified: macOS/Windows (D-004); the `switch` dispatch is compiled
  only as a syntax check (`-DNIFT_RT_VM_SWITCH`); `make certify` on the
  integrated branch.

## Deliverables
Code, tests and tools listed under Scope; Makefile targets `test-rt-vm`
(in `make test` and the certification guard REQUIRED list),
`test-rt-vm-sanitize`, `bench-rt-vm`; evidence in `../evidence/cp11/`.

## Commit strategy
Local commits, not pushed. Original CP11 on `runtime-2-cp11` (from
`c1e3aac`): VM and integration (`18fec73`); tests (`82f420d`); call-path and
admission fixes (`ffc8fc9`); a codegen fix (`25089e8`); benchmark and
evidence (`d1a6e0c`); lint, oracle link and corpus fixes (`566ea74`,
`8d4b522`, `5cf7a6e`); record. Cherry-picked onto `runtime-2` @`4287adb` as
`runtime-2-cp11i` (`54b26d7` … `0a0d43f`), then review round 1: integration
(`38fd2df`), fixes (`d6d0d2b`), rt-diff class policies (`ca41053`), gate and
regression tests (`2e2a4d7`, `d7020a7`), hot-path and compile-cost follow-ups
(`8e10a3a`, `84d9332`), this record and evidence. Review round 2: CP10 follow-up (`26aee9a`), CP09 follow-up (`72c00ac`),
fixes and tests (`1ff0063`), generator adjustment (`52d0e1a`), analysis
speed-up (`b2183e4`), this record and evidence. Review round 3 (rebased on
`runtime-2` @`c4c5894`): revert of the generator adjustment (`3857cf3`,
D-016), legacy name table regenerated for feature-sha256 (`568e02f`), CP09
follow-up F4 (`ba0deb6`), F1–F3 fixes and tests (`1314bd1`), the generated
suites (`8f1e55d`), corpus probe dropped (`c147f35`), this record, D-016 and
evidence.

## Evidence
`../evidence/cp11/README.md`.

## Reviews

### Independent review, round 1 (2026-10-18)
The review found seven silent miscompiles (every one reached outcome `vm`,
i.e. the VM printed a different result with exit 0) and two risks. The
original parity claim ("0 differences") was therefore wrong: the shared
corpora and the CP11 generator did not exercise these shapes. Owner
guidance for the fixes: refuse to compile when legacy meaning is uncertain;
prefer static refusal to run-time replay; replay only enumerated,
side-effect-free bails; report unexpected VM failures instead of replaying
them; do not introduce compatibility changes (D2 and D4 are recorded by the
owner as candidate deliberate changes (D-014), not implemented here).

| # | Finding | Status | Fix (exact / refused) | Tests |
| --- | --- | --- | --- | --- |
| D1 | Zero decimal literals (`0.0`, `0.00`, `0.000`, `0.0e0`, `0.0e5`) keep their spelling in legacy (JSON StrNumber; negation `-0.0`); the VM printed `0` | fixed | **refused** (static): every zero-valued decimal spelling, and exponents of magnitude ≥ 300 (same JSON rule: `1.0e-300` keeps its spelling) → `number-literal`. Computed zeros (`0.5 - 0.5`) are plain doubles in legacy too and stay compiled | unit (5 fallbacks, 1 output), corpus `review_d1_zero_literals.f`, adversarial `numeric` (39 spellings × 19 uses × 4 contexts) and `typed` (zero literals in the generator) |
| D2 | A statement after `break`/`continue` in the same block: legacy ignores the jump and runs the rest | fixed | **refused** (static): a block whose `break`/`continue` is not its last statement → `jump-not-last` (candidate change D-014, owner) | unit (2), corpus `review_d2_jump_not_last.f`, adversarial `control` (jumps at every position, nested) and `typed` (fuzz2 mid-block jumps) |
| D3 | `.size()` on a string: legacy error, VM returned the length | fixed | receiver audit of every compiled method and operator (strings, numbers, bools, null, arrays, objects; top level, loop body, functions called from either): `size()` only on arrays (objects replay), `length()` on strings and arrays (objects replay: legacy accepts it only in loop bodies); other receivers are legacy errors → **replay** (`runtime-error`); `push` arrays only, one argument (unchanged). Index/member/operator behaviour per receiver was already exact (matrix: 0 differences) | unit (5 bails, 1 output), corpus `review_d3_string_size.f`, adversarial `receiver` (14 receivers × 34 operations × 4 contexts) |
| D4 | User functions named like legacy built-ins (`is_null`, …, `is_enum`): legacy built-in wins | fixed | **refused** (static): `legacy_names.cpp`, generated from the legacy evaluator source (206 names; stale table fails `make test-rt-vm`); function, parameter and binding names in it → `fn-name-reserved` / `param-name-reserved` / `binding-name-reserved` (candidate change D-014, owner) | unit (names table, 3 fallbacks), corpus `review_d4_builtin_name.f`, adversarial `names` (every identifier literal of the legacy source + a word list, 7 templates: 3,801 programs) |
| D5 | Bindings named `name`, `title`, `loop` (`reserved_binding_name`): legacy error | fixed | **refused** (static): same generated table (it includes `reserved_binding_name`'s list) | unit (2), corpus `review_d5_reserved_binding.f`, adversarial `names` |
| D6 | Aggregate argument from a member/index (`f(o.k)`, `f(a[0])`) is aliased in legacy; the VM copied | fixed | **refused** (static) when the callee may mutate or rebind the parameter (`alias.cpp`, transitive) → `alias-argument`; read-only callees compile (exact: no mutation, so copy = alias). Audit of the other places (declarations, returns, pushes, literals from places, parenthesised and `?:`/`??` forms): already exact (NoAlias/Return bails; `?:`/`??` copy in legacy too) | unit (3 fallbacks, 1 output), corpus `review_d6_alias_*.f`, adversarial `alias` (9 argument forms × 9 callee bodies × 2 contexts + 9 declaration forms × 3 mutations × 3 contexts) |
| D7 | Output > 64 MiB then a bail: VM committed partial output and exited 1 where legacy succeeded | fixed | exact: under `NIFT_VM=1` nothing is committed before completion; at the limit the run stops (`output-limit`) and legacy **replays** from the start. Cost: ≤ 64 MiB buffer; the VM work up to the limit is wasted | unit (limit without commit), adversarial `output` (`NIFT_VM_OUTPUT_LIMIT=4096`: 18 programs ending normally, in an error, a number-range bail, the recursion budget, a top-level return; and one 70 MiB run past the real limit ending in an error) |
| R1 | Memory blow-up before the recursion bail; uncaught `std::bad_alloc` | fixed | register stack capped; frame-held bytes measured past depth 64 (geometric) → `recursion-budget` before memory runs out (approved D-001#4 applies); `std::bad_alloc`, any C++ exception and internal VM errors are **reported** as `NIFT_VM internal failure` (exit 70), never replayed (owner directive) | unit (frame-memory and register budgets), corpus `review_r1_frame_memory.f` (the review's growing-string recursion), adversarial `recursion` |
| R3 | The approved recursion difference dominated parity triage | fixed | automatic classification as approved D-001#4 in `vm_adversarial.py`/`vm_parity.py` (statistics must show `beyond_legacy_depth`) and an rt-diff class policy | adversarial `recursion`, rt-diff self-test |
| N1 | (found by the new `typing`/`context` generators) Legacy loop bodies type `b := a` (text referring to another binding) from the value: `a := 2.0; b := a; b = 2.5` fails in a loop, succeeds at top level; the VM succeeded in both | fixed | such declarations check that the text-derived and value-derived types agree, else **replay** (`unverified`); a static refusal is impossible (it depends on whether the value is integral and on the caller's context) | unit (1 bail), corpus `review_n1_loop_decl_type.f`, adversarial `typing` (9 × 12 × 10 × 4 = 4,320 programs; 138 differences before the fix, 0 after) and `context` |

Integration with the fixed CP10 parser (`runtime-2` @`4287adb`): four unit
fallback expectations and two corpus `EXPECT` reasons changed from CP11
compiler reasons to Opaque classes (whitespace before postfix, trailing
commas, duplicate object keys → `opaque:legacy-runtime-error`; `!x + 1` →
`opaque:irregular-legacy-parse`). Each was checked against legacy and is
correct and at least as safe (the parser refuses first; the compiler checks
remain as a second line).

Standing tests (so these classes cannot regress): `make test-rt-vm` now runs
`tests/runtime2/vm_adversarial.py` (seed 1, ~20,000 programs, see "Tests")
and `tools/rt-vm/legacy_names.py --check`. Mutation check of the gate: run
(count 100, before the `typing` family existed) against the pre-fix
integrated build it reports differences in every family
(typed 25, context 68, numeric 232, receiver 12, names 19, alias 26,
control 56, output 3, malformed 1; recursion 27 unlabelled because that
build has no `beyond_legacy_depth`).

### Independent review, round 2 (2026-10-18)
The round showed that legacy's second evaluator (loop bodies and the
functions called from them are *prepared*) had been handled finding by
finding. Owner direction for the fixes: legacy is an oracle for finding
differences, not the authority on semantics; narrow the subset instead of
modelling legacy's quirks; refuse statically wherever the uncertainty is
detectable before execution; do not expand replay; keep the gate
benchmarks compiling; record the old behaviour of every difference and leave
the intended language behaviour to the owner (candidates: arguments
evaluated exactly once, types from values not text, consistent declarations
inside and outside loops, conventional `break`, consistent built-in
resolution, explicit aliasing rules). The class was fixed from the source:
"Legacy's prepared evaluator" above lists all eleven differences and how
each is handled.

| # | Finding | Status | Fix (exact / refused) | Tests |
| --- | --- | --- | --- | --- |
| M1 | DEFECT (miscompile class; the round-1 record wrongly called it a residual): loop bodies and loop-called functions type `b := a` / `(a)` against the `a` visible when legacy prepares them (a global, an enclosing scope, the caller's scope), not the one that runs; 303/4,800 targeted programs differed | fixed | **refused** `prepared-decl-type` (P1): any binding-referring declaration in a function reachable from a loop; in other loops one referring to a name declared inside the outermost enclosing loop. Elsewhere exact (same binding at the header and at run time) | unit (4), corpus `review2_m1_*.f` (2 refused, 1 compiled), adversarial `preptype` (8,064 programs; 303 differences on the pre-fix build, 0 after), `shadow`, subset suites |
| N1 | (round-1 replay) | replaced | the run-time check (`CheckParam` after `BindType`) is removed; the shape is refused with M1 | corpus `review_n1_loop_decl_type.f` now `fallback prepared-decl-type` |
| M2 | DEFECT: a loop-called function whose `return`/`if`/`while` text legacy's prepared parser rejects (or with a bare `return`) has its arguments evaluated twice (nested: 4×) | fixed | **refused** `prepared-fn-unprepared` (P2/P3): every function reachable from a loop body or condition (call-graph fixpoint) whose body legacy cannot prepare; acceptance by `rt/prepared_parse.cpp`, an exact port of `src/Ast.cpp`'s parser | `vm_prepared_parse` (100,136 texts vs `nift::ast::parse_expression`: 0 differences; a seeded port mutation gives 1,323), unit (4), corpus `review2_m2_dup_arg_eval.f`, adversarial `dupeval` (984; 114 differences before, 0 after) |
| M3 | DEFECT: a string-literal argument is typed from its unquoted text (`f("q")` with `q := null` → null binding; `f("2.5")` → double binding) | fixed | **refused** `string-arg-type` (P5) when the unquoted text types as a double or names a binding that can be in legacy's scope chain at that call (globals; the calling function and every function that can be active below it). Other string literals type as strings on both legacy paths, as on the VM | unit (3), corpus `review2_m3_*.f` (2 refused, 1 compiled), adversarial `strarg` (1,071; 128 before, 0 after) |
| M4 | DEFECT (CP10): `\f`/`\v` inside a statement are whitespace to the rt lexer, malformed to legacy | fixed | **Opaque** in the CP10 lexer (separate commit "CP10 follow-up"; `cp10-parser.md` Reviews). Conservative inside loop bodies, where legacy's prepared parser accepts them | parser unit, `legacy-cases/c3_*.f`, negative gate whitespace set, corpus `review2_m4_*.f`, adversarial `bytes` (437; 38 before, 0 after; CR, NEL, NBSP, U+2028/9, BOM, NUL, DEL, C0 kept as tests) |
| R5 | D-013 violation: the frame-memory budget sampled two levels deep; `[[s]]` per frame evaded it and the process was OOM-killed | fixed (exact) | CP09 `heap_account` (exact live rt heap, any nesting) + VM stacks, checked after every allocating instruction; deep → `recursion-budget` bail, otherwise reported internal failure (exit 70) | unit (budgets, hidden nested memory, flat overflow → Internal), corpus `review2_r5_hidden_frame_memory.f` (stops at ~2,600 frames, legacy replays), adversarial `memory` (13, with `NIFT_VM_HEAP_BUDGET=2 MiB`: every holder shape must end in a recursion-budget bail; the pre-fix build completes 12 of them) |
| R6 | RISK: `rt::equal` recursion segfaulted on ~50,000-deep arrays | fixed | CP09: iterative `equal`/`hash`, `equal_within` (legacy visiting order); VM bails `nesting-depth` beyond 10,000 levels so legacy decides; CP09 bridge bounded at 512 levels | `value_round2` (300,000 deep), unit (9,999 decided / 10,001 bails), corpus `review2_r6_deep_equality.f`, adversarial `deepeq` (54, `NIFT_VM_EQUALITY_DEPTH=50`) |
| DOC | M1 called a residual; `NIFT_VM_STATS`/`DUMP` behaviour; memory-budget claim | fixed | this record: M1 is a miscompile class; STATS/DUMP are test tooling written to stderr before a replay; memory budget text states what is and is not guaranteed | — |

Also in this round: statically known errors (redeclaration, arity, member
assignment, push arity) are refused instead of replayed
(`BailReason::StaticError` removed); `vm_subset.py` adds the SUPPORTED and
UNSUPPORTED suites; the CP09 reference contract is enforced in sanitizer
builds (owner item 6, `cp09-value-model.md`).

Replay reasons that remain after round 2 (all side-effect free: the
subset's only effect is buffered output): `runtime-error` (an error
depends on values; legacy prints its own diagnostic), `number-range` (a
computed number reaching 2^53 or −0), `aliasing` (whether a bound or
returned place holds an aggregate), `unverified` (P4/P5: text and value
types disagree for this value — e.g. an integral double —; an array element
of another type; `length()`/`size()` on an object), `recursion-budget`,
`output-limit`, `nesting-depth` (resource bounds). (Round 3, DOC-1: the
original sentence said each is "not decidable before execution"; that was
too strong — some instances are decidable from literals, see "Opt-in and
fallback".)

### Independent review, round 3 (2026-10-18)
The last review round (D-015: no further open-ended compatibility cycle).
Owner rules for the fixes: no silent miscompiles; prefer compile-time
refusal (narrow the subset) over machinery; no new replay; the flat-call
gate re-measured on the final build; no benchmark special-casing; no test
weakened. The owner also restructured the generated tests (D-016).

| # | Finding | Status | Fix (exact / refused) | Tests |
| --- | --- | --- | --- | --- |
| F1 | DEFECT (in-subset miscompile): an element assignment whose index text contains `.` (`a[a.length() - 1] = 9`, `a[o.k] = …`, `a[1.0] = …`, `a[g("x.y")] = …`) is a member assignment to legacy's textual evaluator, which fails ("member assignment requires a struct instance"); the VM succeeded | fixed | **refused** `index-target`: element assignment compiles only when the brackets hold exactly a local name or an unsigned integer literal (no parentheses, no `.` anywhere in the target); compound and `++`/`--` element targets were already refused (`compound-target`, `incdec-target`). The former `index-assign-order` check is subsumed | unit (6 fallbacks, admitted forms run), `vm_subset` (SUPPORTED `element_targets`; UNSUPPORTED 9), corpus `review3_f1_*.f` (1 refused, 1 compiled), adversarial `elemtarget` (33 indexes × 11 values × 5 contexts, plus compound/`++` forms: 2,175 programs; on the pre-fix build 1,155 not refused, 0 after) |
| F2 | DEFECT (outside the documented subset but admitted): `1 != 1 == 1` and other equality chains compiled with a different grouping (the CP10 tree groups `==`/`!=` as one right-associative level; legacy splits at the first `==`) | fixed | **refused** `comparison-chain`: every comparison whose operand is itself a comparison (equality or relational, either side, any context, parenthesised too — parentheses leave no node). "comparisons (no chains)" in the specification is now true | unit (4), `vm_subset` UNSUPPORTED (3), corpus `review3_f2_single_ok.f` (compiled; the refused probe was dropped from the corpus because the CP10 paren oracle reports exactly this grouping difference, see Risks), adversarial `cmpchain` (exhaustive 6 × 6 operators × flat/left/right × 4 operand sets × 8 contexts = 3,240 refused + 180 single-comparison controls that run on the VM; pre-fix build: 2,728 compiled) |
| F3 | CRASH: `print(1 + 1 + … + 1)` with ≥ 70,000 terms segfaulted in `compile_unit` (recursive `outermost_loops`/`walk` in `prepared.cpp`) before the expression-nesting limit applied | fixed | `compile_unit` first bounds the unit's tree depth (256 nodes, iterative walk; `nesting-limit`); the prepared-context walks, `outermost_loops` and the Opaque search are iterative; every other recursive pass is then bounded by the depth limit. Legacy runs such units | unit (ten 100,000-deep inputs compiled on a 512 KiB stack; a 50,000-statement list still compiles), `vm_subset` UNSUPPORTED `nesting-limit`, adversarial `deeptree` (18 deep shapes: `+ - && \|\| ?? ==` chains, parentheses, brackets, unary, calls, index chains, deep `if`/`while`, in functions and loops; flat controls), sanitizer run; pre-fix build: 8 crashes |
| F4 | RISK (memory budget): aggregate `==`/`!=` built an uncounted work list of every element pair (24 B per element; 73 MB peak against a 50 MB budget) | fixed (CP09 follow-up) | `equal_within` keeps one frame per open nesting level: O(depth) working memory, at most `NIFT_VM_EQUALITY_DEPTH` + 1 frames (≤ 240 KiB at the default 10,000), never O(elements); order and the TooDeep boundary unchanged | `vm_subset` equality-memory checks (two 1M-element arrays and a 9,000-deep pair under `NIFT_VM_HEAP_BUDGET=50,000,000`: peak RSS ≤ budget + 8 MiB; pre-fix build 86 MB, now ~37 MB), CP09 value suites, adversarial `deepeq` |
| DOC-1 | The record said every replay reason is decided by a run-time value; several are decidable from literals | fixed (wording) | the literal-decidable instances are listed precisely ("Opt-in and fallback"); they are not refused (no constant evaluation in CP11); replay stays side-effect free (D-013) and is removed in CP12 | — |
| DOC-2 | "comparisons (no chains)" was not true | fixed with F2 | specification and table updated | — |

Also in this round: the rebase onto `runtime-2` @`c4c5894` (feature-sha256)
made the generated legacy name table stale (`sha256`, `sha256_file`); the
`--check` guard in `make test-rt-vm` caught it and the table was regenerated
(208 names). The generated supported-subset suite found two imprecisions in
the specification text (not compiler defects): `?:`, `??` and object
literals inside `if`/`while` conditions are CP10 Opaque, and `s += <number>`
is a legacy error (a documented `runtime-error` replay); both are now stated.

Owner-requested test restructuring (D-016): the round-2 change to
`vm_gen.py` is reverted and its 20 % threshold removed; three generated
suites are kept apart — supported subset (correctness), general generator
(coverage, reported) and unsupported (fallback). Results under "Tests".
