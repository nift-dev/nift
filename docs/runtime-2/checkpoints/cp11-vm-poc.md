# CP11 — VM proof of concept (GO/NO-GO)

Status: see `../progress.md` (implemented, review round 1 fixed, under
re-review). **Gate verdict: GO** (see "Gate", re-measured after the review
fixes), with the risks and limits listed below. Integrated on `runtime-2`
@`4287adb` (CP09 + CP10 certified with their review fixes) as branch
`runtime-2-cp11i`.

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
  legacy evaluator source by `tools/rt-vm/legacy_names.py`); all lint-clean.
  `src/rt` is now linked into `nift` (`RT_LINK_SOURCES` in the Makefile).
- `src/CLI.cpp`: one hook in `run_script_source` (file, `-e`/`-c`, stdin
  scripts; not when an interactive shell follows, only the native target).
- Tests and tools: `tests/rt/vm_unit.cpp`, `tests/rt/vm_run.cpp`,
  `tests/rt/rt_vm_tool.h`, `tests/runtime2/vm-corpus/cp11/` (64 scripts +
  `EXPECT`), `tests/runtime2/vm_gen.py`, `tests/runtime2/vm_parity.py`,
  `tests/runtime2/vm_adversarial.py` + `vm_adversarial_gen.py` (review round
  1), `tools/rt-vm/nift-vm`, `tools/rt-vm/legacy_names.py`,
  `tools/rt-diff/policies/cp11-vm.json` (plus class policies in
  `tools/rt-diff/rt-diff`), `benchmarks/runtime2/vm_compare.py`.
- CP09/CP10 files are unchanged. All of `src/rt` is linted (CP10 is
  lint-clean since its review).

## Design

### Opt-in and fallback
| Setting | Behaviour |
| --- | --- |
| `NIFT_VM` unset or `0` | Nothing new runs (one `getenv`); `nift` behaves exactly as before |
| `NIFT_VM=1` | Eligible scripts run on the VM; everything else, and every run-time bail, runs on legacy unchanged |
| `NIFT_VM=require` | Never uses legacy: an ineligible script or a bail is an error (`error: <file>:<l>:<c>: …`, exit 1). Used for every VM measurement |
| `NIFT_VM=classify` | Test tooling: parse and compile only, report `compiled`/`fallback` (with `NIFT_VM_STATS`), run nothing |
| `NIFT_VM_STATS=1` / `=<path>` | One JSON line per script (stderr / appended to the file): outcome, fallback or bail reason, position, parse/compile/execute µs, protos, instructions, `beyond_legacy_depth` |
| `NIFT_VM_DUMP=1` | Bytecode listing on stderr |
| `NIFT_VM_OUTPUT_LIMIT=<bytes>` | Test-only: lowers the 64 MiB output buffer limit (D7 regression tests) |

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
   Bail reasons (an enumerated set; each is a legacy behaviour the VM
   deliberately does not reproduce): `runtime-error` (anything legacy
   reports as an error), `number-range` (integer or double at or beyond 2^53,
   integer −0; see "Numbers"), `aliasing` (an aggregate bound from a place,
   where legacy makes a location reference), `static-error` (a statement the
   compiler knows errors, e.g. redeclaration, wrong argument count),
   `unverified` (a case whose legacy result the VM does not claim, e.g.
   assigning a different type to an array element, or a binding type the two
   legacy paths derive differently), `recursion-budget` (frame, register or
   frame-memory budget), `output-limit`.
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
- Memory (review R1): the register stack is capped at 16M values (256 MiB);
  past legacy's 64-frame limit the bytes held by live registers (strings,
  array/object elements two levels deep, shared objects counted per
  reference) are measured at geometrically spaced depths (every +12.5 %);
  beyond 256 MiB the run stops with `recursion-budget`. Every such stop
  happens deeper than legacy can go, so the approved behaviour applies
  (legacy replays and reports its recursion error) before memory runs out.
  The check is the slow branch of the frame-budget compare that `Call`
  already made: no new hot-path test.

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
  bail (legacy types parameters from argument text). Legacy loop bodies (and
  functions called from them) type a declaration whose text refers to
  another binding (`b := a`, `b := a * 2`) from the value alone, top level
  from the referenced binding's type; such declarations check that both
  readings agree and bail otherwise (review finding N1).
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
| `a[i] = v` (array, existing element, same type) | ✓ (else bail) | object index assignment (bail), member assignment (bail), calls in both index and value |
| `+ - * / %`, comparisons, `&&` `\|\|` `!` `??`, `?:` in parentheses, unary `-` `+` | ✓ | `!` ending the left side of an operator (legacy reads `!x + 1` as `!(x + 1)`) |
| `if` / `else if` / `else`, `while`, `break`, `continue` as the last statement of their block | ✓ | `for`, `try`/`catch`, `throw`; a `break`/`continue` followed by another statement in its block (legacy ignores the jump; D2, candidate change D-013) |
| `fn` declarations (top level, before the first other statement), calls, recursion, `return` (also at top level) | ✓ | `fn[async]`, `@fn`, nested functions, lambdas, struct/enum/import/export, functions referring to non-local names (dynamic scope, CP12); function, parameter or binding names in the generated legacy name table (D4/D5); a member/index argument to a parameter the callee may mutate (D6) |
| Arrays/objects: literals, `a[i]`, `o.k`, `o["k"]`, `.push(x)` on arrays, `.length()` on strings/arrays, `.size()` on arrays | ✓ (other receivers replay) | every other method or built-in (`sort_by`, `map`, `stringify`, `split`, …), `?.`, `?[`, spread, postfix on literals, whitespace before postfix, trailing commas, duplicate object keys |
| `print(x)` | ✓ | every other native; bare expression statements other than calls |

## Tests

| Suite | Content | Result |
| --- | --- | --- |
Results after review round 1 on the integrated branch (`runtime-2-cp11i`).

| Suite | Content | Result |
| --- | --- | --- |
| `tests/rt/vm_unit.cpp` | outputs (25 programs), fallback reasons (33), bail reasons (18), literal decoding, Int rendering = legacy `dump` (20,000 values), frame budget (99,990 deep OK, 100,000 bails, configurable), frame-memory and register budgets, 90,000 nested calls on a 256 KiB stack, output limit (flush only with a hook; otherwise stop without commit), generated name table, code shape | 40,206 checks, 0 failures |
| `tests/runtime2/vm-corpus/cp11` | 64 scripts (9 review regressions), legacy goldens (also checked by `test-runtime2-vm-corpus`), expected VM outcome per script: 11 run on the VM, 28 replay, 25 fall back | identical to legacy, every outcome as expected; the VM scripts also identical under `NIFT_VM=require` |
| `tests/runtime2/vm_adversarial.py` (seed 1, count 600; in `make test-rt-vm`) | 19,969 programs in 11 families (see Reviews and the generator docstring): 7,941 completed on the VM, 6,744 replayed, 5,283 static fallbacks (classified with `NIFT_VM=classify`, legacy by construction) | **0 non-approved differences**; 31 approved D-001#4 (recursion budget, confirmed by the statistics); 0 endless |
| Generated programs (`vm_gen.py`) + mutants, seed 1 | 300 + 300: since the CP10 review fixes 192/173 are Opaque (mostly `?:` in conditions), 76/41 complete on the VM | 0 differences |
| rt-diff, legacy vs `tools/rt-vm/nift-vm` | `vm-corpus`, `semantics`, `rt-diff-corpus`, conformance cases, cp410 probes, extracted smoke scripts, 200 `gen.py` programs: 793 entries | **0 differing**, 2 approved (`deep_recursion.f`, `recursion_limit.f`: D-001#4), 0 unused policies |
| `make test-rt-vm` | all of the above | PASS in 2 min 46 s (idle 2-vCPU host) |
| `make test-rt-vm-sanitize` | ASan+UBSan (no recovery, leak detection on): unit tests; corpus + 150 generated + 150 mutants; adversarial gate seed 2, every family sampled to ≤ 250 programs (1,912), on `nift-sanitize` | clean, 0 non-approved differences (`../evidence/cp11/sanitize.txt`; 19 min) |
| Reviewer's material | kept divergent programs (`fy1/` 17, `fz4/` 86) and the twelve case files `c1`–`c12` (307 cases) through the reviewer's comparison | all identical to legacy or approved (8 recursion-budget); `fuzz2`/`fuzz3` 1,000 programs each: 0 non-approved differences (before: 33 and 6 differing, including unlabelled recursion-budget cases) |
| Lint (`tools/rt-lint.py`) | all of `src/rt` | 0 problems |
| `make test-rt-value`, `make test-rt-parser` | CP09/CP10 suites on the integrated branch | pass (1,867,405 property checks; parser negative gate 0 failures, paren oracle 0 divergent) |

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

| Outcome | CP11 as reviewed (758) | integrated, before fixes (757) | after review round 1 (757 + 9 new regression scripts) |
| --- | ---: | ---: | ---: |
| Ran on the VM | 79 | 78 | 78 (+1) |
| Replayed on legacy after a bail | 53 | 53 | 53 (+3) |
| Fell back before running | 626 | 626 | 626 (+5) |

The review fixes cost no coverage on the shared corpora (the same 757
scripts have the same outcome classes; within replays one script moved from
`static-error` to `unverified`). The integration with the fixed CP10 parser
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

Re-measured after review round 1 at `84d9332` on an otherwise idle host
(load average 1.0 at the start, from the preceding build; the pre-review
figures at `25089e8` were taken under load 2.6–3.8). Raw data:
`../evidence/cp11/vm_compare.jsonl`; tables with the pre-review VM column:
`../evidence/cp11/vm_compare-tables.md`. Instructions: callgrind
(deterministic, gated). Wall times: median of 5 (min in brackets), reported,
not gated. CPython 3.13.16, function-local code.

### Instructions per operation (marginal, N=4,000 vs 8,000)

| Operation | v4.12 | legacy (this branch) | VM before review | **VM** | CPython 3.13 | VM vs v4.12 | VM / CPython |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Loop iteration | 3,066 | 3,129 | 105 | **104** | 388 | 29.5× fewer | 0.27× |
| Add-assign | 4,957 | 5,002 | 177 | **184** | 653 | 26.9× fewer | 0.28× |
| **Flat call** (gate) | 14,994 | 13,414 | 464 | **467** | 980 | **32.1× fewer** | **0.48×** |
| Array index | 5,356 | 5,401 | 374 | **380** | 722 | 14.1× fewer | 0.53× |
| Array push | 4,568 | 4,613 | 277 | **286** | 535 | 16.0× fewer | 0.53× |
| Object member | 5,240 | 5,285 | 340 | **347** | 846 | 15.1× fewer | 0.41× |
| String append (`t = t + "x"`) | 9,385 | 9,450 | 3,152 | **3,182** | 1,770 | 3.0× fewer | 1.80× |
| If + modulo | 6,006 | 6,050 | 235 | **233** | 820 | 25.8× fewer | 0.28× |
| fib call, top level (per call) | 184,321 | 158,662 | 459 | **465** | 629 | 396× fewer | 0.74× |
| fib call, inside a loop (per call) | 14,766 | 14,760 | 459 | **465** | 512 | 31.8× fewer | 0.91× |

Each "operation" includes the loop around it (`run.py` templates). CPython's
fib figure varies between runs (300–630 per call over the runs so far; hash
randomisation changes its dictionary work); its flat-call figure varied
948–980 over this round's three runs.

**Hot-path cost of the review fixes.** None of the fixes adds an instruction
to the bytecode of these loops. The R1 budgets first made `Call` test the
frame mark and the register stack separately (flat call 488); a single
combined test with an out-of-line cold slow path brought it to 467 (+3,
+0.6 %). The remaining +6–9 instructions on add-assign, array and object
operations and fib (+1–3 %) are code-generation effects in the larger
`Vm::run` (`Value::integer` is no longer inlined at one site: 7 instructions
per flat-call iteration, `Value::release` 7 fewer); the loop and if
operations got cheaper by 1–2.

### Whole process (wall, median of 5) and peak RSS

| Program | v4.12 | legacy | **VM** | CPython | Peak RSS KiB (v4.12 / legacy / VM / CPython) |
| --- | ---: | ---: | ---: | ---: | --- |
| fib(22) | 1,372 ms (1,315) | 1,195 ms (1,147) | **7.2 ms** (6.7) | 18.3 ms (17.4) | 7,352 / 7,856 / 5,364 / 8,628 |
| fib(25) | 5,834 ms (5,725) | 5,253 ms (5,056) | **16.2 ms** (15.9) | 24.8 ms (24.3) | 7,492 / 7,896 / 5,596 / 8,628 |
| 1M-iteration loop | 507 ms (503) | 527 ms (489) | **31.7 ms** (27.2) | 66.9 ms (65.3) | 6,736 / 7,280 / 5,616 / 8,628 |
| 200k calls | 345 ms (344) | 337 ms (319) | **12.4 ms** (11.9) | 31.9 ms (31.1) | 6,880 / 7,440 / 5,620 / 8,624 |
| Startup (`x := 1; print(x)`) | 5.0 ms (4.8) | 4.6 ms (4.5) | **4.8 ms** (3.8) | 16.7 ms (15.2) | 6,624 / 7,140 / 5,248 / 8,628 |

On the idle host the VM's fib(22)/fib(25) wall times are about half the
pre-review figures (12.5/37.2 ms under load). The startup medians (3.5–5 ms)
are within noise of each other. The VM's lower RSS is real but small: legacy
allocates its interpreter state (Parser, scope maps) even for a trivial
script; the VM path returns before it is constructed.

### Startup, compilation and execution separated

| Program | Source bytes | Parse instr | Compile instr | Execute instr | Whole VM process instr | Legacy translation instr (reference) | Parse / compile / execute µs | Cold process | In-process: compile / first run / warm run µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | --- |
| startup | 16 | 5,531 | 72,524 | 383 | 2,568,706 | 4,197 | 20.5 / 42.6 / 6.7 | 3.52 ms | 47 / 9.6 / 0.2 |
| fib(22) | 84 | 21,804 | 96,931 | 26,622,154 | 29,234,607 | 15,401 | 27.8 / 51.2 / 2,960 | 6.8 ms | 67 / 3,012 / 2,906 |
| 1M loop | 61 | 20,159 | 79,983 | 184,000,737 | 186,593,062 | 11,290 | 28.7 / 51.4 / 22,862 | 26.8 ms | 121 / 25,185 / 22,996 |
| 200k calls | 98 | 28,557 | 100,765 | 93,400,731 | 96,024,077 | 17,325 | 29.8 / 57.6 / 8,305 | 12.2 ms | 80 / 8,245 / 8,178 |
| 500 functions (66 KB, generated) | 66,296 | 15,953,728 | 20,836,549 | 1,862 | 42,185,514 | 8,122,025 | 1,758 / 2,431 / 19.8 | 7.7 ms | 4,398 / 9.0 / 1.0 |

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
- **Cold vs warm.** Cold process time = startup + compile + first run. In
  process, later runs of the same compiled Program were 1–9 % faster than
  the first (1M loop 25.2 → 23.0 ms, 200k calls 8.2 → 8.2 ms, fib(22)
  3.0 → 2.9 ms) on the idle host; the 30 % reported before the review was
  load noise.

## Gate

Predefined gate (checkpoints.md CP11), measured with the `run.py ops`
methodology (marginal callgrind instructions per iteration, N=4,000 vs 8,000),
re-measured after the review fixes with `NIFT_VM=require` (genuine VM
execution; a fallback or bail would be an error):

| Criterion | Required | Measured | Result |
| --- | --- | --- | --- |
| Flat call ≥ 5× fewer instructions than v4.12 | ≤ 2,994 (v4.12 14,994 here; CP00 14,970) | **467** (32.1× fewer; before the review 464) | **met** |
| Flat call ≤ 2× CPython function-local | ≤ 1,960 (2 × 980 here; CP00: ≤ 1,980) | **467** (0.48×) | **met** |
| Parity on every compiled unit | identical stdout/stderr/exit (approved D-001#4 recursion budget only) | As reviewed: **not met** (round 1 found seven silent miscompiles the corpora did not exercise). After the fixes: 0 non-approved differences over rt-diff 793 entries, VM corpus 64, 600 generated/mutants, the adversarial gate (19,969 programs, seed 1; 1,912 sanitized, seed 2) and the reviewer's own programs | **met after fixes** (re-review pending) |

**Verdict: GO** (unchanged; re-review pending). Recursive calls (fib) cost
465 instructions per call, at CPython parity; loops are 29× cheaper than
v4.12 and ~3.7× cheaper than CPython.

### Where the remaining time goes (flat call, 467 instructions/iteration)
`callgrind_annotate` (`../evidence/cp11/profile-call.txt`): `Vm::run` 420,
`Value` move-assignment 26, `Value::release` 14, `Value::integer` 7 per
iteration (before the review: 410 / 26 / 21 / `reserve_registers` 7). Per
iteration the VM executes 11 instructions in the caller loop (compare-jump,
two argument moves with parameter-type checks, call, type check, move,
`AddI`, type check, jump) and 2 in `add` (add, return). Further easy wins,
not needed for the gate: dropping argument moves when the argument is
already in place, a specialised `Return` that skips register clearing for
frames with no heap values, and a non-refcounting store for scalar results.

## Risks
- **Coverage.** 78 of the 757 shared-corpus scripts run on the VM (79 of 758
  before the CP10 integration); most real programs need constructs from
  CP12–CP16. The review fixes refuse more shapes (zero decimal literals,
  mid-block jumps, 206 legacy-special names, place arguments to mutating
  callees) without changing any shared-corpus outcome. The performance
  evidence is for the subset only; template, filesystem and process code is
  untouched.
- **Replay** costs time on programs that bail late (the VM work is wasted),
  and output appears at the end of a VM run instead of progressively (opt-in
  only). Since review D7 a replay is always exact: nothing is committed
  before completion, and a program printing more than 64 MiB is stopped at
  the limit and replayed on legacy from the start (cost: the buffer memory
  and the VM time until the limit; such programs effectively run on legacy).
- **Parity evidence is differential.** The review showed that corpora and a
  single generator miss shapes (seven silent miscompiles reached `vm`). The
  adversarial gate now targets feature interactions explicitly and found one
  more (N1) during the fixes; other context-dependent legacy rules may
  remain outside what the generators produce.
- **Legacy context-dependent semantics.** Legacy has two evaluators whose
  behaviour depends on where code runs (top level, loop body, function called
  from a loop). The VM avoids them by bailing outside a verified domain, but
  rules found only by differential testing (text-derived binding types, `!`
  precedence, `+=` on strings) suggest others may exist outside what the
  generator produces. Known residual case: a loop-body declaration `y := x`
  whose name `x` refers to a different binding when legacy prepares the loop
  than when it runs it.
- CP09 review items apply: references from `array_mut`/`object_get` must not
  be held across mutations (the VM copies values out immediately); deep
  nesting destruction recursion; `hash_number` inconsistency (unused here).
- CP10's review fixes are integrated (`runtime-2` @`4287adb`); the CP11
  compiler checks for the same shapes remain as a second line.
- **Build gates now see `src/rt`.** Linking `src/rt` into `nift` puts its
  files in the zero-warning gate (`test-warnings`) and the sanitizer/TSan
  builds. Not re-run for this round: `make -k test`/`make certify` on the
  integrated branch (left to certification).
- **Memory budget is approximate.** The frame-held bytes are measured, not
  tracked: strings and elements two levels deep, shared objects counted per
  reference, checked at depths +12.5 % apart. A program that allocates a lot
  per frame between two checks can still use more memory than the budget
  before the bail; an allocation failure is then reported as a VM internal
  failure (exit 70), never silently replayed.
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
(`8e10a3a`, `84d9332`), this record and evidence.

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
owner as candidate deliberate changes D-013/D-014, not implemented here).

| # | Finding | Status | Fix (exact / refused) | Tests |
| --- | --- | --- | --- | --- |
| D1 | Zero decimal literals (`0.0`, `0.00`, `0.000`, `0.0e0`, `0.0e5`) keep their spelling in legacy (JSON StrNumber; negation `-0.0`); the VM printed `0` | fixed | **refused** (static): every zero-valued decimal spelling, and exponents of magnitude ≥ 300 (same JSON rule: `1.0e-300` keeps its spelling) → `number-literal`. Computed zeros (`0.5 - 0.5`) are plain doubles in legacy too and stay compiled | unit (5 fallbacks, 1 output), corpus `review_d1_zero_literals.f`, adversarial `numeric` (39 spellings × 19 uses × 4 contexts) and `typed` (zero literals in the generator) |
| D2 | A statement after `break`/`continue` in the same block: legacy ignores the jump and runs the rest | fixed | **refused** (static): a block whose `break`/`continue` is not its last statement → `jump-not-last` (candidate change D-013, owner) | unit (2), corpus `review_d2_jump_not_last.f`, adversarial `control` (jumps at every position, nested) and `typed` (fuzz2 mid-block jumps) |
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
