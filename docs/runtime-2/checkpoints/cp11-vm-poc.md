# CP11 — VM proof of concept (GO/NO-GO)

Status: see `../progress.md` (implemented, under review). **Gate verdict: GO**
(see "Gate"), with the risks and limits listed below.

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
  `script_runner.*` (2,778 lines, all lint-clean). `src/rt` is now linked
  into `nift` (`RT_LINK_SOURCES` in the Makefile).
- `src/CLI.cpp`: one hook in `run_script_source` (file, `-e`/`-c`, stdin
  scripts; not when an interactive shell follows, only the native target).
- Tests and tools: `tests/rt/vm_unit.cpp`, `tests/rt/vm_run.cpp`,
  `tests/rt/rt_vm_tool.h`, `tests/runtime2/vm-corpus/cp11/` (55 scripts +
  `EXPECT`), `tests/runtime2/vm_gen.py`, `tests/runtime2/vm_parity.py`,
  `tools/rt-vm/nift-vm`, `tools/rt-diff/policies/cp11-vm.json`,
  `benchmarks/runtime2/vm_compare.py`.
- CP09/CP10 files are unchanged. The known CP10 lint findings (15, in the
  parser files) are excluded from the CP11 lint step only.

## Design

### Opt-in and fallback
| Setting | Behaviour |
| --- | --- |
| `NIFT_VM` unset or `0` | Nothing new runs (one `getenv`); `nift` behaves exactly as before |
| `NIFT_VM=1` | Eligible scripts run on the VM; everything else, and every run-time bail, runs on legacy unchanged |
| `NIFT_VM=require` | Never uses legacy: an ineligible script or a bail is an error (`error: <file>:<l>:<c>: …`, exit 1). Used for every VM measurement |
| `NIFT_VM_STATS=1` / `=<path>` | One JSON line per script (stderr / appended to the file): outcome, fallback or bail reason, position, parse/compile/execute µs, protos, instructions |
| `NIFT_VM_DUMP=1` | Bytecode listing on stderr |

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
   Bail reasons: `runtime-error` (anything legacy reports as an error),
   `number-range` (integer or double at or beyond 2^53, integer −0; see
   "Numbers"), `aliasing` (an aggregate bound from a place, where legacy makes
   a location reference), `static-error` (a statement the compiler knows
   errors, e.g. redeclaration, wrong argument count), `unverified` (a case
   whose legacy result the VM does not claim, e.g. assigning a different type
   to an array element), `recursion-budget`.
   A replay is not observable except in time (the VM work is wasted) and in
   when output appears (at the end instead of progressively). If the buffer
   exceeds 64 MiB it is flushed and the run becomes *committed*; a later bail
   then reports a VM-native diagnostic, which is not legacy-exact (see Risks).

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
  between 65 and 100,000 frames deep succeed on the VM where legacy fails;
  the two corpus probes affected are approved by `cp11-vm.json` (D-001#4).

### Natives
Bound to integer ids at compile time: `print` (`NativeId::Print`). The
methods `push`, `length()` and `size()` compile to `Push`/`Length`. A user
function or binding whose name is a legacy built-in, script binding or
compiled method is a fallback (legacy precedence differs per name, D-009#3).

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
  bail (legacy types parameters from argument text).
- **Rendering.** Strings as-is; numbers through the legacy
  `RuntimeValue::dump`; `true`/`false`/`null`. Arrays/objects bail.
- **Strings.** `s += t` appends in place when the string is unshared (legacy
  does the same), so 320k appends stay linear.
- **Places.** Declaring/assigning/returning/pushing an aggregate taken from a
  place bails (`NoAlias`): legacy creates location references there.

### Supported subset vs fallback

| Feature | VM | Legacy (fallback) |
| --- | --- | --- |
| Literals: integers < 2^53 without leading zero; decimals with ≤ 15 significant digits and magnitude < 2^53; strings without `\`, `$`, control bytes, invalid UTF-8; `true` `false` `null` | ✓ | other spellings (exact decimals, `007`, escapes, interpolation) |
| `x := e`, `x = e`, `+= -= *= /= %=`, `++`/`--` on names | ✓ | `const`/`immut`, destructuring, `&= \|= ^=`, increments inside expressions |
| `a[i] = v` (array, existing element, same type) | ✓ (else bail) | object index assignment (bail), member assignment (bail), calls in both index and value |
| `+ - * / %`, comparisons, `&&` `\|\|` `!` `??`, `?:` in parentheses, unary `-` `+` | ✓ | `!` ending the left side of an operator (legacy reads `!x + 1` as `!(x + 1)`) |
| `if` / `else if` / `else`, `while`, `break`, `continue` | ✓ | `for`, `try`/`catch`, `throw` |
| `fn` declarations (top level, before the first other statement), calls, recursion, `return` (also at top level) | ✓ | `fn[async]`, `@fn`, nested functions, lambdas, struct/enum/import/export, functions referring to non-local names (dynamic scope, CP12) |
| Arrays/objects: literals, `a[i]`, `o.k`, `o["k"]`, `.push(x)`, `.length()`, `.size()` | ✓ | every other method or built-in (`sort_by`, `map`, `stringify`, `split`, …), `?.`, `?[`, spread, postfix on literals, whitespace before postfix, trailing commas, duplicate object keys |
| `print(x)` | ✓ | every other native; bare expression statements other than calls |

## Tests

| Suite | Content | Result |
| --- | --- | --- |
| `tests/rt/vm_unit.cpp` | outputs (21 programs), fallback reasons (18), bail reasons (12), literal decoding, Int rendering = legacy `dump` (20,000 values), frame budget (99,990 deep OK, 100,000 bails, configurable), 90,000 nested calls on a 256 KiB stack, output-limit commit, code shape | 40,139 checks, 0 failures |
| `tests/runtime2/vm-corpus/cp11` | 55 scripts, legacy goldens (also checked by `test-runtime2-vm-corpus`), expected VM outcome per script: 10 run on the VM, 25 replay (one per bail reason/diagnostic class), 20 fall back (one per reason) | identical to legacy, every outcome as expected; the 10 VM scripts also identical under `NIFT_VM=require` |
| Generated programs (`vm_gen.py`), seed 3 | 600 typed random programs: 390 completed on the VM, 209 replayed (113 runtime-error, 80 number-range, 12 static-error, 4 unverified), 1 Opaque | 0 differences |
| Mutants of those programs | 600 (deleted characters, swapped operators/lines, braces, `$`, backslashes, whitespace before postfix, trailing commas, `await`, `const`, `for`, lambdas, `#!` lines): 200 on the VM, the rest fall back or replay across 30 reasons | 0 differences; 1 endless mutant (both engines hit the time/output limit) not compared |
| `test-rt-vm` default run (seed 1) | unit + corpus + 300 generated + 300 mutants + rt-diff | PASS |
| rt-diff, legacy vs `tools/rt-vm/nift-vm` | `vm-corpus`, `semantics`, `rt-diff-corpus`, conformance cases, cp410 probes, 1,700+ extracted smoke scripts, 200 `gen.py` programs: 785 entries | **0 differing**, 2 approved (`deep_recursion.f`, `recursion_limit.f`: D-001#4 frame budget), 0 unused policies |
| `make test-rt-vm-sanitize` | ASan+UBSan (no recovery, leak detection on): unit tests; corpus + 150 generated (102 on the VM) + 150 mutants on `nift-sanitize` | clean, 0 differences (`../evidence/cp11/sanitize.txt`) |
| Lint (`tools/rt-lint.py`) | all CP09 and CP11 files in `src/rt` | 0 problems |
| `make -k test` with `NIFT_VM` unset (default behaviour unchanged) | every existing suite; failures re-run with `scripts/certify.py` | all pass except: `test-rt-lint` (CP10's 15 known findings), `test-v43-language`, `test-dependency-status`, `test-unreadable-source` (SKIPPED-ENVIRONMENTAL: fail only as root, pass as user `tester`), two v4.8 guards (UNVERIFIED: `/usr/bin/time` missing). Two CP11 integration breaks found here and fixed: `rt-legacy-oracle` link (duplicate `src/rt` objects) and a corpus probe that tripped CP10's paren oracle |

Defects found by the differential tests and fixed during the checkpoint:
1. **Silent miscompile (fixed):** binding types were taken from values only;
   legacy also types bindings from the expression text (3 of the first 300
   generated programs: legacy failed "cannot assign string to double
   binding", the VM printed output). Fixed by porting `expression_type`.
2. A post-pass removed a `BindType` still read as a text type (bail only, no
   wrong output; caught by the corpus `EXPECT`; regression test added).
3. Quadratic `s += t` (rt-diff timeout on `str320.f`); fixed by in-place append.

### Parity over the shared corpora (rt-diff run, 758 script entries)

| Outcome | Scripts |
| --- | ---: |
| Ran on the VM | 79 |
| Replayed on legacy after a bail | 53 (runtime-error 25, static-error 11, aliasing 11, number-range 3, unverified 2, recursion-budget 1) |
| Fell back before running | 626 |

Main fallback reasons: `fn-after-statement` 211 (generated programs define
functions after statements), `import` 52, `fn[async]`/`@fn` 50, `struct` 27,
`inject` 24, `sort_by` 19, `try` 18, `map` 16+9, lambdas 14. Per-script list:
`../evidence/cp11/rt-diff-vm-outcomes.json`. Most real programs therefore
still run on legacy; CP12–CP16 widen the subset.

## Performance

Raw data: `../evidence/cp11/vm_compare.jsonl` (commit `25089e8`).
Instructions: callgrind (deterministic, gated). Wall times: median of 5
(min in brackets) on a shared 2-vCPU host with load average 2.6–3.8 from
concurrent reviews (reported, not gated). CPython 3.13.16, function-local code.

### Instructions per operation (marginal, N=4,000 vs 8,000)

| Operation | v4.12 | legacy (this branch) | **VM** | CPython 3.13 | VM vs v4.12 | VM / CPython |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Loop iteration | 3,066 | 3,129 | **105** | 386 | 29.2× fewer | 0.27× |
| Add-assign | 4,957 | 5,002 | **177** | 625 | 28.0× fewer | 0.28× |
| **Flat call** (gate) | 14,994 | 13,414 | **464** | 959 | **32.3× fewer** | **0.48×** |
| Array index | 5,356 | 5,401 | **374** | 711 | 14.3× fewer | 0.53× |
| Array push | 4,568 | 4,613 | **277** | 506 | 16.5× fewer | 0.55× |
| Object member | 5,240 | 5,285 | **340** | 827 | 15.4× fewer | 0.41× |
| String append (`t = t + "x"`) | 9,385 | 9,450 | **3,152** | 1,790 | 3.0× fewer | 1.76× |
| If + modulo | 6,006 | 6,050 | **235** | 804 | 25.6× fewer | 0.29× |
| fib call, top level (per call) | 184,321 | 158,665 | **459** | 492 | 402× fewer | 0.93× |
| fib call, inside a loop (per call) | 14,766 | 14,760 | **459** | 442 | 32.2× fewer | 1.04× |

Each "operation" includes the loop around it (`run.py` templates). CPython's
fib figure varies between runs (300–603 per call over three runs here; hash
randomisation changes its dictionary work); the flat-call CPython figure was
stable (951–959).

### Whole process (wall, median of 5) and peak RSS

| Program | v4.12 | legacy | **VM** | CPython | Peak RSS KiB (v4.12 / legacy / VM / CPython) |
| --- | ---: | ---: | ---: | ---: | --- |
| fib(22) | 1,714 ms (1,287) | 1,310 ms (1,226) | **12.5 ms** (7.6) | 32.0 ms (28.0) | 7,368 / 7,912 / 5,588 / 8,680 |
| fib(25) | 6,170 ms (6,036) | 5,242 ms (5,071) | **37.2 ms** (28.5) | 53.4 ms (33.0) | 7,456 / 8,188 / 5,620 / 8,640 |
| 1M-iteration loop | 529 ms (515) | 551 ms (532) | **28.3 ms** (27.7) | 70.4 ms (68.2) | 6,772 / 7,428 / 5,644 / 8,644 |
| 200k calls | 377 ms (360) | 333 ms (308) | **12.3 ms** (11.8) | 29.4 ms (29.2) | 6,896 / 7,620 / 5,548 / 8,684 |
| Startup (`x := 1; print(x)`) | 3.4 ms (3.3) | 3.5 ms (3.4) | **3.1 ms** (3.1) | 15.9 ms (14.5) | 6,752 / 7,368 / 5,372 / 8,680 |

The VM's lower RSS is real but small: legacy allocates its interpreter state
(Parser, scope maps) even for a trivial script; the VM path returns before it
is constructed.

### Startup, compilation and execution separated

| Program | Source bytes | Parse instr | Compile instr | Execute instr | Whole VM process instr | Legacy translation instr (reference) | Parse / compile / execute µs | Cold process | In-process: compile / first run / warm run µs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: | --- |
| startup | 16 | 5,071 | 72,148 | 363 | 2,560,557 | 4,197 | 19.8 / 43.5 / 7.4 | 3.45 ms | 44 / 5.2 / 0.2 |
| fib(22) | 84 | 18,944 | 92,349 | 26,278,267 | 28,877,077 | 15,401 | 31.3 / 59.2 / 2,957 | 12.0 ms | 133 / 2,853 / 2,886 |
| 1M loop | 61 | 17,580 | 79,245 | 177,000,718 | 179,583,116 | 11,290 | 30.9 / 61.2 / 34,878 | 54.6 ms | 60 / 35,639 / 24,801 |
| 200k calls | 98 | 25,018 | 94,827 | 92,800,712 | 95,408,612 | 17,318 | 37.7 / 58.1 / 13,190 | 23.1 ms | 75 / 12,907 / 9,311 |
| 500 functions (66 KB, generated) | 66,296 | 14,425,120 | 17,443,493 | 1,786 | 38,299,238 | 8,122,025 | 1,631 / 2,226 / 21.5 | 7.1 ms | 3,688 / 9.5 / 1.1 |

- **Process startup** is ~2.5M instructions for both engines, almost all in
  the dynamic loader (symbol lookup and relocation of the 5 MB binary); the VM
  adds ~0.1M (parse + compile + atom table) for small scripts.
- **Compilation** costs 263 instructions per source byte on the large program
  (parse 218/byte, compile 263/byte; 3.9 ms wall). That is more than legacy's
  up-front translation (123/byte), but legacy re-parses expression text on
  every evaluation, so it pays more over any run that executes the code.
  The compile pass re-scans source text for the legacy text-type inference
  (once per declaration, assignment and argument) and interns atoms; both are
  obvious targets if compile time matters (Proto caching by content hash is
  planned anyway, architecture.md #2).
- **Cold vs warm.** Cold process time = startup + compile + first run. In
  process, later runs of the same compiled Program were up to 30 % faster
  than the first (1M loop 35.6 → 24.8 ms, 200k calls 12.9 → 9.3 ms; fib(22)
  unchanged), consistent with first-touch effects; wall times on a loaded host.

## Gate

Predefined gate (checkpoints.md CP11), measured with the `run.py ops`
methodology (marginal callgrind instructions per iteration, N=4,000 vs 8,000):

| Criterion | Required | Measured | Result |
| --- | --- | --- | --- |
| Flat call ≥ 5× fewer instructions than v4.12 | ≤ 2,994 (v4.12 14,994 here; CP00 14,970) | **464** (32.3× fewer) | **met** |
| Flat call ≤ 2× CPython function-local | ≤ 1,918 (2 × 959 here; CP00: ≤ 1,980) | **464** (0.48×) | **met** |
| Parity on every compiled unit | identical stdout/stderr/exit (approved D-001#4 recursion budget only) | 0 differences: rt-diff 785 entries, VM corpus 55 (56 at the time of the seed-3 run), generated 600 + 600 mutants (and the earlier seed-1 runs) | **met** |

**Verdict: GO.** Recursive calls (fib) cost 459 instructions per call, at
CPython parity; loops are 29× cheaper than v4.12 and ~3.7× cheaper than CPython.

### Where the remaining time goes (flat call, 464 instructions/iteration)
`callgrind_annotate` (`../evidence/cp11/profile-call.txt`): `Vm::run` 410,
`Value` move-assignment 26, `Value::release` 21, `reserve_registers` 7 per
iteration. Per iteration the VM executes 11 instructions in the caller loop
(compare-jump, two argument moves with parameter-type checks, call, type
check, move, `AddI`, type check, jump) and 2 in `add` (add, return). Before
the binding-type work the same loop cost 422; adding legacy-exact binding
types cost 789 until unread type instructions were removed and calls wrote
their result in place (464). Further easy wins, not needed for the gate:
dropping argument moves when the argument is already in place, a
specialised `Return` that skips register clearing for frames with no heap
values, and a non-refcounting store for scalar results.

## Risks
- **Coverage.** 79 of 758 shared-corpus scripts run on the VM; most real
  programs need constructs from CP12–CP16. The performance evidence is for the
  subset only; template, filesystem and process code is untouched.
- **Replay** costs time on programs that bail late (the VM work is wasted),
  and output appears at the end of a VM run instead of progressively (opt-in
  only). A program that prints more than 64 MiB and then bails gets a
  VM-native, not legacy-exact, diagnostic.
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
- CP10 parser fixes are pending integration; the compiler refuses every
  construct the CP10 review listed (non-name callees, postfix on literals or
  after whitespace, trailing commas, duplicate keys, `await`, deep nesting,
  `#!` statements, `!` precedence).
- **Build gates now see `src/rt`.** Linking `src/rt` into `nift` puts its
  files in the zero-warning gate (`test-warnings`) and the sanitizer/TSan
  builds. All CP09 and CP11 files compile warning-free with g++ 13 and clang
  (`-Werror -pedantic`), but CP10's `src/rt/parser.cpp:165` (`mode_` unused)
  fails the clang pass; it is left to the CP10 fixes (not edited here).
  `make test-rt-lint` (whole `src/rt`) still reports CP10's 15 known findings.
- Not verified: macOS/Windows (D-004); the `switch` dispatch is compiled
  only as a syntax check (`-DNIFT_RT_VM_SWITCH`); wall times on an idle host.

## Deliverables
Code, tests and tools listed under Scope; Makefile targets `test-rt-vm`
(in `make test` and the certification guard REQUIRED list),
`test-rt-vm-sanitize`, `bench-rt-vm`; evidence in `../evidence/cp11/`.

## Commit strategy
Local commits on `runtime-2-cp11` (from `c1e3aac`), not pushed: VM and
integration (`18fec73`); tests (`82f420d`); call-path and admission fixes
(`ffc8fc9`); a codegen fix (`25089e8`); benchmark and evidence (`d1a6e0c`);
lint, oracle link and corpus fixes (`566ea74`, `8d4b522`, `5cf7a6e`); this
record and the progress/benchmark updates.

## Evidence
`../evidence/cp11/README.md`.

## Reviews
None yet.
