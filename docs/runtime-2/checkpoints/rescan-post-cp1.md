# Post-checkpoint-1 inventory rescan (C111, repaired in C112)

Base: frozen checkpoint-1 candidate `0c66cad6` (owner 21:30: CHECKPOINT 1 ACCEPTED). C111 (`73886c2c`) was the
rescan; its independent review came back NOT CONFIRMED (no P1; classifications, evidence and gate counts wrong).
C112 repairs it. nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4` is unchanged and
`git diff 0c66cad6 -- src/` is empty. Only `tests/runtime2/cp25/classification.tsv` and the docs change. Nothing
is approved or deferred, and no rt-diff or pending rule is touched. **Every proposed disposition is a PROPOSAL;
the owner decides the families.**

## 1. Method

- **Collections.** A fresh `inventory.py collect` at the tip (C112): 155 reasons seen. The C111 tip, pre-checkpoint-1
  mutant and 28a6368b comparisons stand (C111 section 1 findings are unchanged: only `compound-target`,
  `index-assign-target` and `index-target` moved because of C107).
- **F12 fix measured.** A scratch `sanctioned_mutant` build of the two-line inventory fix (section 10) was collected
  and compared with the tip, then deleted.
- **Every reclassification and evidence program was run** on the tip binary, VM default and `NIFT_VM=0`. The exact
  outputs below come from those runs (`NIFT_VM_STATS` for the reason).
- **Gates were re-run** (section 4); every count comes from those logs, one row per failing case.
- **D rows**: every D row was re-checked: either the reviewer's sample (review L-2) or a C112 root-free probe
  (section 3.5).
- **Static scan**: every reason literal in `src/rt` without a tsv row was triaged (section 9).

## 2. Summary

| Class | C111 | C112 | Meaning |
| --- | --- | --- | --- |
| E1 | 2 | 2 | eliminated for its evidence by checkpoint 1 (C107) |
| E2 | 19 | 18 (+2 from N) | test or inventory artefact: 16 F12, `method:join` (F12 plus D residue), `method:increment` (generic name); from the N list `callable:prit` (test typo) and `builtin-receiver:stringify` (retired, not reproducible) |
| A | 4 | 3 | covered by an approved ruling, not yet that behaviour |
| Q | 0 | 1 | owner question: `fn-flags` |
| D | 42 | 39 | derivative, with a named root |
| G | 113 | 117 (+2) | genuine remaining capability; +2 = `incdec-target` (from the N list) and the new `method:*` family row |
| **classified** | **180** | **180** | = 157 PENDING-OWNER + 23 implement at 0c66cad6 |
| N | 45 | 58 | newly found, unapproved: 42 of C111's 45, plus 16 from the C112 static scan |

C112 class moves: `presentation-struct-method:*` E2 (retired) → G; `struct-value`, `callable:undefined_thing`,
`coro-module-private-async` D → G; `fn-flags` A → Q; `method:join` E2 → E2+D; `incdec-target` N → G;
`callable:prit` N → E2; `builtin-receiver:stringify` N → E2 (retired).

| Status | 0c66cad6 | C111 | C112 |
| --- | --- | --- | --- |
| approved | 44 | 44 | 44 |
| deferred-named | 17 | 17 | 17 |
| implement | 23 | 23 | 23 |
| PENDING-OWNER | 157 | 200 | 217 |
| retired | 7 | 9 | 9 |
| rows | 248 | 293 | 310 |

C112 status changes: `presentation-struct-method:*` retired → PENDING-OWNER; `builtin-receiver:stringify`
PENDING-OWNER → retired; 16 new N rows and the `method:*` family row, all PENDING-OWNER.

### 2.1 Per-row classes (C112)

- **E1 (2)**
  - assign (2): `compound-target`, `index-assign-target`
- **E2 (20)**
  - f12 (16): `builtin-arity:join`, `builtin-receiver:join`, `callable:thread`, `coro-blocking-host:exists`, `coro-blocking-host:file`, `coro-blocking-host:rm`, `coro-observe:done`, `coro-observe:print`, `coro-observe:status`, `coro-struct`, `method:close`, `method:done`, `method:open`, `method:save`, `method:status`, `method:write`
  - f12+D (1): `method:join`
  - stale (1): `method:increment`
  - stale (retired C112) (1): `builtin-receiver:stringify`
  - test-artefact (1): `callable:prit`
- **A (3)**
  - approved (3): `alias-argument`, `context:assignment-error`, `fn-name-reserved`
- **Q (1)**
  - owner-question (1): `fn-flags`
- **D (39)**
  - derivative (39): `assign-free-name:undeclared`, `builtin-bytes:decode`, `free-name:undeclared`, `method:array_value`, `method:await`, `method:call`, `method:call_helper`, `method:call_step`, `method:catch`, `method:command`, `method:count`, `method:decode`, `method:encode`, `method:error`, `method:filter`, `method:fixed`, `method:group_by`, `method:helper`, `method:hidden`, `method:m`, `method:map`, `method:public_helper`, `method:push`, `method:q`, `method:read`, `method:secret`, `method:stringify`, `method:sum`, `method:throw`, `method:to_global`, `method:to_string`, `method:trim`, `statement:Import`, `statement:TemplateStmt`, `struct-construction`, `text-type-name`, `type:index`, `type:push`, `unresolved-name:legacy-binding`
- **G (119)**
  - aliasing (1): `path-in-call-argument`
  - builtins (19): `builtin-arity:copy`, `builtin-arity:move`, `builtin-bytes:bytes`, `builtin-bytes:encode`, `builtin-bytes:open_bytes`, `builtin-bytes:secure_random_bytes`, `builtin-lazy-args:move`, `builtin-lazy-args:setenv`, `builtin-lazy-args:substr`, `builtin-lazy-args:validate`, `callable:err`, `callable:minify`, `callable:module_path`, `callable:read`, `callable:secure_random_bytes`, `callable:sorted_map`, `callable:sorted_set`, `callable:warn`, `statement:Command`
  - context (8): `context:assignment-type`, `context:number-to-int`, `context:object-length`, `jump-not-last`, `prepared-decl-type`, `prepared-fn-unprepared`, `prepared-unpreparable`, `string-arg-type`
  - coro (2): `coro-future-flow:Member`, `for-capture-vanishing`
  - cp24 (9): `callable:build`, `callable:build_all`, `callable:build_names`, `callable:inject`, `callable:status`, `callable:track`, `callable:tracked`, `callable:untrack`, `opaque:template-text`
  - dynscope (2): `free-name`, `free-name:dynamic-caller`
  - exports (8): `export-struct`, `import-collision`, `import-dynamic`, `import-name-clash`, `module-bare-return`, `module-host-op:cat`, `module-host-op:exists`, `statement:Export`
  - infer (2): `interpolation:unknown-type`, `method:sort_by`
  - portability (1): `method:sort`
  - static (33): `builtin-arity:contains`, `builtin-arity:last`, `builtin-arity:set`, `builtin-arity:sha256`, `builtin-arity:sha256_file`, `builtin-arity:split`, `builtin-receiver:substr`, `builtin-receiver:to_string`, `coro-async-value`, `coro-module-before-import`, `coro-module-private-async`, `declare-const`, `error-result-expression`, `host-handle-flow`, `jump-outside-loop`, `non-transferable`, `number-literal`, `opaque:legacy-runtime-error`, `opaque:struct-body`, `static-error:arity`, `static-error:member-assign`, `static-error:redeclared`, `static-error:struct-arity`, `struct-private`, `type:+`, `type:<`, `type:>`, `type:compound-assignment`, `type:increment`, `type:length`, `type:member`, `type:script-return`, `type:unary`
  - structs (5): `context:struct-new`, `presentation-struct-method:*`, `struct-after-statement`, `struct-field-init`, `struct-value`
  - syntax (9): `binding-name-reserved`, `expression-statement`, `incdec-target`, `index-target`, `nesting-limit`, `opaque:irregular-legacy-parse`, `param-name-reserved`, `postfix-spelling`, `shebang-statement`
  - undef (16): `assign-free-name`, `binding-shadows-fn`, `callable:assert`, `callable:exit`, `callable:len`, `callable:nope`, `callable:nosuch`, `callable:to_string`, `callable:undefined_fn`, `callable:undefined_thing`, `callable:unknown`, `fn-called-before-declaration`, `method:*`, `method:nope`, `method:nosuch`, `unresolved-name`
  - variadic (4): `declare-pattern`, `fn-param`, `lambda-variadic`, `spread`
- **N (58)**
  - builtins (5): `builtin-lazy-args:range`, `builtin-lazy-args:replace`, `builtin-literal-arg:validate`, `builtin-presentation-chain`, `host-literal-arg`
  - context (6): `context:modulo-assignment`, `context:negative-zero`, `context:parameter-type`, `context:string-append-type`, `context:struct-field`, `for-missing-write`
  - exports (8): `import-limit`, `import-reinitialised`, `module-host-op:cp`, `module-host-op:file`, `module-host-op:getenv`, `module-host-op:is_dir`, `module-host-op:is_file`, `module-host-op:remove`
  - infer (3): `builtin-arg-fault-context`, `interpolation:non-scalar`, `postfix-receiver:member`
  - static (21): `assign-catch-binding`, `await-outside-coroutine-unit`, `builtin-arity:abs`, `builtin-arity:keys`, `builtin-arity:stringify`, `builtin-arity:trim`, `builtin-receiver:empty`, `builtin-receiver:first`, `builtin-receiver:index_of`, `builtin-receiver:keys`, `builtin-receiver:sum`, `builtin-receiver:to_double`, `builtin-receiver:to_upper`, `coro-future-flow:method`, `coro-thread-callee`, `method:replace_once`, `push-receiver`, `static-error:push-arity`, `type:%`, `type:*`, `type:-`
  - structs (7): `struct-assign`, `struct-member-clash`, `struct-name`, `struct-nested`, `struct-no-field`, `struct-redeclared`, `struct-shadows-fn`
  - syntax (6): `comparison-chain`, `object-key`, `opaque:nesting-limit`, `param-shadows-fn`, `prefix-not-precedence`, `string-literal`
  - undef (2): `callable:stringify`, `import-scope`

Root of each D row: section 3.5.

## 3. E1, E2, A, Q and D

### 3.1 E1

`compound-target` and `index-assign-target`: their evidence runs since C107; on the tip corpus each is seen only as a
consequential reason. Residual forms (`o.k += 1`; a fourth level, `o.k[0] = v`) keep them implement.

### 3.2 E2

- **F12 (16 rows).** classify-all compiles with default `CompileLimits` (`refusal_inventory.cpp:163`, no
  `cooperative_switches()`), script runs with the switches (`script_runner.cpp:310-311`). Measured with the fix built:
  all 16 are no longer seen. They stay PENDING-OWNER until the fix lands (retiring them now would trip `retired
  reason seen again`).
- **`method:join` (E2+D).** 40 classify-all sightings at the tip; 9 non-consequential remain with the fix, in
  `v44_automation_smoke` auto.f and auto2.f (`r.affected.join`, `tracked().join`, `status().join`: derivative of the
  CP24 rows `callable:build`, `callable:tracked`, `callable:status`) and `v44_cp3_variadic_lambdas_smoke` ok.f
  (`mapped.join` after `lambda-variadic`). A plain `xs.join(",")` and `.join` on a lambda's result run on the VM. Not
  retirable even with the fix.
- **`method:increment` (retired, reworded).** Its evidence copy.f runs (legacy-equal). It is one name of the generic
  `method:<name>` family: `x := 5` / `x.increment()` still emits it (legacy: `unknown value or malformed expression:
  x.increment()`). C112 adds a `method:*` family row (PENDING-OWNER, G undef) so any generic method name without its
  own row is classified.
- **`callable:prit`**: a typo mutation inside test-rt-vm (an undefined callable); a test artefact, kept PENDING-OWNER
  like every undefined callable.
- **`builtin-receiver:stringify` (retired).** C111 added it from a mis-read session probe. No program emits it:
  `fn(f(x)) { return x.stringify() }` with 1 and `"a"`, `show(1)` in a session, and a struct in a session all run.
- **C111's retirement of `presentation-struct-method:*` was wrong**; it is restored as G (section 5.7).

### 3.3 A (3 rows)

| Row | Ruling | Why still PENDING-OWNER |
| --- | --- | --- |
| `context:assignment-error` | D-009 #1 (owner 2026-10-17): one top-level wording in loops | the refusal defers implementing it |
| `fn-name-reserved` | D-009 #3: user functions take precedence | the refusal defers that change. Gates: filesystem-recipe 7, sha256 4, script_import_syntax 1 |
| `alias-argument` | B5 shared references (D-NEXT-refsem-b, Q3 list) | aliasing is the approved semantics; the refusal predates B5 |

### 3.4 Q: owner question (1 row)

`fn-flags`: D-047 #1 / D-048 #1 approve rejecting plain `@fn` before execution in **both** engines. The legacy side
was declined, and `known-limitations.md` itself says the VM-only refusal is not what was approved. Representative:
`@fn(v()) { return 3 }` / `print(v())`: legacy prints `3`, exit 0; VM `fn-flags`, exit 1. **Question:** does D-048's
rejection-before-execution cover a VM-only refusal now that the legacy change is declined?

### 3.5 D (39 rows): roots and the root-free check

| Rows | Root | Check |
| --- | --- | --- |
| `method:array_value`, `method:command`, `method:count`, `method:fixed`, `method:to_global` | fn-param / fn-flags (variadic struct methods, v44_struct_variadic_methods ok.f) | C112: the same struct with fixed parameters (and a parameter not named `args`, which is `param-name-reserved`) runs, legacy-equal |
| `method:stringify` | fn-param / builtin-arity:copy | review L-2 |
| `method:call` | fn-param (non-array-spread.f) | C112: `fn(call(xs))` with `a.call(x)` runs, legacy-equal |
| `method:hidden` | fn-param, then struct-private (private.f calls a private method from outside; legacy errors `private struct method`) | C112: without the variadic it refuses with `struct-private` (legacy's error), legal private use runs; its own reason never surfaces |
| `method:read`, `method:helper`, `method:call_helper` | struct-field-init | review L-2 |
| `method:call_step`, `method:public_helper` | struct-field-init (a lambda field in internal.f) | C112: the vault without lambda fields runs (10/11/2, legacy-equal); `public_helper` is itself a lambda field, so its only form is the root: a struct with just that field refuses with `struct-field-init` |
| `type:push` | struct-field-init / path-in-call-argument | review L-2 |
| `method:catch`, `method:error`, `method:throw` | fn-name-reserved (global `error`, `catch`, `throw` functions) and opaque:legacy-runtime-error (`throw (1)`) in names.f | C112: the struct with methods named error/catch/throw alone runs (14/15/16, legacy-equal) |
| `method:filter`, `method:group_by`, `method:map` | callable:inject | review L-2 |
| `method:m` | struct-after-statement | review L-2 |
| `method:push` | path-in-call-argument / struct-field-init | review L-2 |
| `method:q` | opaque:irregular-legacy-parse (`@struct`) then struct-field-init (a lambda field) | C112: without `@` it refuses with `struct-field-init`; as a plain method it runs, legacy-equal |
| `method:secret` | struct-private | review L-2 |
| `method:sum` | static-error:struct-arity | review L-2; C112: a struct method `sum` runs |
| `method:to_string` | callable:run / fn-flags / builtin-receiver:to_string | review L-2; C112: `x.to_string()` runs |
| `method:trim` | callable:run (deferred-named; `r.stdout.trim()`) | C112: `s.trim()` on a string runs, legacy-equal |
| `type:index` | prepared-decl-type / callable:inject | review L-2 |
| `method:await` | coro-future-flow:Member | review L-2 |
| `statement:Import`, `statement:TemplateStmt` | opaque:template-text (an `@script` template) | C112: the same import and the same try/catch in a plain script run, legacy-equal |
| `assign-free-name:undeclared` | assign-free-name (inventory refinement of it, refusal_inventory.cpp:69-71) | review L-2; C112: the root-free program reports `assign-free-name` |
| `free-name:undeclared` | free-name (inventory refinement, globals.cpp:179 via refusal_inventory.cpp) | C112: top level and loop variants both report `free-name`, never the sub-name |
| `unresolved-name:legacy-binding` | unresolved-name (inventory refinement for legacy special names, refusal_inventory.cpp:65-69); consequential only | C112: `print(content)` reports `unresolved-name`; tip sightings are all consequential |
| `text-type-name` | binding-name-reserved / fn-name-reserved (consequential only, gate logs) | C112: `type(x) == int` and `== number` report `unresolved-name` first (legacy errors); not reached on its own |
| `struct-construction` | context:struct-new | review L-2 |
| `builtin-bytes:decode`, `method:decode`, `method:encode` | builtin-bytes:encode | review L-2 |

Three C111 D rows failed this check and are now G: `struct-value` (section 5.7), `callable:undefined_thing` (5.2),
`coro-module-private-async` (5.1).

## 4. Gate re-runs (P2-4)

Each gate was run at the tip; every `FAIL` case line was read once (listing repeats and summary lines dropped).
Where a line carries no reason (one program's several failing checks), it inherits the reason of the program's
first line (marked *inherited*). Fail-fast gates stop at their first failing case and report 1.

### test-v410-native-dispatch-parity: 5 failing case(s) (gate reports `5 of 15 case(s)`)

| Reason | Cases |
| --- | --- |
| `callable:len` | 2 |
| `callable:to_string` | 1 |
| `callable:unknown` | 1 |
| `static-error:arity` | 1 |
| **total** | **5** |

### eval_entry: 6 failing case(s) (cp19 `eval_entry.py` (threaded run); gate reports `6 refusal(s) not accepted`)

| Reason | Cases |
| --- | --- |
| `builtin-bytes:bytes` | 3 |
| `opaque:legacy-runtime-error` | 2 |
| `unresolved-name` | 1 |
| **total** | **6** |

### test-v410-filesystem-recipe-parity: 91 failing case(s) (gate reports `91 failing case(s)`; C111's counts were doubled (each case is listed twice in the log))

| Reason | Cases |
| --- | --- |
| `postfix-spelling` | 49 |
| `builtin-lazy-args:move` | 13 |
| `builtin-arity:copy` | 13 |
| `fn-name-reserved` | 7 |
| `builtin-arity:move` | 2 |
| pending difference (rt-diff/pending, not a refusal) | 1 |
| `spread` | 1 |
| `context:assignment-error` | 1 |
| `builtin-arity:split` | 1 |
| `builtin-arity:last` | 1 |
| `unresolved-name` | 1 |
| `opaque:legacy-runtime-error` | 1 |
| **total** | **91** |

<details><summary>Per case</summary>

- FAIL stat-prepared: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (only app → `fn-name-reserved`
- FAIL stat-compatibility: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (onl → `fn-name-reserved`
- FAIL stat-wrong-arity: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (only  → `fn-name-reserved`
- FAIL stat-wrong-type: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (only a → `fn-name-reserved`
- FAIL stat-first-error: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (only  → `fn-name-reserved`
- FAIL named-stat-prepared: refused with fn-name-reserved, whose inventory status is PENDING-OWNER (on → `fn-name-reserved`
- FAIL named-stat-compatibility: refused with fn-name-reserved, whose inventory status is PENDING-OWNE → `fn-name-reserved`
- FAIL move-order: refused with builtin-lazy-args:move, whose inventory status is PENDING-OWNER (only  → `builtin-lazy-args:move`
- FAIL move-argument-mutates-source: refused with builtin-lazy-args:move, whose inventory status is PE → `builtin-lazy-args:move`
- FAIL move-first-source-error: refused with builtin-lazy-args:move, whose inventory status is PENDING → `builtin-lazy-args:move`
- FAIL move-first-destination-error: refused with builtin-lazy-args:move, whose inventory status is PE → `builtin-lazy-args:move`
- FAIL move-wrong-arity: refused with builtin-arity:move, whose inventory status is PENDING-OWNER (onl → `builtin-arity:move`
- FAIL move-cwd-boundary: refused with builtin-lazy-args:move, whose inventory status is PENDING-OWNER → `builtin-lazy-args:move`
- FAIL copy-order: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER (only appr → `builtin-arity:copy`
- FAIL copy-argument-mutates-source: refused with builtin-arity:copy, whose inventory status is PENDIN → `builtin-arity:copy`
- FAIL copy-first-source-error: refused with builtin-arity:copy, whose inventory status is PENDING-OWN → `builtin-arity:copy`
- FAIL copy-first-destination-error: refused with builtin-arity:copy, whose inventory status is PENDIN → `builtin-arity:copy`
- FAIL copy-wrong-arity: VM {'exit': 0, 'stdout': '1\n', 'stderr': '', 'files': {'b': 'B', 'a': 'A', ' → `pending difference (rt-diff/pending, not a refusal)`
- FAIL copy-cwd-boundary: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER (on → `builtin-arity:copy`
- FAIL move-unmatched-source: refused with builtin-lazy-args:move, whose inventory status is PENDING-O → `builtin-lazy-args:move`
- FAIL move-array-source: refused with builtin-lazy-args:move, whose inventory status is PENDING-OWNER → `builtin-lazy-args:move`
- FAIL move-bad-source-array: refused with builtin-lazy-args:move, whose inventory status is PENDING-O → `builtin-lazy-args:move`
- FAIL move-second-destination-error: refused with builtin-lazy-args:move, whose inventory status is P → `builtin-lazy-args:move`
- FAIL move-second-source-error: refused with builtin-lazy-args:move, whose inventory status is PENDIN → `builtin-lazy-args:move`
- FAIL move-spread: refused with builtin-arity:move, whose inventory status is PENDING-OWNER (only app → `builtin-arity:move`
- FAIL copy-unmatched-source: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER → `builtin-arity:copy`
- FAIL copy-array-source: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER (on → `builtin-arity:copy`
- FAIL copy-bad-source-array: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER → `builtin-arity:copy`
- FAIL copy-second-destination-error: refused with builtin-arity:copy, whose inventory status is PENDI → `builtin-arity:copy`
- FAIL copy-second-source-error: refused with builtin-arity:copy, whose inventory status is PENDING-OW → `builtin-arity:copy`
- FAIL copy-spread: refused with spread, whose inventory status is PENDING-OWNER (only approved or def → `spread`
- FAIL move-loop-location: refused with builtin-lazy-args:move, whose inventory status is PENDING-OWNE → `builtin-lazy-args:move`
- FAIL move-index-probe-order: refused with builtin-lazy-args:move, whose inventory status is PENDING- → `builtin-lazy-args:move`
- FAIL move-root-path-replacement: refused with builtin-lazy-args:move, whose inventory status is PEND → `builtin-lazy-args:move`
- FAIL copy-loop-location: refused with builtin-arity:copy, whose inventory status is PENDING-OWNER (o → `builtin-arity:copy`
- FAIL copy-index-probe-order: refused with builtin-arity:copy, whose inventory status is PENDING-OWNE → `builtin-arity:copy`
- FAIL copy-root-path-replacement: refused with builtin-arity:copy, whose inventory status is PENDING- → `builtin-arity:copy`
- FAIL pure#0: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#1: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#2: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#3: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#4: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#5: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#6: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#7: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#8: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#9: refused with postfix-spelling, whose inventory status is implement (only approved or de → `postfix-spelling`
- FAIL pure#10: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#11: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#12: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#13: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#14: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#15: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#16: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#17: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#18: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#19: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#20: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#21: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#22: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#23: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#24: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#25: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#26: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#27: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#28: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#29: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#30: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#31: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#32: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#33: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#34: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#35: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#36: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#37: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#38: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#39: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#40: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#41: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#42: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#43: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#44: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#45: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#46: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#47: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`
- FAIL pure#48: refused with context:assignment-error, whose inventory status is PENDING-OWNER (only a → `context:assignment-error`
- FAIL pure#52: refused with builtin-arity:split, whose inventory status is PENDING-OWNER (only approv → `builtin-arity:split`
- FAIL pure#53: refused with builtin-arity:last, whose inventory status is PENDING-OWNER (only approve → `builtin-arity:last`
- FAIL pure#54: refused with unresolved-name, whose inventory status is PENDING-OWNER (only approved o → `unresolved-name`
- FAIL pure#56: refused with opaque:legacy-runtime-error, whose inventory status is PENDING-OWNER (onl → `opaque:legacy-runtime-error`
- FAIL pure#57: refused with postfix-spelling, whose inventory status is implement (only approved or d → `postfix-spelling`

</details>

### test-v410-filesystem-recipe-guard: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `builtin-arity:copy` | 1 |
| **total** | **1** |

### test-v410-sort-factory-parity: 7 failing case(s) (gate reports `7 failing case(s)`)

| Reason | Cases |
| --- | --- |
| `for-capture-vanishing` | 2 |
| `free-name` | 1 |
| pending difference (rt-diff/pending, not a refusal) | 1 |
| `import-name-clash` | 1 |
| `method:sort_by` | 1 |
| `type:member` | 1 |
| **total** | **7** |

<details><summary>Per case</summary>

- FAIL dynamic-capture-dependency: refused with free-name, whose inventory status is PENDING-OWNER (on → `free-name`
- FAIL named-precedence: VM (0, 'int\n', '') != recorded (0, 'function\n', '') (unapproved VM/legacy d → `pending difference (rt-diff/pending, not a refusal)`
- FAIL root-path-growth: refused with for-capture-vanishing, whose inventory status is PENDING-OWNER ( → `for-capture-vanishing`
- FAIL module-ownership: refused with import-name-clash, whose inventory status is PENDING-OWNER (only → `import-name-clash`
- FAIL nested-returned-factory: refused with method:sort_by, whose inventory status is PENDING-OWNER ( → `method:sort_by`
- FAIL location-parent-growth-sort: refused with for-capture-vanishing, whose inventory status is PEND → `for-capture-vanishing`
- FAIL module-definition-error: refused with type:member, whose inventory status is PENDING-OWNER (onl → `type:member`

</details>

### script_import_syntax: 6 failing case(s)

| Reason | Cases |
| --- | --- |
| `import-dynamic` | 4 |
| `fn-name-reserved` | 1 |
| `opaque:template-text` | 1 |
| **total** | **6** |

### v44_root_path_corruption_reproducers: 3 failing case(s)

| Reason | Cases |
| --- | --- |
| `context:struct-new` | 2 |
| `declare-pattern` | 1 |
| **total** | **3** |

### v44_ast_expression_smoke: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `prepared-unpreparable` | 1 |
| **total** | **1** |

### v44_ast_constant_fold_smoke: 0 failing case(s) (passes)

No failing case.

### v44_element_assignment_smoke: 0 failing case(s) (passes (checkpoint 1))

No failing case.

### v46_output_smoke: 4 failing case(s)

| Reason | Cases |
| --- | --- |
| `callable:err` | 4 |
| **total** | **4** |

### test-v46-secure-random-cli: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `builtin-bytes:secure_random_bytes` | 1 |
| **total** | **1** |

### test-v48-bytes-index-parity: 13 failing case(s)

| Reason | Cases |
| --- | --- |
| `builtin-bytes:bytes` | 13 |
| **total** | **13** |

### test-v48-comparison-parity: 11 failing case(s)

| Reason | Cases |
| --- | --- |
| `opaque:irregular-legacy-parse` | 4 |
| `builtin-bytes:bytes` | 3 |
| `binding-name-reserved` | 2 |
| `type:<` | 2 |
| **total** | **11** |

<details><summary>Per case</summary>

- FAIL ordinary bytes equality: exit 1, refused with builtin-bytes:bytes (inventory status implement) → `builtin-bytes:bytes`
- FAIL direct if object ==: exit 1, refused with opaque:irregular-legacy-parse (inventory status PENDI → `opaque:irregular-legacy-parse`
- FAIL direct if object !=: exit 1, refused with opaque:irregular-legacy-parse (inventory status PENDI → `opaque:irregular-legacy-parse`
- FAIL direct if nested ==: exit 1, refused with opaque:irregular-legacy-parse (inventory status PENDI → `opaque:irregular-legacy-parse`
- FAIL direct if bytes ==: exit 1, refused with builtin-bytes:bytes (inventory status implement) → `builtin-bytes:bytes`
- FAIL direct if bytes !=: exit 1, refused with builtin-bytes:bytes (inventory status implement) → `builtin-bytes:bytes`
- FAIL precomputed vs direct parity: exit 1, refused with binding-name-reserved (inventory status impl → `binding-name-reserved`
- FAIL precomputed condition then direct nested: exit 1, refused with binding-name-reserved (inventory → `binding-name-reserved`
- FAIL ordinary array ordering rejected: stderr lacks [ordering comparisons require two numbers or two → `type:<`
- FAIL direct if array ordering rejected: stderr lacks [ordering comparisons require two numbers or tw → `type:<`
- FAIL direct if object ordering rejected: stderr lacks [ordering comparisons require two numbers or t → `opaque:irregular-legacy-parse`

</details>

### test-v48-callable-parity: 2 failing case(s)

| Reason | Cases |
| --- | --- |
| `prepared-decl-type` | 1 |
| `static-error:arity` | 1 |
| **total** | **2** |

### test-v48-map-set-index: 3 failing case(s)

| Reason | Cases |
| --- | --- |
| `callable:sorted_map` | 2 |
| `callable:sorted_set` | 1 |
| **total** | **3** |

### test-v48-prepared-collection-parity: 4 failing case(s)

| Reason | Cases |
| --- | --- |
| `postfix-spelling` | 2 |
| `builtin-arity:contains` | 1 |
| `builtin-arity:set` | 1 |
| **total** | **4** |

### test-v48-identifier-parity: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `binding-shadows-fn` | 1 |
| **total** | **1** |

### test-v49-selector-parity: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `for-capture-vanishing` | 1 |
| **total** | **1** |

### test-sha256: 21 failing case(s) (21 FAIL lines; the gate's own counter prints `68 passed, fail=1` (a flag, not a count))

| Reason | Cases |
| --- | --- |
| `builtin-bytes:bytes` *(inherited)* | 10 |
| `fn-name-reserved` *(inherited)* | 3 |
| `builtin-bytes:bytes` | 2 |
| `builtin-bytes:open_bytes` *(inherited)* | 2 |
| `builtin-bytes:open_bytes` | 1 |
| `builtin-arity:sha256` | 1 |
| `builtin-arity:sha256_file` | 1 |
| `fn-name-reserved` | 1 |
| **total** | **21** |

### test-parser-hardening: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `nesting-limit` | 1 |
| **total** | **1** |

### test-cp15-numeric-repair: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `number-literal` | 1 |
| **total** | **1** |

### test-sort-number-compact: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `builtin-receiver:substr` | 1 |
| **total** | **1** |

### test-cp19-s1-dispatch: 9 failing case(s) (gate's FAILED list has 9 names)

| Reason | Cases |
| --- | --- |
| `callable:run` | 6 |
| `callable:run` *(inherited)* | 2 |
| `builtin-lazy-args:setenv` | 1 |
| **total** | **9** |

<details><summary>Per case</summary>

- FAIL user function side effect once: marker='' stderr='error: <stdin>:3:3: NIFT_VM session refused:  → `callable:run`
- FAIL failed user function status non-zero → `callable:run (inherited: same program, no reason on its line)`
- FAIL $[...] evaluated once: bump='' stderr='error: <stdin>:3:3: NIFT_VM session refused: callable:ru → `callable:run`
- FAIL $[...] result dispatched → `callable:run (inherited: same program, no reason on its line)`
- FAIL executed // interpolation once: error: <stdin>:3:3: NIFT_VM session refused: callable:run: the  → `callable:run`
- FAIL executed pipeline interpolation once: error: <stdin>:3:3: NIFT_VM session refused: callable:run → `callable:run`
- FAIL interpolation left-to-right: error: <stdin>:2:16: NIFT_VM session refused: callable:run: the ca → `callable:run`
- FAIL interpolation cannot reclassify: error: <stdin>:3:18: NIFT_VM session refused: builtin-lazy-arg → `builtin-lazy-args:setenv`
- FAIL syntax validates before interpolation: error: <stdin>:3:3: NIFT_VM session refused: callable:ru → `callable:run`

</details>

### test-runtime2-cp03-simplify: 1 failing case(s)

| Reason | Cases |
| --- | --- |
| `callable:module_path` | 1 |
| **total** | **1** |

### test-runtime2-host-deps: 24 failing case(s) (24 FAIL lines; no gate total printed)

| Reason | Cases |
| --- | --- |
| `builtin-bytes:open_bytes` | 6 |
| `callable:run` | 6 |
| `host-handle-flow` | 4 |
| `callable:read` | 3 |
| `callable:cmd` | 2 |
| `type:length` | 1 |
| `statement:Command` | 1 |
| `callable:run` *(inherited)* | 1 |
| **total** | **24** |

Notes:
- filesystem-recipe: `postfix-spelling` (49 of 91) is the largest share; the `copy-wrong-arity` and sort-factory
  `named-precedence` cases are recorded pending differences (`pending_differences.tsv`), not refusals.
- `opaque:irregular-legacy-parse` in v48-comparison is ordinary code: `if({"a": 1} == {"a": 1}) { print("yes") }`
  (legacy prints `yes`; the VM refuses). It is in the syntax family (5.4).
- `callable:run` and `callable:cmd` (cp19-s1, host-deps) are deferred-named rows.

## 5. G and N families

Common to every family:
- **VM refusal form**: stderr `error: <file>:<l>:<c>: not compiled for the VM (<reason>): <text> is not supported by
  the Runtime 2.0 VM yet` (sessions: `NIFT_VM session refused: <reason>: ...`), exit 1, nothing executed.
- **Corpus** = the tip collection's full/consequential/fallback counts summed over the family's rows.
- **Affected gates** = section 4, summed over the family's rows.
- **Ordinary scripting**: the repository has no `examples/` directory; frequencies are over the 2539 `.f` test files
  and the extracted smoke heredocs where measured.
- **Approval**: none of these rows is approved; rulings cited are reasons, not approvals, unless stated.

### 5.1 Static errors raised at the wrong point (G 33, N 21)

- **Rows**: G `builtin-arity:contains`, `builtin-arity:last`, `builtin-arity:set`, `builtin-arity:sha256`, `builtin-arity:sha256_file`, `builtin-arity:split`, `builtin-receiver:substr`, `builtin-receiver:to_string`, `coro-async-value`, `coro-module-before-import`, `coro-module-private-async`, `declare-const`, `error-result-expression`, `host-handle-flow`, `jump-outside-loop`, `non-transferable`, `number-literal`, `opaque:legacy-runtime-error`, `opaque:struct-body`, `static-error:arity`, `static-error:member-assign`, `static-error:redeclared`, `static-error:struct-arity`, `struct-private`, `type:+`, `type:<`, `type:>`, `type:compound-assignment`, `type:increment`, `type:length`, `type:member`, `type:script-return`, `type:unary`.
  N `assign-catch-binding`, `await-outside-coroutine-unit`, `builtin-arity:abs`, `builtin-arity:keys`, `builtin-arity:stringify`, `builtin-arity:trim`, `builtin-receiver:empty`, `builtin-receiver:first`, `builtin-receiver:index_of`, `builtin-receiver:keys`, `builtin-receiver:sum`, `builtin-receiver:to_double`, `builtin-receiver:to_upper`, `coro-future-flow:method`, `coro-thread-callee`, `method:replace_once`, `push-receiver`, `static-error:push-arity`, `type:%`, `type:*`, `type:-`.
- **Root cause**: the compiler can prove that legacy fails here, or the construct is outside the admitted shapes, and refuses before execution instead of raising legacy's error at the point legacy raises it.
- **Raise sites**: `builtin-arity:*` compiler_keyed.cpp:105,196 and compiler_native_args.cpp:24; `builtin-receiver:*` compiler_dispatch.cpp:41; `type:<op>` compiler_expr.cpp:165,170; `static-error:arity` compiler_call.cpp:52; `declare-const` compiler_assign.cpp:47,151,205,278,315,331, compiler_element_path.cpp:86; `number-literal` compiler_literal_expr.cpp:58; `host-handle-flow` compiler_handles.cpp:190; `non-transferable` compiler_handles.cpp:195; `coro-async-value` compiler_coro.cpp:158; `coro-module-before-import` compiler_coro_module.cpp:32; `coro-module-private-async` compiler_coro_module.cpp:31; `static-error:push-arity` compiler_stmt_expr.cpp:27; `push-receiver` compiler_stmt_expr.cpp:22; `await-outside-coroutine-unit` compiler_coro.cpp:80; `assign-catch-binding` compiler_assign.cpp:332; `coro-future-flow:method` compiler_coro.cpp:68; `coro-thread-callee` compiler_thread.cpp:23.
- **Representative (a legacy error with prior output)**:

```
print("a")
print(sha256("a", "b"))
```

- Legacy (`NIFT_VM=0`): exit 1, stdout `a`, stderr `error: main.f:2:7: sha256: expected one string or bytes value`.
- VM: exit 1, stdout empty, stderr `error: main.f:2:7: not compiled for the VM (builtin-arity:sha256): 'sha256()' with this number of arguments is not supported by the Runtime 2.0 VM yet` (reason `builtin-arity:sha256`).

- **Representative (a program legacy runs: const object)**:

```
const o := {"k": 1}
print(o["k"])
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `1`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:1: not compiled for the VM (declare-const): this use of a const binding is not supported by the Runtime 2.0 VM yet` (reason `declare-const`).

- **Representative (a program legacy runs: smoke v45_adversarial_runtime/007-churn)**:

```
@fn[async](id(x)) { return x }
i := 0
while(i < 50) {
  t := thread(id, i); t.join()
  f := id(i); r := await f
  i += 1
}
```

- Legacy (`NIFT_VM=0`): exit 0, stdout empty, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:4:15: not compiled for the VM (coro-async-value): this coroutine or async usage is not supported by the Runtime 2.0 VM yet` (reason `coro-async-value`).

- **Representative (N, legacy accepts an extra argument)**:

```
o := {"a": 1}
k := o.keys(1)
print(k.size())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `1`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:6: not compiled for the VM (builtin-arity:keys): 'keys()' with this number of arguments is not supported by the Runtime 2.0 VM yet` (reason `builtin-arity:keys`).

- **Affected gates**: eval_entry: `opaque:legacy-runtime-error` 2; test-cp15-numeric-repair: `number-literal` 1; test-runtime2-host-deps: `host-handle-flow` 4, `type:length` 1; test-sha256: `builtin-arity:sha256` 1, `builtin-arity:sha256_file` 1; test-sort-number-compact: `builtin-receiver:substr` 1; test-v410-filesystem-recipe-parity: `builtin-arity:last` 1, `builtin-arity:split` 1, `opaque:legacy-runtime-error` 1; test-v410-native-dispatch-parity: `static-error:arity` 1; test-v410-sort-factory-parity: `type:member` 1; test-v48-callable-parity: `static-error:arity` 1; test-v48-comparison-parity: `type:<` 2; test-v48-prepared-collection-parity: `builtin-arity:contains` 1, `builtin-arity:set` 1.
- **Corpus** (tip, full/consequential/fallback): 47/44/42.
- **Approval**: none. `declare-const` cites D-009 #2/D-058 for the scalar form only.
- **Ordinary-scripting impact**: **not only already-failing programs** (C111 was wrong): `declare-const` refuses const objects/arrays that legacy runs (`const o := {"k": 1}` prints 1), `coro-async-value` refuses the churn smoke that legacy runs (exit 0), and `builtin-arity:keys` refuses a call legacy accepts. The rest reach only programs that fail in legacy, but the refusal is pre-execution, so legacy's prior output (e.g. `a` above) is lost.
- **PROPOSAL**: one generic "raise legacy's error at the point" mechanism (the C25 `type:print` pattern) for the legacy-error rows; implement const aggregates, the async-value flow and legacy-accepted arities as capability work.

### 5.2 Undefined names and calls (G 17, N 2)

- **Rows**: G `assign-free-name`, `binding-shadows-fn`, `callable:assert`, `callable:exit`, `callable:len`, `callable:nope`, `callable:nosuch`, `callable:to_string`, `callable:undefined_fn`, `callable:undefined_thing`, `callable:unknown`, `fn-called-before-declaration`, `method:*`, `method:nope`, `method:nosuch`, `unresolved-name`, `method:*`.
  N `callable:stringify`, `import-scope`.
- **Root cause**: an undefined callable, method or name is refused at compile time instead of raising legacy's error when reached.
- **Raise sites**: `callable:<name>` compiler_thread.cpp:22 (fallback for every unbound callee); `method:<name>` compiler_call.cpp:204 (now covered by the `method:*` family row); `assign-free-name` compiler_assign.cpp:204; `fn-called-before-declaration` resolver.cpp:128; `binding-shadows-fn` compiler_assign.cpp:17; `import-scope` module_link.cpp:356.
- **Representative**:

```
print("a")
nope(1)
```

- Legacy (`NIFT_VM=0`): exit 1, stdout `a`, stderr `error: main.f:2:1: undefined callable: nope`.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (callable:nope): the call 'nope()' is not supported by the Runtime 2.0 VM yet` (reason `callable:nope`).

- **Representative (C112 G, root-free)**:

```
try { undefined_thing() } catch (e) { print("c") }
```

- Legacy (`NIFT_VM=0`): exit 1, stdout empty, stderr `error: main.f:1:7: undefined callable: undefined_thing`.
- VM: exit 1, stdout empty, stderr `error: main.f:1:7: not compiled for the VM (callable:undefined_thing): the call 'undefined_thing()' is not supported by the Runtime 2.0 VM yet` (reason `callable:undefined_thing`).

- **Representative (N)**:

`m.nft`:

```
fn(helper(x)) {
  return x * 3
}
fn(api(x)) {
  return helper(x)
}
export(api)
```

`main.f`:

```
import("m.nft")
print(helper(4))
```

- Legacy (`NIFT_VM=0`): exit 1, stdout empty, stderr `error: main.f:2:7: undefined callable: helper`.
- VM: exit 1, stdout empty, stderr `error: main.f:2:7: not compiled for the VM (import-scope): this form of import is not supported by the Runtime 2.0 VM yet` (reason `import-scope`).

- **Affected gates**: eval_entry: `unresolved-name` 1; test-v410-filesystem-recipe-parity: `unresolved-name` 1; test-v410-native-dispatch-parity: `callable:len` 2, `callable:to_string` 1, `callable:unknown` 1; test-v48-identifier-parity: `binding-shadows-fn` 1.
- **Corpus** (tip, full/consequential/fallback): 13/27/12.
- **Approval**: `assign-free-name`, `callable:nope`, `callable:exit`, `method:nope` cite "owner ruling 2026-10-29: keep refused pending dynamic-resolution semantics"; review P1-2 found no decision text that approves it for release.
- **Ordinary-scripting impact**: low for correct scripts (legacy fails too), but prior output is lost; `binding-shadows-fn` (identifier gate) and `fn-called-before-declaration` can be reached by ordinary code that legacy runs in some orders.
- **PROPOSAL**: decide with dynamic scope (section 8): if names stay statically resolved, raise `undefined callable: <n>` / `unknown value or malformed expression` at the point.

### 5.3 Context-dependent legacy behaviour (G 8, N 6)

- **Rows**: G `context:assignment-type`, `context:number-to-int`, `context:object-length`, `jump-not-last`, `prepared-decl-type`, `prepared-fn-unprepared`, `prepared-unpreparable`, `string-arg-type`.
  N `context:modulo-assignment`, `context:negative-zero`, `context:parameter-type`, `context:string-append-type`, `context:struct-field`, `for-missing-write`.
- **Root cause**: legacy behaviour depends on the calling context (loop or prepared code, declared binding types), so the VM refuses loop-reachable or prepared forms.
- **Raise sites**: `context:number-to-int` compiler_call.cpp:196; `context:object-length` compiler_call.cpp:237; `prepared-decl-type` prepared.cpp:248; `prepared-fn-unprepared` prepared.cpp:330; `string-arg-type` prepared.cpp:370; `context:modulo-assignment` compiler_assign.cpp:295; `context:string-append-type` compiler_assign.cpp:300; `context:negative-zero` compiler_expr.cpp:240,318; `context:parameter-type` compiler_call.cpp:109, compiler_structs.cpp:370; `for-missing-write` compiler_loops_step.cpp:92; `context:struct-field` compiler_assign.cpp:203.
- **Representative**:

```
fn(n(o)) { return o.length() }
i := 0
while (i < 1) { print(n({"k": 1})); i += 1 }
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `1`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:19: not compiled for the VM (context:object-length): a context-dependent legacy behaviour (object-length) is not supported by the Runtime 2.0 VM yet` (reason `context:object-length`).

- **Representative (N, legacy runs)**:

```
s := "a" + 0.5
i := 0
while (i < 1) { s += "b"; i += 1 }
print(s)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `a0.5b`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:3:17: not compiled for the VM (context:string-append-type): a context-dependent legacy behaviour (string-append-type) is not supported by the Runtime 2.0 VM yet` (reason `context:string-append-type`).

- **Affected gates**: test-v48-callable-parity: `prepared-decl-type` 1; v44_ast_expression_smoke: `prepared-unpreparable` 1.
- **Corpus** (tip, full/consequential/fallback): 14/10/13.
- **Approval**: D-018, D-014 and D-015 are cited as "keep refused" rationale, not release approval.
- **Ordinary-scripting impact**: moderate: `.length()` on objects, `to_int` and string appends in loops are ordinary.
- **PROPOSAL**: owner rule D-015 "consistent declarations everywhere", or implement both legacy contexts.

### 5.4 Syntax and statement forms (G 9, N 6)

- **Rows**: G `binding-name-reserved`, `expression-statement`, `incdec-target`, `index-target`, `nesting-limit`, `opaque:irregular-legacy-parse`, `param-name-reserved`, `postfix-spelling`, `shebang-statement`.
  N `comparison-chain`, `object-key`, `opaque:nesting-limit`, `param-shadows-fn`, `prefix-not-precedence`, `string-literal`.
- **Root cause**: statement and expression forms legacy accepts that the VM parser or compiler does not admit.
- **Raise sites**: `expression-statement` compiler_stmt_expr.cpp:84; `shebang-statement` compiler.cpp:139; `postfix-spelling` compiler_rules.cpp:41; `index-target` compiler_rules.cpp:332, compiler_element_path.cpp:67,69; `incdec-target` compiler_assign.cpp:312,328; `param-name-reserved` resolver.cpp:77, compiler_closure.cpp:81; `nesting-limit` compiler.cpp:259; `opaque:nesting-limit` expr_parser.cpp:255,307,341, expr_postfix.cpp:98, statement_parser.cpp:50; `opaque:irregular-legacy-parse` (parser opaque marking); `comparison-chain` compiler_rules.cpp:309; `prefix-not-precedence` compiler_rules.cpp:298; `string-literal` compiler_literal_expr.cpp:62; `object-key` compiler_expr.cpp:344; `param-shadows-fn` compiler_unit.cpp:286; `binding-name-reserved` resolver.
- **Representative (ordinary code, v48-comparison)**:

```
if({"a": 1} == {"a": 1}) { print("yes") }
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `yes`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:4: not compiled for the VM (opaque:irregular-legacy-parse): source the VM parser does not structure (irregular-legacy-parse) is not supported by the Runtime 2.0 VM yet` (reason `opaque:irregular-legacy-parse`).

- **Representative**:

```
print("a")
#!/usr/bin/env nift
print("b")
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `a\nb`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (shebang-statement): a shebang line after the first line is not supported by the Runtime 2.0 VM yet` (reason `shebang-statement`).

- **Representative (owner-excluded checkpoint-1 form)**:

```
a := [1]
a[0]++
print(a[0])
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `2`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (incdec-target): this increment or decrement target is not supported by the Runtime 2.0 VM yet` (reason `incdec-target`).

- **Representative (N)**:

```
print(1 != 1 == 1)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `false`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:7: not compiled for the VM (comparison-chain): a chained comparison is not supported by the Runtime 2.0 VM yet` (reason `comparison-chain`).

- **Affected gates**: test-parser-hardening: `nesting-limit` 1; test-v410-filesystem-recipe-parity: `postfix-spelling` 49; test-v48-comparison-parity: `opaque:irregular-legacy-parse` 4, `binding-name-reserved` 2; test-v48-prepared-collection-parity: `postfix-spelling` 2.
- **Corpus** (tip, full/consequential/fallback): 17/38/14.
- **Approval**: none. `incdec-target` (`a[0]++`) is the form the owner excluded from checkpoint 1 (16:35), pinned since C107 by `assign_family.py` `refused_increment`; it is not a new finding.
- **Ordinary-scripting impact**: high: `postfix-spelling` alone is 49 of filesystem-recipe's 91 failures, and `if({...} == {...})` (irregular parse) and `a[0]++` are ordinary.
- **PROPOSAL**: compile `#!` and bare scalars as no-ops; lower the comparison and prefix rules to legacy's precedence; admit the irregular `if(` spelling and postfix spellings; `a[0]++` is for the owner to schedule (it was excluded from checkpoint 1).

### 5.5 Missing built-ins and host surfaces (G 19, N 5)

- **Rows**: G `builtin-arity:copy`, `builtin-arity:move`, `builtin-bytes:bytes`, `builtin-bytes:encode`, `builtin-bytes:open_bytes`, `builtin-bytes:secure_random_bytes`, `builtin-lazy-args:move`, `builtin-lazy-args:setenv`, `builtin-lazy-args:substr`, `builtin-lazy-args:validate`, `callable:err`, `callable:minify`, `callable:module_path`, `callable:read`, `callable:secure_random_bytes`, `callable:sorted_map`, `callable:sorted_set`, `callable:warn`, `statement:Command`.
  N `builtin-lazy-args:range`, `builtin-lazy-args:replace`, `builtin-literal-arg:validate`, `builtin-presentation-chain`, `host-literal-arg`.
- **Root cause**: the native exists but its compiler admission, lazy-argument order or receiver kind is missing; bytes have no kind bit (CP15 W8).
- **Raise sites**: `builtin-lazy-args:*` compiler_native_args.cpp:34; `host-literal-arg` compiler_native_args.cpp:27; `builtin-arg-fault-context` compiler_native_args.cpp:42; `builtin-presentation-chain` compiler_call.cpp:225; `builtin-bytes:*` compiler_native_args.cpp:9; `builtin-literal-arg:validate` compiler_native_args.cpp:16; `callable:*` compiler_thread.cpp:22; `statement:Command` compiler.cpp:156.
- **Representative**:

```
s := sorted_set()
s.add(2)
s.add(1)
print(s.size())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `2`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:6: not compiled for the VM (callable:sorted_set): the call 'sorted_set()' is not supported by the Runtime 2.0 VM yet` (reason `callable:sorted_set`).

- **Representative (N, legacy runs)**:

```
fn(f()) { print("f")
return 3 }
r := range(0, f())
print(r.size())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `f\n3`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:3:15: not compiled for the VM (builtin-lazy-args:range): lazily evaluated arguments of 'range()' is not supported by the Runtime 2.0 VM yet` (reason `builtin-lazy-args:range`).

- **Representative (N, legacy runs)**:

```
n := 1
f := file("a.txt")
f.open("w")
f.write("v=$[n]")
f.save()
f.close()
cat("a.txt")
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `v=$[n]`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:4:9: not compiled for the VM (host-literal-arg): a literal argument to a host operation is not supported by the Runtime 2.0 VM yet` (reason `host-literal-arg`).

- **Affected gates**: eval_entry: `builtin-bytes:bytes` 3; test-cp19-s1-dispatch: `builtin-lazy-args:setenv` 1; test-runtime2-cp03-simplify: `callable:module_path` 1; test-runtime2-host-deps: `builtin-bytes:open_bytes` 6, `callable:read` 3, `statement:Command` 1; test-sha256: `builtin-bytes:bytes` 12, `builtin-bytes:open_bytes` 3; test-v410-filesystem-recipe-guard: `builtin-arity:copy` 1; test-v410-filesystem-recipe-parity: `builtin-arity:copy` 13, `builtin-lazy-args:move` 13, `builtin-arity:move` 2; test-v46-secure-random-cli: `builtin-bytes:secure_random_bytes` 1; test-v48-bytes-index-parity: `builtin-bytes:bytes` 13; test-v48-comparison-parity: `builtin-bytes:bytes` 3; test-v48-map-set-index: `callable:sorted_map` 2, `callable:sorted_set` 1; v46_output_smoke: `callable:err` 4.
- **Corpus** (tip, full/consequential/fallback): 17/1/15.
- **Approval**: none; `callable:err`, `read`, `warn` and the bytes rows are implement.
- **Ordinary-scripting impact**: high for host scripting: bytes (`builtin-bytes:bytes` 13 in bytes-index, 12 in sha256, 3 in comparison, 3 in eval_entry), filesystem moves and copies (`builtin-lazy-args:move` 13, `builtin-arity:copy` 14), `err()` (all 4 v46-output failures).
- **PROPOSAL**: implement; treat lazy argument order as one general rule, not per built-in.

### 5.6 CP24 project APIs (G 9, N 0)

- **Rows**: G `callable:build`, `callable:build_all`, `callable:build_names`, `callable:inject`, `callable:status`, `callable:track`, `callable:tracked`, `callable:untrack`, `opaque:template-text`.
- **Root cause**: the project and build APIs are not bound on the VM.
- **Raise sites**: compiler_thread.cpp:22 (`callable:*`); `opaque:template-text` (parser).
- **Representative**:

```
print(tracked())
```

- Legacy (`NIFT_VM=0`): exit 1, stdout empty, stderr `error: main.f:1:7: tracked: not a Nift project (no .nift/config.json found from /tmp/claude-0/-home-claude/5262fcab-c28a-58db-987c-2c4b624387c1/scratchpad/rescan/fam/F-cp24)`.
- VM: exit 1, stdout empty, stderr `error: main.f:1:7: not compiled for the VM (callable:tracked): the call 'tracked()' is not supported by the Runtime 2.0 VM yet` (reason `callable:tracked`).

- **Affected gates**: script_import_syntax: `opaque:template-text` 1.
- **Corpus** (tip, full/consequential/fallback): 39/0/31.
- **Approval**: none.
- **Ordinary-scripting impact**: project scripts only, but `callable:inject` (24/0/24) also masks `map`, `filter`, `group_by`, `type:index` and `method:join` (auto.f, auto2.f).
- **PROPOSAL**: a CP24-on-VM checkpoint.

### 5.7 Structs (G 5, N 7)

- **Rows**: G `context:struct-new`, `presentation-struct-method:*`, `struct-after-statement`, `struct-field-init`, `struct-value`.
  N `struct-assign`, `struct-member-clash`, `struct-name`, `struct-nested`, `struct-no-field`, `struct-redeclared`, `struct-shadows-fn`.
- **Root cause**: struct features outside the admitted subset: construction and defaults in loops, non-constant defaults, declarations after statements, reassigning struct bindings, instances as values, and presentation choice in sessions.
- **Raise sites**: `context:struct-new` compiler_structs.cpp:278; `struct-field-init` structs.cpp:109; `struct-after-statement` structs.cpp:88; `struct-value` compiler_assign.cpp:140, compiler_structs.cpp:64; `struct-assign` compiler_assign.cpp:209; `struct-shadows-fn` structs.cpp:93; `struct-member-clash` structs.cpp:103; `struct-name` structs.cpp:91; `struct-nested` structs.cpp:78; `struct-no-field` compiler_structs.cpp:71; `struct-redeclared` structs.cpp:92; `presentation-struct-method:*` compiler_presentation.cpp.
- **Representative (non-constant default, legacy runs)**:

```
struct(S) {
  xs := [1]
  fn(add(v)) { xs.push(v) }
}
s := S()
s.add(2)
print(s.xs.stringify())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `[1,2]`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:3: not compiled for the VM (struct-field-init): this struct feature is not supported by the Runtime 2.0 VM yet` (reason `struct-field-init`).

- **Representative (C112 G, root-free)**:

```
struct(P) {
  v := 1
}
struct(Q) {
  v := 2
}
fn(pick(b)) {
  if (b) { return P() }
  return Q()
}
x := pick(true)
x.v = 9
print(x.v)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `9`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:12:1: not compiled for the VM (struct-value): this struct feature is not supported by the Runtime 2.0 VM yet` (reason `struct-value`).

- **Representative (N, legacy runs)**:

```
struct(P) {
  v := 1
}
x := P()
x = P()
x.v = 3
print(x.v)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `3`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:5:1: not compiled for the VM (struct-assign): this struct feature is not supported by the Runtime 2.0 VM yet` (reason `struct-assign`).

- **Representative (restored G, session)**:

session (`nift -i`) input:

```
struct(P) {
  v := 1
  fn(stringify()) { return "P!" }
}
xs := [P()]
print(xs[0].stringify())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `P{v:1}`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: <stdin>:6:7: NIFT_VM session refused: presentation-struct-method:stringify: '.stringify()' on a value that may be an instance of a struct declaring its own 'stringify()' method (choosing the method or the built-in at run ti` (reason `presentation-struct-method:stringify`).

- **Affected gates**: v44_root_path_corruption_reproducers: `context:struct-new` 2.
- **Corpus** (tip, full/consequential/fallback): 13/2/11.
- **Approval**: `presentation-struct-method:*`: the B3 ruling (2026-11-05) approves that an explicit `.stringify()` calls the struct's own method (so the VM may differ from legacy's `P{v:1}`); B4 implemented the run-time choice for scripts and modules only, so the session refusal is the gap. The others: none.
- **Ordinary-scripting impact**: high: `x = P()` (`struct-assign`), returning either of two structs (`struct-value`), array defaults with push methods (`struct-field-init`) and structs declared after a `print` (`struct-after-statement`) are ordinary. The escaped-ref smoke fails on `context:struct-new` (section 11).
- **PROPOSAL**: checkpoint 2 takes `context:struct-new` and `struct-construction`; see the scope PROPOSAL in section 10 for the rest. The legacy-error rows (`struct-no-field` and the declaration checks) fit the 5.1 raise-at-point mechanism.

### 5.8 Coroutine and loop-capture flow (G 2, N 0)

- **Rows**: G `coro-future-flow:Member`, `for-capture-vanishing`.
- **Root cause**: a for-loop body that captures the element while the source may shrink, and `.await()` method spelling on a future.
- **Raise sites**: `for-capture-vanishing` compiler_loops_step.cpp:90; `coro-future-flow:Member` compiler_coro.cpp:36,68.
- **Representative**:

```
a := [[1]]
for(row : a) { f := x => row[0]; a.push([9]); print(f(0)) }
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `1`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (for-capture-vanishing): this for-loop form is not supported by the Runtime 2.0 VM yet` (reason `for-capture-vanishing`).

- **Affected gates**: test-v410-sort-factory-parity: `for-capture-vanishing` 2; test-v49-selector-parity: `for-capture-vanishing` 1.
- **Corpus** (tip, full/consequential/fallback): 1/0/1.
- **Approval**: none.
- **Ordinary-scripting impact**: moderate: capturing the loop element in a lambda while appending to the source is uncommon but appears in sort-factory (2) and v49-selector (1).
- **PROPOSAL**: implement the vanishing-element capture (copy the element into the closure as legacy does); raise legacy's error for `.await()`.

### 5.9 Kind inference (G 2, N 3)

- **Rows**: G `interpolation:unknown-type`, `method:sort_by`.
  N `builtin-arg-fault-context`, `interpolation:non-scalar`, `postfix-receiver:member`.
- **Root cause**: the compiler cannot prove a value's kind where legacy dispatches at run time.
- **Raise sites**: `interpolation:unknown-type` compiler_interp.cpp:274; `interpolation:non-scalar` compiler_interp.cpp:275; `method:sort_by` compiler_call.cpp:204; `postfix-receiver:member` compiler_rules.cpp:53; `builtin-arg-fault-context` compiler_native_args.cpp:42.
- **Representative**:

```
fn(f(x)) { return "v=$[x]" }
print(f(1))
print(f("s"))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `v=1\nv=s`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:24: not compiled for the VM (interpolation:unknown-type): this string interpolation (unknown-type) is not supported by the Runtime 2.0 VM yet` (reason `interpolation:unknown-type`).

- **Representative (N, legacy errors)**:

```
fn(f(x)) { return x }
v := f([1])
print("v=$[v]")
```

- Legacy (`NIFT_VM=0`): exit 1, stdout empty, stderr `error: main.f:3:1: print: parameter expression must resolve to a scalar value`.
- VM: exit 1, stdout empty, stderr `error: main.f:3:12: not compiled for the VM (interpolation:non-scalar): this string interpolation (non-scalar) is not supported by the Runtime 2.0 VM yet` (reason `interpolation:non-scalar`).

- **Affected gates**: test-v410-sort-factory-parity: `method:sort_by` 1.
- **Corpus** (tip, full/consequential/fallback): 2/0/0.
- **Approval**: none.
- **Ordinary-scripting impact**: high: interpolating a parameter of mixed kind is ordinary.
- **PROPOSAL**: a run-time-kind render path for interpolation; dynamic dispatch for `sort_by` on returned factories.

### 5.10 Portability (G 1, N 0)

- **Rows**: G `method:sort`.
- **Root cause**: comparator sorts are not certified across toolchains.
- **Raise sites**: `method:sort` compiler_call.cpp:204.
- **Representative**:

```
xs := [3, 1, 2]
xs.sort((a, b) => a < b)
print(xs.stringify())
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `[1,2,3]`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (method:sort): the method call '.sort()' is not supported by the Runtime 2.0 VM yet` (reason `method:sort`).

- **Affected gates**: none of the gates re-run in section 4.
- **Corpus** (tip, full/consequential/fallback): 0/0/0.
- **Approval**: "owner 2026-10-25 comparator profile" is cited but the row is not approved.
- **Ordinary-scripting impact**: moderate: comparator sorts are ordinary.
- **PROPOSAL**: verify on GCC 15.2 on Linode (section 12), then rule.

### 5.11 Aliasing (G 1, N 0)

- **Rows**: G `path-in-call-argument`.
- **Root cause**: an element path passed as a call argument is not admitted.
- **Raise sites**: `path-in-call-argument` compiler_call.cpp:278 (and the `:imported` variant).
- **Representative**:

```
o := {"k": 0}
a := [5]
fn(id(x)) { return x }
print(a[id(o.k)])
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `5`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:4:12: not compiled for the VM (path-in-call-argument): an element path as a call argument is not supported by the Runtime 2.0 VM yet` (reason `path-in-call-argument`).

- **Affected gates**: none of the gates re-run in section 4.
- **Corpus** (tip, full/consequential/fallback): 2/0/2.
- **Approval**: none; the row is implement.
- **Ordinary-scripting impact**: moderate: `f(o.k)` inside an index is ordinary; it also masks `method:push`.
- **PROPOSAL**: implement under B5's shared-reference rules.

### 5.12 CP24 / modules: exports and imports (G 8, N 8)

- **Rows**: G `export-struct`, `import-collision`, `import-dynamic`, `import-name-clash`, `module-bare-return`, `module-host-op:cat`, `module-host-op:exists`, `statement:Export`.
  N `import-limit`, `import-reinitialised`, `module-host-op:cp`, `module-host-op:file`, `module-host-op:getenv`, `module-host-op:is_dir`, `module-host-op:is_file`, `module-host-op:remove`.
- **Root cause**: see section 7.
- **Raise sites**: see section 7.
- **Representative**:

```
fn(twice(n)) { return n * 2 }
export(twice)
print(twice(2))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `4`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (statement:Export): an export statement in this position is not supported by the Runtime 2.0 VM yet` (reason `statement:Export`).

- **Affected gates**: script_import_syntax: `import-dynamic` 4; test-v410-sort-factory-parity: `import-name-clash` 1.
- **Corpus** (tip, full/consequential/fallback): 56/0/43.
- **Approval**: see section 7.
- **Ordinary-scripting impact**: see section 7.
- **PROPOSAL**: see section 7.

### 5.13 Variadics, spread and destructuring (G 4, N 0)

- **Rows**: G `declare-pattern`, `fn-param`, `lambda-variadic`, `spread`.
- **Root cause**: see section 6.
- **Raise sites**: see section 6.
- **Representative**:

```
fn(sum(...xs)) { t := 0; for (x : xs) { t += x }; return t }
print(sum(1, 2, 3))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `6`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:11: not compiled for the VM (fn-param): this function declaration form is not supported by the Runtime 2.0 VM yet` (reason `fn-param`).

- **Affected gates**: test-v410-filesystem-recipe-parity: `spread` 1; v44_root_path_corruption_reproducers: `declare-pattern` 1.
- **Corpus** (tip, full/consequential/fallback): 10/1/6.
- **Approval**: see section 6.
- **Ordinary-scripting impact**: see section 6.
- **PROPOSAL**: see section 6.

### 5.14 Dynamic scope (G 2, N 0)

- **Rows**: G `free-name`, `free-name:dynamic-caller`.
- **Root cause**: see section 8.
- **Raise sites**: see section 8.
- **Representative**:

```
fn(read_hidden()) { return hidden }
fn(mk(hidden)) { return read_hidden() }
print(mk(7))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `7`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:28: not compiled for the VM (free-name): a name that is not declared in this scope is not supported by the Runtime 2.0 VM yet` (reason `free-name`).

- **Affected gates**: test-v410-sort-factory-parity: `free-name` 1.
- **Corpus** (tip, full/consequential/fallback): 1/0/2.
- **Approval**: see section 8.
- **Ordinary-scripting impact**: see section 8.
- **PROPOSAL**: see section 8.

## 6. Variadics, spread and destructuring

- **Raise sites**: `fn-param` resolver.cpp:70; `lambda-variadic` compiler_closure.cpp:80; `spread` compiler_call.cpp:87,160;
  `declare-pattern` compiler_assign.cpp:48.

```
fn(sum(...xs)) { t := 0; for (x : xs) { t += x }; return t }
print(sum(1, 2, 3))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `6`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:11: not compiled for the VM (fn-param): this function declaration form is not supported by the Runtime 2.0 VM yet` (reason `fn-param`).

```
fn(add(a, b)) { return a + b }
xs := [1, 2]
print(add(...xs))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `3`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:3:11: not compiled for the VM (spread): the spread operator is not supported by the Runtime 2.0 VM yet` (reason `spread`).

```
f := (...xs) => xs.length()
print(f(1, 2))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `2`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:10: not compiled for the VM (lambda-variadic): a variadic lambda is not supported by the Runtime 2.0 VM yet` (reason `lambda-variadic`).

```
[x, y] := [1, 2]
print(x + y)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `3`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:1: not compiled for the VM (declare-pattern): this declaration pattern is not supported by the Runtime 2.0 VM yet` (reason `declare-pattern`).

- **Affected gates**: test-v410-filesystem-recipe-parity: `spread` 1; v44_root_path_corruption_reproducers: `declare-pattern` 1.
- **Corpus**: 10/1/6. `fn-param` masks 8 `method:*` rows (section 3.5) and `method:join` (ok.f).
- **Frequency**: `...name` in 21 test `.f` files, destructuring declarations in 5.
- **Approval**: none; `declare-pattern` is implement. **Release gate**: the escaped-ref smoke's `destruct` fails on it.
- **PROPOSAL**: one variadic/spread checkpoint (rest parameters, call spread, lambda rest), destructuring next to it.

## 7. Exports and modules

- **Rows**: G `export-struct`, `import-collision`, `import-dynamic`, `import-name-clash`, `module-bare-return`, `module-host-op:cat`, `module-host-op:exists`, `statement:Export`; N `import-limit`, `import-reinitialised`, `module-host-op:cp`, `module-host-op:file`, `module-host-op:getenv`, `module-host-op:is_dir`, `module-host-op:is_file`, `module-host-op:remove`.
- **Root cause**: `export(...)` in a directly run unit is not compiled; module initialisation with host operations, struct
  exports, name clashes, dynamic imports, repeated initialisation and deep import chains are refused.
- **Raise sites**: `statement:Export` compiler.cpp:156; `export-struct` module_graph.cpp:350; `import-name-clash`
  module_link.cpp:338; `module-bare-return` module_link.cpp:304; `module-host-op:*` module_host_op.h:29; `import-dynamic`
  (import resolver); `import-reinitialised` module_graph.cpp:271; `import-limit` module_graph.cpp:275.

```
fn(twice(n)) { return n * 2 }
export(twice)
print(twice(2))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `4`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:2:1: not compiled for the VM (statement:Export): an export statement in this position is not supported by the Runtime 2.0 VM yet` (reason `statement:Export`).

`main.f`:

```
import("./m.f")
p := P()
print(p.v)
```

`m.f`:

```
struct(P) { v := 3 }
export(P)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `3`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: m.f:2:1: not compiled for the VM (export-struct): exporting a struct from a module is not supported by the Runtime 2.0 VM yet` (reason `export-struct`).

`m.nft`:

```
x := 1
export(x)
```

`m2.nft`:

```
x := 2
export(x)
```

`main.f`:

```
cp("m2.nft", "m.nft")
import("m.nft")
print(x)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `2`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:1: not compiled for the VM (module-host-op:cp): the host operation 'cp()' in a program with imports is not supported by the Runtime 2.0 VM yet` (reason `module-host-op:cp`).

`m.f`:

```
print("init")
k := 1
export(k)
```

`a.f`:

```
import("./m.f")
fn(fa()) { return k }
export(fa)
```

`main.f`:

```
import("./m.f")
import("./a.f")
print(k)
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `init\ninit\n1`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: a.f:1:1: not compiled for the VM (import-reinitialised): this form of import is not supported by the Runtime 2.0 VM yet` (reason `import-reinitialised`).

- **Affected gates**: script_import_syntax: `import-dynamic` 4; test-v410-sort-factory-parity: `import-name-clash` 1.
- **Corpus**: 56/0/43; `statement:Export` (45/0/33) is the largest single G reason. `export(` appears in 197 test `.f` files.
- **Approval**: `import-dynamic` cites D-037 (owner 2026-10-20: rejected before execution) with "G2 release approval
  pending", so it is G. `import-reinitialised`: D-041 #2 allows this refusal temporarily, only if the engines land
  separately and with a recorded removal condition; no such condition is recorded (owner question).
- **Impact**: high; a library file that is also run directly, and diamond imports (legacy initialises twice), are ordinary.
- **PROPOSAL**: compile `export(...)` in an entry unit as a no-op; admit module-init host operations in order (pinned by
  module_host_order); the owner states whether D-037 is the G2 approval for `import-dynamic` and records a removal
  condition (or not) for `import-reinitialised`.

## 8. Dynamic scope

```
fn(read_hidden()) { return hidden }
fn(mk(hidden)) { return read_hidden() }
print(mk(7))
```

- Legacy (`NIFT_VM=0`): exit 0, stdout `7`, stderr empty.
- VM: exit 1, stdout empty, stderr `error: main.f:1:28: not compiled for the VM (free-name): a name that is not declared in this scope is not supported by the Runtime 2.0 VM yet` (reason `free-name`).

- **Rows**: `free-name`, `free-name:dynamic-caller` (G); `free-name:undeclared` (D, inventory refinement).
- **Raise site**: globals.cpp:170-186 (plan) and the compiler's name resolution.
- **Affected gates**: test-v410-sort-factory-parity: `free-name` 1.
- **Corpus**: 1/0/2. **Approval**: none (2026-10-29 "keep refused" is cited; P1-2 found no decision text).
- **PROPOSAL**: rule legacy dynamic scope out of the release contract (class X), or keep it as a blocker; this also decides 5.2.

## 9. Reason literals in src/rt without a tsv row (P2-6)

A scan of `fail`/`refuse`/`add` calls and `reason =` assignments in `src/rt` finds 221 reason literals; 108 had no tsv
row at C111 (the review's scan found 83; this scan is wider and includes non-compile codes). Each has one disposition:

| Disposition | Count |
| --- | --- |
| session-only | 30 |
| N row added | 16 |
| not a compile refusal | 14 |
| unreachable | 13 |
| not reached | 13 |
| parser-shadowed | 5 |
| consequential-only | 3 |
| inventory refinement of `free-name` | 3 |
| unreachable in practice | 3 |
| eval-entry only | 2 |
| classify-all/session only | 2 |
| resolver-shadowed | 2 |
| unreachable as a refusal | 2 |
| **total** | **108** |

<details><summary>All 108</summary>

| Literal | Disposition |
| --- | --- |
| `analysis-limit:kinds` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `assign-catch-binding` | N row added (reachable; program in its evidence) |
| `assign-target` | consequential-only (tip corpus: consequential sightings only; probes give another reason first) |
| `await-shape` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `binary-operator` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `binding-call-reserved` | resolver-shadowed: binding-name-reserved fires first; legacy errors |
| `builtin-literal-arg:validate` | N row added (reachable; program in its evidence) |
| `builtin-plain-arg:` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `cleanup-incomplete` | not a compile refusal: an isolate-transfer, handle or process error code |
| `compound-operator` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `context-end` | not a compile refusal: an isolate-transfer, handle or process error code |
| `context:collection-empty` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `context:struct-field` | N row added (reachable; program in its evidence) |
| `coro-async-recursion` | classify-all/session only: needs parallel_tasks off (F12 default limits) or a session unit (refused as session:coroutine first) |
| `coro-capture` | classify-all/session only: needs parallel_tasks off (F12 default limits) or a session unit (refused as session:coroutine first) |
| `coro-future-flow:method` | N row added (reachable; program in its evidence) |
| `coro-future-flow:type` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `coro-observe:epoch` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `coro-task-loop` | consequential-only (tip corpus: consequential sightings only; probes give another reason first) |
| `coro-task-native:` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `coro-task-op:` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `coro-task-recursion` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `coro-thread-callee` | N row added (reachable; program in its evidence) |
| `entry-out-of-range` | not a compile refusal: an isolate-transfer, handle or process error code |
| `eval:error-aggregate-presentation` | eval-entry only (cp19 evaluated expressions, not script or session units) |
| `eval:quoted-callback` | eval-entry only (cp19 evaluated expressions, not script or session units) |
| `execution-error` | not a compile refusal: an isolate-transfer, handle or process error code |
| `expression-nesting` | parser-shadowed: the parser's opaque:nesting-limit fires first (row added in C111) |
| `expression:` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `expression:assignment-target` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `file-capture` | not a compile refusal: an isolate-transfer, handle or process error code |
| `free-name:before-declaration` | inventory refinement of `free-name` (globals plan names the sub-case; the runner reports free-name, which has a row) |
| `free-name:dynamic-shadow` | inventory refinement of `free-name` (globals plan names the sub-case; the runner reports free-name, which has a row) |
| `free-name:not-one-top-level-binding` | inventory refinement of `free-name` (globals plan names the sub-case; the runner reports free-name, which has a row) |
| `import-limit` | N row added (reachable; program in its evidence) |
| `import-link` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `import-outside-project` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `import-parse-error` | not reached: a module parse error is reported as module-error:syntax (row exists), legacy-equal message |
| `import-reinitialised` | N row added (reachable; program in its evidence) |
| `interpolation:internal` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `interpolation:non-scalar` | N row added (reachable; program in its evidence) |
| `invalid-graph` | not a compile refusal: an isolate-transfer, handle or process error code |
| `invalid-input` | not a compile refusal: an isolate-transfer, handle or process error code |
| `lambda-capture` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `lambda-param` | parser-shadowed: opaque:legacy-runtime-error first; legacy errors (malformed parameter list, duplicate key, invalid signature) |
| `lambda-shape` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `limit:arguments` | unreachable in practice: capacity limit (0x7fff arguments, 0xffff atoms or lambdas) |
| `limit:atoms` | unreachable in practice: capacity limit (0x7fff arguments, 0xffff atoms or lambdas) |
| `limit:lambdas` | unreachable in practice: capacity limit (0x7fff arguments, 0xffff atoms or lambdas) |
| `limit:session-bindings` | session-only (the session compiler's contract checks) |
| `limit:session-protos` | session-only (the session compiler's contract checks) |
| `logical-operator` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `method:splice` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `object-duplicate-key` | parser-shadowed: opaque:legacy-runtime-error first; legacy errors (malformed parameter list, duplicate key, invalid signature) |
| `param-duplicate` | parser-shadowed: opaque:legacy-runtime-error first; legacy errors (malformed parameter list, duplicate key, invalid signature) |
| `param-type-unpredicted` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `parse-error` | not a compile refusal: the runner's stop reason for a parse error (legacy-identical diagnostic, CP10) |
| `path-in-call-argument:imported` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `path-in-index` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found |
| `pipe-capture` | not a compile refusal: an isolate-transfer, handle or process error code |
| `postfix-receiver` | consequential-only (tip corpus: consequential sightings only; probes give another reason first) |
| `proto-unavailable` | not a compile refusal: an isolate-transfer, handle or process error code |
| `push-receiver` | N row added (reachable; program in its evidence) |
| `refused-future` | not a compile refusal: an isolate-transfer, handle or process error code |
| `refused-handle` | not a compile refusal: an isolate-transfer, handle or process error code |
| `session:active-historical-call` | session-only (the session compiler's contract checks) |
| `session:binding-contract` | session-only (the session compiler's contract checks) |
| `session:binding-shape-contract` | session-only (the session compiler's contract checks) |
| `session:binding-struct-contract` | session-only (the session compiler's contract checks) |
| `session:call-alias-contract` | session-only (the session compiler's contract checks) |
| `session:call-kind-contract` | session-only (the session compiler's contract checks) |
| `session:call-signature` | session-only (the session compiler's contract checks) |
| `session:callable-flow` | session-only (the session compiler's contract checks) |
| `session:callable-rebind` | session-only (the session compiler's contract checks) |
| `session:capture-layout` | session-only (the session compiler's contract checks) |
| `session:captured-struct` | session-only (the session compiler's contract checks) |
| `session:coroutine` | session-only (the session compiler's contract checks) |
| `session:current-callable-proof` | session-only (the session compiler's contract checks) |
| `session:dynamic-shadow` | session-only (the session compiler's contract checks) |
| `session:element-contract` | session-only (the session compiler's contract checks) |
| `session:lambda-assignment-spelling` | session-only (the session compiler's contract checks) |
| `session:legacy-method-precedence` | session-only (the session compiler's contract checks) |
| `session:parse` | session-only (the session compiler's contract checks) |
| `session:redeclared` | session-only (the session compiler's contract checks) |
| `session:redefinition-callable-flow` | session-only (the session compiler's contract checks) |
| `session:redefinition-order` | session-only (the session compiler's contract checks) |
| `session:reserved-binding-contract` | session-only (the session compiler's contract checks) |
| `session:shadow-or-redeclare` | session-only (the session compiler's contract checks) |
| `session:struct-callable-flow` | session-only (the session compiler's contract checks) |
| `session:struct-dynamic-shadow` | session-only (the session compiler's contract checks) |
| `session:struct-name-conflict` | session-only (the session compiler's contract checks) |
| `session:struct-redeclared` | session-only (the session compiler's contract checks) |
| `session:widening-callable-flow` | session-only (the session compiler's contract checks) |
| `struct-assign` | N row added (reachable; program in its evidence) |
| `struct-member-clash` | N row added (reachable; program in its evidence) |
| `struct-method` | parser-shadowed: opaque:struct-body first (legacy errors 'struct fields require name := initializer') |
| `struct-name` | N row added (reachable; program in its evidence) |
| `struct-nested` | N row added (reachable; program in its evidence) |
| `struct-no-field` | N row added (reachable; program in its evidence) |
| `struct-redeclared` | N row added (reachable; program in its evidence) |
| `struct-shadows-fn` | N row added (reachable; program in its evidence) |
| `struct-this-binding` | resolver-shadowed: binding-name-reserved fires first; legacy errors |
| `throw-expression` | unreachable as a refusal: the parse error comes first and is legacy-identical |
| `trailing-comma` | unreachable as a refusal: the parse error comes first and is legacy-identical |
| `unary-operator` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `unknown-effects` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) |
| `unsupported-constant` | not a compile refusal: an isolate-transfer, handle or process error code |
| `unsupported-value` | not a compile refusal: an isolate-transfer, handle or process error code |

</details>

Probe notes for the "not reached" rows: `a.empty()` in a loop-called function, an async function returning epoch(),
a non-suspending loop, print and recursion in tasks, `splice` with a sorted argument, a reassigned parameter, an
index inside a call argument, and an import outside a project root all ran or gave another reason. **Observation**
(not a refusal): an async function awaiting its own recursive call runs on the VM (prints 0) where legacy errors
`future: return: await: expected future`; flagged for the owner (section 13).

The N evidence of every C111 N row is now a minimal program with its exact VM reason and legacy output (in the tsv).

## 10. Src-side and scope proposals (none applied)

1. **F12, inventory limits (corrected).** In `src/rt/refusal_inventory.cpp`, add `#include "rt/compiler_coro_admit.h"`
   and, after `CompileLimits limits;` (line 163), `cooperative_switches(limits);` as `script_runner.cpp:311` does. C112
   built and collected it in a scratch mutant: 16 of the 17 F12 reasons are no longer seen; `method:join` keeps 9
   non-consequential sightings (section 3.2), so it is not retirable. The fix also unmasks `coro-async-value` (+1) and
   `binding-name-reserved` (+1), both of which have rows, and drops one approved `method:get` sighting. It changes
   nift's sha256 (the file is linked into nift).
2. **Nesting names.** Name the parser and compiler limits distinctly (e.g. `parse-nesting-limit`, `compile-nesting-limit`).
3. **`type:*` name collision.** The `*` operator's reason `type:*` is also the inventory's family-row syntax, so its row
   acts as the fallback for every unlisted `type:<op>` reason (still PENDING-OWNER, never an approval). Rename the
   emitter's reason (e.g. `type:mul`).
4. **Checkpoint-2 scope (review L-3), PROPOSAL for the owner:**
   - CP2 also takes `struct-after-statement`: reachable on its own in ordinary code (`print("a")` then a struct;
     legacy prints `a`, `1`); after CP2's struct-in-loops work such scripts would still be refused; it masks `method:m`.
   - CP2 also takes `struct-value`: root-free reproducer `pick(true)` returning either of two structs, then `x.v = 9`;
     legacy prints 9.
   - CP2 also takes non-constant array defaults (`struct-field-init`): `xs := [1]` with a push method; legacy prints `[1,2]`.
   - CP2 does not take `struct-private`: it mostly reproduces legacy errors (`private struct field: v`), and legal
     private use already runs on the VM.
   - `struct-construction` (derivative of `context:struct-new`) belongs with CP2's construction-in-loops work.
   - Also for the owner: `struct-assign` (N, `x = P()`, legacy prints 3) is the same struct-as-value gap.
   - No implement row was added or removed; the 13:26 implement rows for checkpoints 2-5 stay implement (only the
     E1 narrowing of `compound-target`/`index-assign-target` changed scope).

## 11. Release-gate issue, still outstanding

`tests/v44_root_path_corruption_reproducers.sh` **FAILS** at the tip (re-run in C112): `destruct` (`declare-pattern`),
`structarr` and `mapstruct` (`context:struct-new`). `escaped-ref` passes. The file is not passing; the failures are
recorded on the `declare-pattern` and `context:struct-new` rows.

## 12. Linode-only items (not run here)

- The execute pass on the release toolchain: `make cp25r-inventory`.
- `method:sort` comparator portability on GCC 15.2 (cp14b-sort's 234 unverified cases).
- Sanitizer runs of the gates in section 4.

## 13. Owner questions

1. `fn-flags`: does D-048's rejection-before-execution cover a VM-only refusal, with the legacy change declined?
2. `import-dynamic`: is D-037 its G2 release approval?
3. `import-reinitialised` (N): D-041 #2 allows a temporary refusal only with a recorded removal condition; record one, or treat it as G?
4. Dynamic scope (`free-name`): out of the release contract (X), or a blocker?
5. Checkpoint-2 scope: accept the section 10 item 4 recommendation?
6. `incdec-target` (`a[0]++`, excluded from checkpoint 1): which checkpoint takes it?
7. Approve the F12 inventory fix (a src change; changes the sha256), after which the 16 F12 rows can be retired?
8. `method:*` family row: keep it (classifies generic method names as PENDING-OWNER), or require one row per name?
9. Observation: async self-recursion runs on the VM where legacy errors (section 9); is that an accepted difference?
10. Family dispositions in sections 5-8 (each a PROPOSAL).

## 14. Gates at the tip (C112)

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS (historical control; classification self-test, 31 cases) |
| `make test-rt-lint` | PASS (boundary: 0 problems in 710 files) |
| `make test-test-integrity` | clean (700 files scanned, 0 findings) |
| fresh `inventory.py collect` + `report --check` | **exit 3** |
| `python3 -B tests/runtime2/assign_family.py --nift $PWD/nift` | PASS (94 cases: 47 legacy-equal, 47 pinned differences or refusals) |

- **Inventory**: G1 PASS, 155 reasons seen (approved 8, deferred-named 17, implement 17, PENDING-OWNER 113, retired 0).
  Exit 3 because 113 seen reasons are PENDING-OWNER: the intended BLOCKED state. No retired reason is seen again; no N
  row's reason is seen (the inventory corpora do not contain their programs; sessions are not in the corpora, which is
  why `presentation-struct-method:*` could not be caught by `--check`).
- **Production code**: `git diff 0c66cad6 -- src/` is empty; nift sha256 is
  `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.
- **Scratch**: the F12 mutant (`.build/mutant-nift`), its collection and the smoke extraction were deleted.

## 15. C112 changes per review finding

| Finding | Change |
| --- | --- |
| P2-1 | `presentation-struct-method:*` restored to PENDING-OWNER, class G (session reproducer in its evidence); C111's claims corrected; `method:increment` stays retired with a reworded cause; `method:*` family row added |
| P2-2 | `struct-value`, `callable:undefined_thing` D → G; all 42 D rows re-checked (section 3.5): `coro-module-private-async` also → G; 39 D remain |
| P2-3 | `declare-const`, `param-name-reserved`, `interpolation:unknown-type` evidence replaced; all 12 repaired programs re-run, each emitting its own reason (recorded in the tsv) |
| P2-4 | gates re-run; one table per failing gate (section 4); family gate fields rebuilt from them |
| P2-5 | static impact corrected; struct representative replaced; every family has all owner fields |
| P2-6 | `incdec-target` → G (owner-excluded, pinned since C107); `callable:prit` → E2; `builtin-receiver:stringify` retired; 108 literals triaged; 16 N rows added; N evidence replaced by programs |
| P2-7 | F12 proposal corrected and measured; `method:join` E2+D, not retirable |
| L-1 | `fn-flags` → Q (owner question) |
| L-3 | checkpoint-2 recommendation added as a PROPOSAL |
| new | `type:*` family-syntax collision noted (row and proposal) |
