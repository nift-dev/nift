# CP12 phase B — resolver coverage: plan and record

Status: in progress. Branch `runtime-2-cp12b-dev` (local, not pushed), base
`runtime-2` @`ed7d64d9`. Roadmap scope (CP12): "Resolver complete: globals,
`GETDYN`, modules, structs, enums; bail-and-replay removed" — phase A
(engine selection, replay removal) is certified. Inventory:
`cp12b-coverage-inventory.md`.

File ownership while the integration owner lands CP13/CP14/CP15 and the
records20k stage (`spec-integration`): phase B edits only `src/rt/resolver.*`,
`src/rt/declarations.*` and new `src/rt/` files. Anything in `compiler*`,
`vm*`, `codegen*`, `infer*` or the integration-owned tests is a **hook**,
described below or kept on a commit marked "needs … hook".

## Plan (order of work)

Ranked by scripts unlocked in the inventory (curated shared corpora, 589;
project units, 86), cheapest first where the value is close. Semantics come
from `semantics.md` (CP08) with legacy as the oracle (D-015); refusal is
preferred to compatibility machinery; one meaning everywhere (D-018); errors
stay loud (D-020); quirks go to `language-consistency.md`, not fixed here.

| # | Feature | Blocks (certain) | Sole | Files | Semantics source | State |
| --- | --- | ---: | ---: | --- | --- | --- |
| B1 | Function declarations after statements | 48 curated (227 with rt-diff gen), 7 projects | 2 | resolver | legacy registers a function when its declaration runs; exact static binding when no call can run first | **done** (`7b549ae2`) |
| B2 | Globals: free names that provably resolve to the top-level binding | 11 curated (21 all) | 2 | analysis: new `globals.*` (done); use: compiler/infer/VM hook H1 | semantics.md "Free names" (dynamic scoping) + G1–G3 below | analysis done; **needs compiler hook** |
| B3 | Modules: `import`/`export`, imported functions and bindings | 124 + 33 + 16 curated, 41 projects | 22 | resolver (module graph) + compiler/VM hook H2 | **not written in semantics.md**: needs CP08-style probes first | not started: semantics first (question Q1) |
| B4 | Structs (declarations, construction, fields, methods) | 37 + 17 curated, 26 projects | 10 | resolver (struct table) + compiler/VM hook H3 | semantics.md: reference semantics, construction rules only | not started: semantics first (Q2) |
| B5 | GETDYN proper (a free name only a caller's local resolves; dynamic shadowing) | 2 curated (54 rt-diff gen) | 0 | — | semantics.md "Free names"; L25 proposes lexical scoping | **refused by design** (`free-name:dynamic-caller`, `free-name:dynamic-shadow`); question Q3 |
| B6 | Enums | 0 units in any corpus | 0 | — | not written | defer; refused (`statement:EnumDecl`) |

Why this order: B1 is resolver-only and a prerequisite almost everywhere (it
blocks 227 of 687 refused shared scripts); B2's analysis is resolver work and
the hook is small and self-contained; B3 is the largest unlock but has no
written semantics and needs module loading (a Host read, CP16 interface) and
a cross-unit link step; B4 next; B5/B6 stay refused unless the owner wants
lexical scoping (L25) or enums appear in real code.

## B1 — function declarations after statements (implemented)

Legacy (`ParserTemplate.cpp`) registers a function when its declaration
statement runs; a call before then fails ("undefined callable: f", or reaches
a built-in of that name). Phase A refused every unit with a declaration after
any statement (`fn-after-statement`). Now `plan_unit` admits such a unit when
no top-level statement before a declaration can reach that function through
calls, directly or via other functions (every call in the statement counts,
taken or not). Otherwise it refuses at that statement:
`fn-called-before-declaration`. Static binding is then exactly legacy's
binding: by the time any call runs, the declaration has run.

* Code: `src/rt/resolver.{h,cpp}` only (`check_declaration_order`). The
  compiler already skips declarations wherever they appear, and every unit
  analysis (kinds, prepared contexts, may-double, typed names, aliasing)
  walks `plan.functions` in program order.
* Not changed: redeclaration (`fn-redeclared`), a binding named like a
  function (`binding-shadows-fn`), nested declarations (`statement:FnDecl`).
* Tests (commit `649729a2`/`e2bbc555`, integration-owned files, see Hooks):
  `vm_unit` (2 supported programs, 3 refused shapes: direct, through
  another function, in an untaken branch); `vm_subset` (supported
  `fn-after-statements` at top level, in loops and calls; unsupported direct
  and transitive); VM corpus (`fallback_fn_after.f` now `vm`, new
  `fallback_fn_called_early.f`); `vm_inventory.py`; the supported-subset
  generator declares 1–2 functions after statements in 35 % of programs
  (172 of 500, seed 1; required feature `fn:after-statement`).
* Coverage (classify-all, same corpora): shared 102 → 104 compiling (+2,
  the two scripts B1 was the sole blocker of; 0 lost); projects and
  generator unchanged (their late declarations have other blockers).

## B2 — globals (analysis implemented; compiler hook H1 needed)

`src/rt/globals.h`: for each function and each name it reads or writes
without declaring it, the name is the top-level binding exactly when

* G1 it is declared once, directly in the program (not in a block, not by a
  pattern) and in no top-level block;
* G2 no function that can be active when this one runs (it and its
  transitive callers) declares it;
* G3 every top-level statement that can reach the function comes after the
  declaration (the declaring statement itself may not).

Then legacy's dynamic lookup can only find the top-level binding, and it
exists whenever the function runs. Otherwise the name stays refused with the
reason: `free-name:dynamic-caller` (GETDYN proper), `dynamic-shadow`,
`before-declaration`, `not-one-top-level-binding`, `undeclared`,
`legacy-binding`. Today the analysis refines the inventory; the compiler
does not use it yet.

## Chained postfix (coordinator item 1)

Cause of the refusal: `plain_postfix` (compiler_rules.cpp) admitted member,
index and method links only on name paths (`a`, `a.b`, `a[i]`); CP10 had
already reduced every chain on other receivers to the shapes legacy
dispatches (links from a plain name through calls, links on array/object
literals, known methods elsewhere; everything else Opaque). Legacy evaluates
those shapes like any receiver, in every context (`vm_chain.py` and the
chain probes).

Shapes in the inventory (classify-all now records each refused access's
shape and source line). Of the 153 curated scripts with a chained-postfix
refusal, 28 have it as a certain reason; the other 125 meet it only behind
an unsupported inner method (`s.split(",").join("-")`: masking `split`
leaves `null.join`). Certain sites (curated): receivers — call results 47,
object literals 10, method-call results 7, array literals 6, string
literals 2, parenthesised expressions 3; accesses — method 58, member 16,
index 1.

| # | Cause | Effort | State | Scripts |
| --- | --- | --- | --- | --- |
| C1 | index link on a call result or literal (`f()[0]`, `[..][i]`, `{..}["k"]`) | S | **done** (`5521a861`) | part of the 28 |
| C2 | method link on a call result or literal (`f().length()`, `s.trim().upper()`): the chain is admitted; the method itself still needs its owner (CP14, CP15, CP14B, CP15B, CP16) | S | **done** (`5521a861`) | most of the 153 (as their methods land) |
| C3 | member read on a call result or literal (`f().k`, `{"a": 1}.a`): legacy's missing-member message depends on receiver shape and context (L43) | M: a receiver-shape note in `member()` (compiler_call.cpp, frozen) and the VM message; or language cleanup unifies the message | refused, `postfix-receiver:member` | 6 curated, 1 project (certain) |
| C4 | links on other receivers (parenthesised expressions, numbers, booleans) | — | CP10 Opaque (legacy rejects most of them) | 0 certain after C1/C2 |

Measured with the fix (classify-all, same corpora): alone +0 (104 → 104
curated; every chain also calls a method nobody supports yet, 0 lost);
with B2 globals 106; with modules 154; modules + B2 156; with modules,
functions as values and CP13–CP16 **262 curated, 14 projects, 285 shared**
(without the chain fix the same set gives 252 / 5 / 262: the fix is worth
+10 curated, +9 projects, +23 shared in combination); C3 would add +2
curated.

Gates for the fix and for H1 below the compiler: see "Tests run (round 2)".

Interaction with CP14's W6 (found by cp12b-gates10, after the rebase brought
the CP14 wiring in): `o.stringify().stringify()` used to be refused as
`postfix-receiver` before W6's presentation-chain check was reached
(`vm_builtins.py` q14 expected that). With call-result receivers compiling,
W6 refuses it with its own reason, `builtin-presentation-chain`. The program
is refused in every context either way (legacy prints `{"a":1}`); the
expectation's premise was outdated and now names W6.

## H1 globals: the parts below the compiler (coordinator item 2)

Done (`c8ffc7ca`): opcodes `GetGlobal`/`SetGlobal` (VM cold path; no hot
opcode touched), `FunctionBuilder::keep_type`, the inference's shared global
slot (`infer_unit_kinds(..., globals)`, `infer_globals.*`), unit tests
`tests/rt/vm_globals_unit.cpp` (23 checks). Waiting for the CP15
integration (frozen `compiler_expr`, `compiler_call`): emitting the opcodes
for free names, writes with the binding-type check, `compile_unit` passing
the plan to the inference, and the D-018 analyses' scope for globals
(compiler_unit.cpp, compiler_assign.cpp: not frozen, but the hookup goes in
together). Decision 3 (L25): ordinary globals and module bindings must not
be compile errors; only caller-dependent lookups stay refused.

## Tests run (round 2: chained postfix, H1 below the compiler)

At `2f424e1b` (before the rebase onto `runtime-2` @`1de92ee9`; the rebased
branch is re-validated, see below), through `heavy.sh`:

| Gate | Result |
| --- | --- |
| `make test-rt-vm` | **PASS**: vm-unit 40,320; globals unit 23; prepared-parser 100,136; rt-diff 816 entries, 0 differing, 7 approved; adversarial 37,449, 0 non-approved; subset suites 50/66; supported generator 500/500 on the VM; unsupported 500/500; engine selection, former replay, D-018, context consistency, policy negatives, review 87, golden 108, int bounds 2,682; vm_inventory 13; **vm_chain 96 programs (24 shapes × 4 contexts), 0 failures**; module probes 25 and struct probes 26 (legacy) PASS |
| `make test-rt-vm-mutants` | **PASS**: 8/8 rejected, unmutated passes |
| hot paths (callgrind) | flat call **420** (425), loop **104** (109), add_local 178 (183), if_branch 236 (244), fib **285** (285) per call — the two new opcodes as ordinary dispatch entries; the cold-path variant had cost +1 (426 / 109 / 184 / 286) and was replaced |
| `make test-rt-vm-sanitize` | **clean** (globals unit and vm_chain included) |
| supported generator with chains (`b7d60330`, needs test hook) | seed 1 at 500 and seed 2 at 300: 100 % on the VM (`chain` in 374 of 500); seed 3 index 154 is refused `context:assignment-error` by the pre-change binary too: a generator precision gap the shifted random stream exposes, not a chain problem (to fix with the generator owner) |

Inventory after the fix (`classify-all`, same corpora): curated 104 (no
change alone), with modules 154, with modules + globals 156, with modules +
functions as values + CP13–CP16 **260 curated, 14 projects, 283 shared**;
member reads (L43) would add 2 curated.

## Effort of the greedy-climb features

S = days, resolver/compiler only; M = a week or two, compiler + VM + tests;
L = a checkpoint-sized piece or a runtime subsystem. Order = the curated
greedy climb after modules + functions as values + CP13–CP16
(`cp12b-coverage-inventory.md`, "Residual").

| Feature (owner) | Unlocks (sole / blocked, curated residual) | Effort | Why |
| --- | --- | --- | --- |
| `fn[async]` (CP22), `@fn` deferred bodies (CP24) | 34 / 58 | L (async), M (@fn) | async needs the CP22 scheduler; `@fn` is a translated body |
| callback-taking collection methods (CP14B) | 30 / 38 | M | callables as values (CP12C) first, then natives calling back into the VM |
| threads, mutexes, atomics (CP22/CP23) | 28 / 44 | L | VM coroutines, isolates |
| template functions (CP24) | 23 / 30 | M | template compiler hooks |
| L7 aliasing (language cleanup; records20k: CP14) | 19 / 29 | L | a value/reference rule decision, then COW cells |
| remaining pure built-ins (CP15B) | 18 / 32 | M | many small natives |
| structs (CP12b) | 15 / 37 (+ 18 methods) | L | struct table, heap layout, methods, private, identity |
| literal spellings, interpolation (CP15B) | 15 / 20 | S–M | exact decimals, escapes, `$[…]` in strings |
| legacy parse/run-time quirks (language cleanup) | 12 / 16 | M | per-quirk decisions (D-014, D-022) |
| statement and binding forms (CP12C) | 11 / 23 | M | const, destructuring, element targets, spread |
| L6 reserved names (language cleanup; D-009#3 approved) | 11 / 17 | S | resolver: user names shadow built-ins |
| D-018 loop-evaluator refusals (language cleanup) | 11 / 15 | M | lift once legacy is no longer the loop oracle |
| template-only constructs (CP24) | 7 / 10 | L | template compiler |
| globals (CP12b, H1) | 6 / 9 | S | hook after the rebase |
| FFI (CP18) | 4 / 5 | L | C ABI 1.4 |
| `await` (CP22) | 3 / 23 | L | scheduler |
| shell (CP19) | 3 / 4 | L | command mode |
| processes (CP16/CP20) | 2 / 4 | M | pipelines |
| modules (CP12b) | — | L | per `cp12b-modules.md` |
| chained postfix (CP12b) | — | S | done |
| functions as values, closures, `for` (CP12C) | — | L | closures, upvalues, iteration protocol |

## Hooks for the compiler / VM / test files (sequence with the integration owner)

**T1 — B1 test expectations** (commits `649729a2`, `e2bbc555`, marked "needs
test hook"): `tests/rt/vm_unit.cpp`, `tests/runtime2/vm_subset.py`,
`tests/runtime2/vm_subset_gen.py`, `tests/runtime2/vm-corpus/cp11/EXPECT`
(+ one new corpus file). Without them `test-rt-vm` fails on B1's changed
reason. Small, line-local edits; they conflict only textually with the
integration stage's edits of the same files.

**H1 — globals in the compiler and VM** (not written; touches compiler,
infer, VM):

1. `compile_unit`: compute `plan_globals` after `plan_unit`; pass it to the
   inference and the `UnitCompiler`.
2. `compile_main`: a top-level declaration that is some function's global is
   always typed, and its value and type registers are recorded (main frame,
   base 0); `drop_unread_types` keeps that type register.
3. Reads (`local_register`, `compiler_expr.cpp`): a free name with
   `ok` → `GetGlobal dst, reg` (new opcode, R[a] = frame0[b]); otherwise
   `fail(node, ref.refusal)` (precise reasons replace `free-name`).
4. Writes (`assign`, `compound_assign`, `inc_dec`): value into a temporary,
   global type into a temporary (`GetGlobal`), the canonical check
   (`CheckType`/`CheckAssign`, D-020), then `SetGlobal reg, value` (new
   opcode). D-018 context checks use the global's `Binding` (main scope's
   may-double sets). Pushing onto a global (`g.push`) stays refused
   (`push-receiver`) until aliasing rules for globals are written.
5. Text types naming a global inside a function resolve to the global's
   type register.
6. `infer.cpp`: an identifier not in a function's scopes that is a global
   reads the declaration's slot; writes merge into it; growth re-queues main
   and every function using it.
7. VM: two opcodes, not on any hot path (dispatch table +2).
8. Tests: `vm_unit` read/write/compound/type-error cases; context
   consistency (globals read from loop-called functions — legacy's prepared
   evaluator must be probed for free names first); `vm_subset` supported
   (`globals-*` at top level, loops, calls) and unsupported (each refusal);
   the supported generator: functions reading or updating top-level scalars
   declared before their first call.

**H2 — modules** and **H3 — structs**: designs follow the written semantics
(Q1, Q2); none is started.

## Tests run

At `e2bbc555` (B1 + its tests), shared host, every heavy job through
`heavy.sh`. Logs: `../evidence/cp12b/gates/`.

| Gate | Result |
| --- | --- |
| `make test-rt-vm` | **PASS**: vm-unit 40,320 checks; prepared-parser port 100,136; rt-lint; legacy names; VM corpus 79 (0 failures); rt-diff 816 entries, **0 differing, 7 approved** (3 D-001#4 + 2 D-018c class, confirmed by VM statistics; 2 exact rules); rt-diff VM outcomes 103 of 789 scripts on the VM (83 vm + 20 vm-error); adversarial 37,449 programs, 0 non-approved differences (69 D-001#4, 99 D-018c); subset suites 50 supported / 66 unsupported; supported generator 500/500 on the VM; unsupported generator 500/500 refused before execution with the expected reason; engine selection, former replay, D-018, context consistency, policy negatives, review reproducers, golden 108, int bounds 2,682: PASS |
| `make test-rt-vm-mutants` | **PASS**: unmutated passes every suite; 8 of 8 mutants rejected |
| hot paths (callgrind marginal VM instructions, `vm_compare.py`'s method; baseline = this branch before B1) | flat call 425 → 425, loop 109 → 109, fib 285 → 285 per call (add_local 183, if_branch 244 unchanged) |
| `make test-rt-vm-sanitize` | **clean** (ASan+UBSan, leaks on): vm-unit 40,320; prepared-parser 40,136; parity, adversarial 3,619 (0 non-approved), subset suites, generated suites, engine selection, former replay, D-018, context consistency, review, golden, int bounds: PASS |
| `tests/runtime2/vm_inventory.py` | 12 cases, 0 failures (also on the sanitized binary); classify-all over the 86 project units on the sanitized binary: 86 complete inventories, no crash |

Generators (seed 1, 500 each), before (runtime-2 tests, baseline binary) →
after:

* supported subset: 500/500 on the VM → 500/500 on the VM; programs with a
  declaration after statements 0 → 172.
* unsupported (all refused before execution with the expected reason, both
  runs; the mix shifts because the supported base programs draw more random
  numbers): `fn-after-statement` 16 → 0, `fn-called-before-declaration` 0 →
  26; the other 32 reasons 484 → 474 programs.
* general generator (`vm_gen`, 300): 72 on the VM (24.0 %) → 72 (its
  functions are all declared first).

## Open questions for the owner

* Q1 Modules: `semantics.md` has no entry for `import`/`export`. Write the
  CP08-style probes and semantics first (what an import binds, where, when
  the module runs, re-import, cycles, export of non-functions, paths)? Is
  compile-time resolution of literal paths, with the module compiled as part
  of the importing unit, the intended design, and is loading a Host read
  (CP16 interface)?
* Q2 Structs: same — only reference semantics and construction are written;
  methods, `this`, private fields, field types, equality and printing need
  semantics before B4.
* Q3 GETDYN: keep `free-name:dynamic-caller` / `dynamic-shadow` refused for
  good (L25 proposes lexical scoping; 2 curated scripts use it), or is a true
  dynamic lookup op wanted in CP12?
* Q4 Unassigned work (≈ 180 curated scripts): `for` loops, lambdas /
  first-class functions, lambda-taking collection methods, other methods and
  legacy built-ins outside CP14–CP16. Which checkpoints own them?
* Q5 Enums never occur in the corpora: defer B6 past CP25's fallback count
  (they would still need to be refused, which they are)?
