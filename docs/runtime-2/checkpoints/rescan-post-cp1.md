# Post-checkpoint-1 inventory rescan (C111)

Base: frozen checkpoint-1 candidate `0c66cad6` (owner 21:30: CHECKPOINT 1 ACCEPTED). nift sha256
`8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`, unchanged; `git diff 0c66cad6 -- src/`
is empty. This rescan changes only `tests/runtime2/cp25/classification.tsv` and the docs. It approves
nothing and defers nothing. Every class and disposition below is input for the owner; **every proposed
disposition is a PROPOSAL.** The owner decides the families.

## 1. Method and collections

- **Tip collection** (`inventory.py collect`, nift at 0c66cad6): 155 reasons seen (classify-all
  non-consequential reasons and execute fallbacks).
- **Pre-checkpoint-1 collection**: a scratch mutant of the same tree with C107 reverted, removing the two
  ElementPath routing lines in `compiler_assign.cpp` and the `compiler_coro.cpp` Assign tightening. It
  saw 157 reasons. Its report equals the old `.build/cp25r-inventory` (f063f1a9 era, the cf833f5a
  analysis) apart from corpus changes. The mutant has been deleted.
- **28a6368b collection**: the binary is identical, and C109 and C110 changed no inventory corpus (the
  C110 `v44_root_path` edit is printf-only and is not extracted). It therefore equals the tip.
- **Differences tip vs pre-CP1**, as full/consequential/fallback counts:

  | Row | Pre-CP1 | Tip |
  | --- | --- | --- |
  | `compound-target` | 3/9/0 | 0/6/0 |
  | `index-assign-target` | 3/7/2 | 0/3/0 |
  | `index-target` | 5/0/4 | 2/1/2 |

  Nothing else changed because of C107. Other differences against the old collection are corpus changes,
  for example `fn-flags` 28 → 8 (C94) and `statement:Export` fallbacks 16 → 33.
- **Per-row evidence check**: every PENDING-OWNER and implement row's evidence program was run on the
  tip, the pre-CP1 mutant and legacy (`NIFT_VM=0`).
- **Gates**: the 19 gates named in evidence columns were run at the tip, and their refusal reasons were
  read from the logs.
- **Rows the evidence no longer reproduces**: for each, I wrote a probe to emit the reason.
- **F13 chains**: each chain was read from classify-all, then a root-free probe was run for every
  masked reason.
- **New reasons**: reasons emitted by passing VM tests and test-rt-vm fallback logs were compared with
  the tsv.

## 2. Summary

| Class | Rows | Meaning |
| --- | --- | --- |
| E1 | 2 | eliminated for its evidence by checkpoint 1 (C107) |
| E2 | 19 | test or inventory artefact (17 F12, 2 stale) |
| A | 4 | covered by an existing owner-approved difference, but not yet that behaviour |
| D | 42 | derivative (chain masking), with a named root |
| G | 113 | genuine remaining unsupported capability |
| **classified** | **180** | = 157 PENDING-OWNER + 23 implement before the rescan |
| N | 45 | newly found, unapproved, not in the inventory: added as PENDING-OWNER |

Status counts in `classification.tsv`:

| Status | Before (0c66cad6) | After (C111) | Change |
| --- | --- | --- | --- |
| approved | 44 | 44 | none |
| deferred-named | 17 | 17 | none |
| implement | 23 | 23 | none |
| PENDING-OWNER | 157 | 200 | -2 retired, +45 N rows |
| retired | 7 | 9 | +2 E2 retirements (`method:increment`, `presentation-struct-method:*`) |
| rows | 248 | 293 | |

Tsv changes:

- Every classified row's disposition now starts with `RESCAN post-CP1 (C111, class X[, family])`.
- **Evidence repairs** (status kept): `declare-const`, `param-name-reserved`, `interpolation:unknown-type`,
  `context:number-to-int`, `context:object-length`, `module-host-op:exists`, `module-host-op:cat`,
  `coro-module-before-import`, `coro-async-value`, `method:sort_by`, `type:member` and `nesting-limit`.
- **`nesting-limit` raised-at** is corrected from `expr_parser.cpp` to `compiler.cpp:259`
  (`check_unit_depth`).
- **The escaped-ref smoke's three failures** are recorded on the `context:struct-new` and
  `declare-pattern` rows; see section 9.
- **No other column changed.** No row became approved or deferred-named, and no rt-diff/pending rule
  was touched.

### Per-row classes

### E1, eliminated by checkpoint 1 (2 rows)

- **assign** (2): `compound-target` (implement), `index-assign-target` (implement)

### E2, test or inventory artefact (19 rows)

- **f12** (17): `builtin-arity:join`, `builtin-receiver:join`, `callable:thread`, `coro-blocking-host:exists`, `coro-blocking-host:file`, `coro-blocking-host:rm`, `coro-observe:done`, `coro-observe:print`, `coro-observe:status`, `coro-struct`, `method:close`, `method:done`, `method:join`, `method:open`, `method:save`, `method:status`, `method:write`
- **stale** (2): `method:increment` (now retired), `presentation-struct-method:*` (now retired)

### A, covered by an approved difference (4 rows)

- **approved** (4): `alias-argument`, `context:assignment-error`, `fn-flags`, `fn-name-reserved`

### D, derivative (42 rows)

- root `fn-param/fn-flags (variadics)`: `method:array_value`, `method:command`, `method:count`, `method:fixed`, `method:to_global`, `method:stringify`, `method:call`, `method:hidden`
- root `struct-field-init`: `method:call_helper`, `method:call_step`, `method:public_helper`, `method:read`, `method:helper`, `type:push`
- root `opaque:legacy-runtime-error`: `method:catch`, `method:error`, `method:throw`
- root `callable:inject`: `method:filter`, `method:group_by`, `method:map`
- root `struct-after-statement`: `method:m`
- root `path-in-call-argument / struct-field-init`: `method:push`
- root `opaque:irregular-legacy-parse / struct-field-init`: `method:q`
- root `struct-private`: `method:secret`
- root `static-error:struct-arity`: `method:sum`
- root `callable:run / builtin-receiver:to_string`: `method:to_string`
- root `callable:run`: `method:trim`
- root `prepared-decl-type / callable:inject`: `type:index`
- root `coro-future-flow:Member`: `method:await`
- root `opaque:template-text`: `statement:Import`, `statement:TemplateStmt`
- root `statement:Export`: `coro-module-private-async`
- root `assign-free-name`: `assign-free-name:undeclared`
- root `static-error:arity`: `callable:undefined_thing`
- root `prepared-decl-type / free-name`: `free-name:undeclared`
- root `context:struct-new`: `struct-construction`
- root `struct-private / context:struct-new`: `struct-value`
- root `builtin-bytes:encode`: `builtin-bytes:decode`, `method:decode`, `method:encode`
- root `binding-name-reserved / fn-name-reserved (consequential only)`: `unresolved-name:legacy-binding`, `text-type-name`

### G, genuine remaining (113 rows)

- **aliasing** (1): `path-in-call-argument` (implement)
- **builtins** (19): `builtin-arity:copy`, `builtin-arity:move`, `builtin-bytes:bytes` (implement), `builtin-bytes:encode` (implement), `builtin-bytes:open_bytes` (implement), `builtin-bytes:secure_random_bytes` (implement), `builtin-lazy-args:move`, `builtin-lazy-args:setenv`, `builtin-lazy-args:substr`, `builtin-lazy-args:validate`, `callable:err` (implement), `callable:minify`, `callable:module_path`, `callable:read` (implement), `callable:secure_random_bytes` (implement), `callable:sorted_map`, `callable:sorted_set`, `callable:warn` (implement), `statement:Command`
- **context** (8): `context:assignment-type`, `context:number-to-int`, `context:object-length`, `jump-not-last`, `prepared-decl-type`, `prepared-fn-unprepared`, `prepared-unpreparable` (implement), `string-arg-type`
- **coro** (2): `coro-future-flow:Member`, `for-capture-vanishing`
- **cp24** (9): `callable:build`, `callable:build_all`, `callable:build_names`, `callable:inject`, `callable:status`, `callable:track`, `callable:tracked`, `callable:untrack`, `opaque:template-text`
- **dynscope** (2): `free-name`, `free-name:dynamic-caller`
- **exports** (8): `export-struct`, `import-collision` (implement), `import-dynamic`, `import-name-clash`, `module-bare-return`, `module-host-op:cat`, `module-host-op:exists`, `statement:Export`
- **infer** (2): `interpolation:unknown-type`, `method:sort_by`
- **portability** (1): `method:sort`
- **static** (32): `builtin-arity:contains`, `builtin-arity:last`, `builtin-arity:set`, `builtin-arity:sha256`, `builtin-arity:sha256_file`, `builtin-arity:split`, `builtin-receiver:substr`, `builtin-receiver:to_string`, `coro-async-value`, `coro-module-before-import`, `declare-const`, `error-result-expression`, `host-handle-flow`, `jump-outside-loop`, `non-transferable`, `number-literal` (implement), `opaque:legacy-runtime-error`, `opaque:struct-body`, `static-error:arity`, `static-error:member-assign`, `static-error:redeclared`, `static-error:struct-arity`, `struct-private`, `type:+`, `type:<`, `type:>`, `type:compound-assignment`, `type:increment`, `type:length`, `type:member`, `type:script-return`, `type:unary`
- **structs** (3): `context:struct-new` (implement), `struct-after-statement`, `struct-field-init` (implement)
- **syntax** (8): `binding-name-reserved` (implement), `expression-statement`, `index-target` (implement), `nesting-limit`, `opaque:irregular-legacy-parse`, `param-name-reserved`, `postfix-spelling` (implement), `shebang-statement`
- **undef** (14): `assign-free-name`, `binding-shadows-fn`, `callable:assert`, `callable:exit`, `callable:len`, `callable:nope`, `callable:nosuch`, `callable:to_string`, `callable:undefined_fn`, `callable:unknown`, `fn-called-before-declaration`, `method:nope`, `method:nosuch`, `unresolved-name`
- **variadic** (4): `declare-pattern` (implement), `fn-param`, `lambda-variadic`, `spread`

### N, newly found (45 rows, added as PENDING-OWNER)

- **builtins** (4): `builtin-lazy-args:range`, `builtin-lazy-args:replace`, `builtin-presentation-chain`, `host-literal-arg`
- **context** (5): `context:modulo-assignment`, `context:negative-zero`, `context:parameter-type`, `context:string-append-type`, `for-missing-write`
- **exports** (6): `module-host-op:cp`, `module-host-op:file`, `module-host-op:getenv`, `module-host-op:is_dir`, `module-host-op:is_file`, `module-host-op:remove`
- **infer** (2): `builtin-arg-fault-context`, `postfix-receiver:member`
- **static** (18): `await-outside-coroutine-unit`, `builtin-arity:abs`, `builtin-arity:keys`, `builtin-arity:stringify`, `builtin-arity:trim`, `builtin-receiver:empty`, `builtin-receiver:first`, `builtin-receiver:index_of`, `builtin-receiver:keys`, `builtin-receiver:stringify`, `builtin-receiver:sum`, `builtin-receiver:to_double`, `builtin-receiver:to_upper`, `method:replace_once`, `static-error:push-arity`, `type:%`, `type:*`, `type:-`
- **syntax** (7): `comparison-chain`, `incdec-target`, `object-key`, `opaque:nesting-limit`, `param-shadows-fn`, `prefix-not-precedence`, `string-literal`
- **undef** (3): `callable:prit`, `callable:stringify`, `import-scope`

The F12 rows are E2 but **stay PENDING-OWNER**. classify-all still emits them, so retiring them would
trip `retired reason seen again` (exit 3). They become retirable once the section 7 tool fix lands. The
implement rows keep implement, including the E1 pair, which are narrowed but still emitted.

## 3. E1, E2, A and D details

**E1, checkpoint 1 (C107).**
- `compound-target`: its evidence (call-aggregate-2000, call-locals-*, minify `e.f`) now runs. The only
  residual is a member target on a plain object, `o.k += 1`, which is legacy's member-assignment
  error.
- `index-assign-target`: official-json-transform runs, and official-bfs now refuses with
  `prepared-decl-type`. The residual is a fourth level and `o.k[0] = v`.
- Both are seen only as consequential reasons on the tip corpus. They stay implement.
- `type:element-assignment` was already retired.

**E2, F12 (17 rows).**
- Cause: classify-all compiles with default `CompileLimits` (`src/rt/refusal_inventory.cpp:163`) and
  never calls `cooperative_switches()`, so `parallel_tasks` is false.
- Script runs do call it (`src/rt/script_runner.cpp:310-311`; the CP23 default is
  `parallel_tasks=true`).
- In the real-runner execute pass, every F12 reason has **0 fallbacks**. Every evidence program either
  runs (vm or vm-error) or refuses with another reason (`builtin-arity:copy`, `callable:err`,
  `fn-param`, `callable:project_root`).
- Classified from the real runner, none of the 17 is a refusal.

**E2, stale (2 rows, retired).**
- `method:increment`: its evidence `copy.f` runs on the VM (`1/2/2`, legacy-equal), and no corpus
  program or probe emits it.
- `presentation-struct-method:*`: B3/B4 struct presentation and dispatch compile. A session with an
  unknown receiver gives `builtin-receiver:stringify` (an N row) instead.
- Neither is seen at the tip, so the report stays clean.

**A (4 rows).** Each is covered by a ruling, but the refusal is not the ruled behaviour, which is why it
was not approved:

| Row | Ruling | Why it is still PENDING-OWNER |
| --- | --- | --- |
| `context:assignment-error` | D-009 #1 (owner 2026-10-17): one top-level wording in loops | the refusal defers implementing it |
| `fn-name-reserved` | D-009 #3: user functions take precedence over built-ins | the refusal defers that approved change. Gates: filesystem-recipe ×14. Representative: `fn(stat(p)) {return 1}` / `print(stat("x"))`; legacy "print: value is not directly renderable", exit 1 |
| `alias-argument` | B5 shared references (D-NEXT-refsem-b, Q3 list) | aliasing is the approved semantics; the refusal predates B5 |
| `fn-flags` | D-047 #1 / D-048 #1: reject plain `@fn` in both engines | legacy still accepts it (prints 3) and the legacy-side rejection was declined, so the VM-only refusal is outside the rule's letter. PROPOSAL: the owner states whether a VM-only rejection is covered |

**D (42 rows).** For each masked reason, a root-free form runs on the VM: `map`, `filter`, `sort_by`
(simple), `group_by`, struct methods, a struct declared before statements, `push`,
`catch`/`error`/`throw`, `to_string`, `trim`, `sum`, `index`, `q`, `await` on a future, and the
encode/decode chain once its root compiles. Each resolves with its root and needs no independent work.

Three former F13 rows were **not** derivative:

| Row | Class | Reason |
| --- | --- | --- |
| `method:sort_by` | G | sort-factory `nested-returned-factory`, first reason |
| `type:member` | G | sort-factory `module-definition-error`, first reason |
| `method:increment` | E2 | see above |

`coro-module-private-async` is D under `statement:Export`.

## 4. G and N families

Common to all families:

- VM refusals are reported on stderr as `error: <file>:<l>:<c>: not compiled for the VM (<reason>):
  <text> is not supported by the Runtime 2.0 VM yet`, with exit 1 and nothing printed.
- Corpus counts are tip full/consequential/fallback per reason.
- "Gates" are the gate logs at the tip, out of the 19 run.
- **Ordinary-scripting impact**: the repository has no `examples/` directory. The test corpus has 2539
  `.f` files, and construct frequencies in it are given where measured.
- No G or N row has an approval. Exceptions are noted.

### 4.1 Static errors raised at the wrong point (G `static` 32, N `static` 18)

- **Root cause**: the compiler can prove statically that a construct is an error in legacy, but refuses
  instead of raising legacy's error at the point legacy would.
- **Raise sites**:
  - `builtin-arity:*`: `compiler_keyed.cpp:105,196`;
  - `builtin-receiver:*`: `compiler_dispatch.cpp:41`, `compiler_call.cpp:215`;
  - `type:<op>`: `compiler_expr.cpp:165,170`;
  - `static-error:arity`: `compiler_call.cpp:52`;
  - `number-literal`: `compiler_literal_expr.cpp:58`;
  - `host-handle-flow`: `compiler_handles.cpp:190`;
  - `non-transferable`: `compiler_handles.cpp:195`;
  - `coro-async-value`: `compiler_coro.cpp:158`;
  - `coro-module-before-import`: `compiler_coro_module.cpp:32`;
  - `static-error:push-arity`: `compiler_stmt_expr.cpp:27`;
  - `await-outside-coroutine-unit`: `compiler_coro.cpp:80`.
- **Representative**: `print("a")` / `print(sha256("a", "b"))`.
  - Legacy: stdout `a`, stderr `error: main.f:2:7: sha256: expected one string or bytes value`,
    exit 1.
  - VM: `builtin-arity:sha256`, exit 1, no stdout. The prefix output is lost.
- **Second representative**: `print("a" > 1)`.
  - Legacy: `ordering comparisons require two numbers or two strings`, exit 1.
  - VM: `type:>`.
- **Not every N row here is an error in legacy.** `o.keys(1)`: legacy ignores the extra argument and
  prints `1`, exit 0, while the VM refuses with `builtin-arity:keys`. `a.index_of(1)`: legacy
  `index_of: unsupported for this value`, exit 1; VM `builtin-receiver:index_of`.
- **Gates**:

  | Gate | Reasons and cases |
  | --- | --- |
  | v48-prepared-collection | `builtin-arity:contains`, `builtin-arity:set` (part of 100 failing cases with `postfix-spelling`) |
  | filesystem-recipe | `builtin-arity:split` 2, `builtin-arity:last` 2 |
  | sha256 | `builtin-arity:sha256`, `builtin-arity:sha256_file` |
  | native-dispatch | `static-error:arity`, `callable:len` 2, `callable:to_string`, `callable:unknown` |
  | cp15 | `number-literal` |
  | host-deps | `host-handle-flow` 3 |
  | sort-factory | `type:member` |
  | runtime2-diagnostics | `type:>`: passes, because its probes expect the refusal |

- **Corpus**: 47/38/42 over the G rows. The largest are `opaque:legacy-runtime-error` 11/0/11,
  `static-error:arity` 5/1/4, `static-error:redeclared` 5/0/5 and `struct-private` 5/0/5.
- **Impact**: low on correct scripts, since they reach only already-failing programs. But a refusal is
  pre-execution, so the prefix output legacy prints before the error is lost.
- **PROPOSAL**: one generic "raise legacy's error at the point" mechanism, the pattern C25 used for
  `type:print`. Treat `builtin-arity:keys` and the other legacy-accepting arity rows as missing arity
  support.

### 4.2 Undefined names and calls (G `undef` 14, N `undef` 3)

- **Root cause**: undefined callables, methods and names are refused rather than raised at the call.
- **Raise sites**:
  - `callable:<name>`: `compiler_thread.cpp:22` (the fallback for every unbound callee);
  - `method:<name>`: `compiler_call.cpp:204`;
  - `assign-free-name`: `compiler_assign.cpp:204`;
  - `fn-called-before-declaration`: `resolver.cpp:128`;
  - `binding-shadows-fn`: `compiler_assign.cpp:17`;
  - `import-scope`: `module_link.cpp:356`.
- **Representative**: `print("a")` / `nope(1)`.
  - Legacy: stdout `a`, stderr `error: main.f:2:1: undefined callable: nope`, exit 1.
  - VM: `callable:nope`, exit 1.
- **N rows**:
  - `print(stringify(1))`: legacy `undefined callable: stringify`, exit 1. VM `callable:stringify`.
  - A private module helper used by the importer: legacy `undefined callable: helper`, exit 1. VM
    `import-scope`.
  - `callable:prit`: a typo mutation in test-rt-vm.
- **Gates**:
  - native-dispatch: `callable:len` 2, `callable:to_string`, `callable:unknown`;
  - v48-identifier: `binding-shadows-fn`;
  - runtime2-diagnostics: `callable:assert`, `callable:nosuch`, `method:nosuch` (passes; it expects
    the refusal).
- **Corpus**: 12/27/12. `unresolved-name` accounts for 3/23/2.
- **Approval**: `assign-free-name`, `callable:nope`, `callable:exit` and `method:nope` cite "owner ruling
  2026-10-29: keep refused pending dynamic-resolution semantics". `free-name` carries the P1-2 note
  that no decision text approves it.
- **PROPOSAL**: decide this together with dynamic scope (section 6). If names stay statically resolved,
  raise legacy's `undefined callable: <n>` at the point.

### 4.3 Context-dependent legacy behaviour (G `context` 8, N `context` 5)

- **Root cause**: legacy behaviour depends on the calling context (loop, prepared code, declared
  binding type), so the VM refuses to compile loop-reachable or prepared forms.
- **Raise sites**:
  - `context:number-to-int`: `compiler_call.cpp:196`;
  - `context:object-length`: `compiler_call.cpp:237`;
  - `prepared-decl-type`: `prepared.cpp:248`;
  - `prepared-fn-unprepared`: `prepared.cpp:330`;
  - `string-arg-type`: `prepared.cpp:370`;
  - `context:modulo-assignment`: `compiler_assign.cpp:295`;
  - `context:string-append-type`: `compiler_assign.cpp:300`;
  - `context:negative-zero`: `compiler_expr.cpp:240,318`;
  - `context:parameter-type`: `compiler_call.cpp:109` and `compiler_structs.cpp:370`;
  - `for-missing-write`: `compiler_loops_step.cpp:92`.
- **Representative**: `fn(n(o)) { return o.length() }` / `i := 0` /
  `while (i < 1) { print(n({"k": 1})); i += 1 }`.
  - Legacy: stdout `1`, exit 0.
  - VM: `context:object-length`, exit 1.
- **N representative**: `x := 7` / `x /= 2` / a while loop with `x %= 2`.
  - Legacy: `error: main.f:2:1: cannot assign double to int binding 'x'`, exit 1.
  - VM: `context:modulo-assignment`.
- **Gates**: none of the 19.
- **Corpus**: 14/10/13. `prepared-decl-type` is 8/6/8.
- **Approval**: D-018, D-014 and D-015 are cited as "keep refused" rationale, not as release approval.
- **Impact**: moderate. `.length()` on objects and `to_int` in loops are ordinary.
- **PROPOSAL**: owner rule D-015 "consistent declarations everywhere", or implement both legacy
  contexts.

### 4.4 Syntax and statement forms (G `syntax` 8, N `syntax` 7)

- **Root cause**: statement and expression forms that legacy accepts but the VM compiler does not admit.
- **Raise sites**:
  - `expression-statement`: `compiler_stmt_expr.cpp:84`;
  - `shebang-statement`: `compiler.cpp:139`;
  - `postfix-spelling`: `compiler_rules.cpp:41`;
  - `index-target`: `compiler_rules.cpp:332`, `compiler_element_path.cpp:67,69`;
  - `param-name-reserved`: `resolver.cpp:77`, `compiler_closure.cpp:81`;
  - `nesting-limit`: `compiler.cpp:259`;
  - `comparison-chain`: `compiler_rules.cpp:309`;
  - `prefix-not-precedence`: `compiler_rules.cpp:298`;
  - `incdec-target`: `compiler_assign.cpp:312,328`;
  - `opaque:nesting-limit`: `expr_parser.cpp:255,307,341`, `expr_postfix.cpp:98`,
    `statement_parser.cpp:50`;
  - `string-literal`: `compiler_literal_expr.cpp:62`;
  - `object-key`: `compiler_expr.cpp:344`;
  - `param-shadows-fn`: `compiler_unit.cpp:286`.
- **Representatives**:

  | Program | Legacy | VM |
  | --- | --- | --- |
  | `print("a")` / `#!/usr/bin/env nift` / `print("b")` | `a`, `b`, exit 0 | `shebang-statement` |
  | `print(1 != 1 == 1)` | `false`, exit 0 | `comparison-chain` |
  | `print(!1 == 2)` | `false`, exit 0 | `prefix-not-precedence` |
  | `a := [1]` / `a[0]++` / `print(a[0])` | `2`, exit 0 | `incdec-target` |
  | `fn(k()) {print("k"); return 0}` / `fn(v()) {print("v"); return 5}` / `a := [1]` / `a[k()] = v()` / `print(a[0])` | `v`, `k`, `5` | `index-target` (residual after C107) |
  | `1 + 2` / `print("x")` | `x` | `statement:Command` (bare-expression statements surface as Command or `expression-statement`, e.g. `fallback_bare_expression.f`: legacy "cannot render JSON object $[o]; select a member first") |

  - **`a[0]++` is common in ordinary scripts.** It is a new row, and it interacts with checkpoint 1's
    element targets.
- **Gates**:
  - parser-hardening: `nesting-limit` 1;
  - v48-prepared-collection: `postfix-spelling` (with arity, 100 cases).
- **Corpus**: 17/38/14. `expression-statement` is 4/36/3 and `postfix-spelling` 5/0/4.
- **PROPOSAL**:
  - compile `#!` and bare scalars as no-ops;
  - lower the comparison and prefix rules to legacy's precedence;
  - extend C107's element-path routing to `++`/`--`;
  - see section 4.10 for nesting.

### 4.5 Missing built-ins and host surfaces (G `builtins` 19, N `builtins` 4)

- **Root cause**: the native exists, but its compiler admission, lazy-argument order or receiver kind is
  missing. Bytes have no kind bit (CP15 W8).
- **Raise sites**:
  - `builtin-lazy-args:*`: `compiler_native_args.cpp:34`;
  - `host-literal-arg`: `compiler_native_args.cpp:27`;
  - `builtin-arg-fault-context`: `compiler_native_args.cpp:42`;
  - `builtin-presentation-chain`: `compiler_call.cpp:225`;
  - `builtin-bytes:*`: `compiler_native_args.cpp:9`;
  - `callable:*`: `compiler_thread.cpp:22`.
- **Representatives**:

  | Program | Legacy | VM |
  | --- | --- | --- |
  | `s := sorted_set()` / `s.add(2)` / `s.add(1)` / `print(s.size())` | `2`, exit 0 | `callable:sorted_set` |
  | `print(module_path() != "")` | `true` | `callable:module_path` |
  | `fn(f()) { print("f")` / `return 3 }` / `r := range(0, f())` / `print(r.size())` | `f`, `3` | `builtin-lazy-args:range` |
  | `"aa".replace("a", f())` | `f`, `bb` | `builtin-lazy-args:replace` |

- **Gates**:

  | Gate | Reasons and cases |
  | --- | --- |
  | filesystem-recipe | `builtin-lazy-args:move` 26, `builtin-arity:move` 4 |
  | filesystem-recipe-guard | `builtin-arity:copy`, 26 in total |
  | map-set-index | `sorted_map` 2, `sorted_set` 1 |
  | cp19-s1 | `builtin-lazy-args:setenv` |
  | cp03 | `callable:module_path` |
  | host-deps | `builtin-bytes:open_bytes` 6, `statement:Command` |
  | v46 secure-random | `builtin-bytes:secure_random_bytes` |
  | sha256 | `builtin-bytes:*` |
  | sort-number-compact | `builtin-receiver:substr` |

- **Corpus**: 17/1/15. `callable:minify` is 4/0/4 and `statement:Command` 4/0/3. `sorted_*` appears in
  1 test `.f` file.
- **PROPOSAL**: implement. `callable:err`, `read`, `warn` and the bytes rows are already implement.
  Lazy argument evaluation order is a general rule, not a per-built-in one.

### 4.6 CP24 project APIs (G `cp24` 9)

- **Root cause**: the project and build APIs are not bound on the VM.
- **Raise site**: `compiler_thread.cpp:22`.
- **Representative**: `print(tracked())`.
  - Legacy: `error: main.f:1:7: tracked: not a Nift project (no .nift/config.json found from <dir>)`,
    exit 1.
  - VM: `callable:tracked`.
- **Gates**: none of the 19.
- **Corpus**: 39/0/31, of which `callable:inject` is 24/0/24 and `opaque:template-text` 6/0/6.
- **PROPOSAL**: a CP24-on-VM checkpoint. `callable:inject` alone masks `map`, `filter`, `group_by` and
  `type:index`.

### 4.7 Structs (G `structs` 3)

- **Raise sites**: `context:struct-new` at `compiler_structs.cpp:278`, `struct-field-init` at
  `structs.cpp:109`, `struct-after-statement` at `structs.cpp:88`.
- **Representatives** (VM refuses each, exit 1):

  | Program | Legacy |
  | --- | --- |
  | `struct(S) { x := 1 }` / `xs := []` / while loop with `xs.push(S())` / `print(xs.length())` | `2` |
  | `struct(S) { x := [1, 2].length() }` / `print(S().x)` | `2` |
  | `print("a")` / `struct(S) { x := 1 }` / `s := S()` / `print(s.x)` | `a`, `1` |

- **Corpus**: 13/2/11.
- **Escaped-ref smoke**: `structarr` and `mapstruct` fail on `context:struct-new`; see section 9.
- **Checkpoint-2 scope flag**: see section 8.

### 4.8 Coroutine flow (G `coro` 2)

- **Raise sites**: `coro-future-flow:Member` at `compiler_coro.cpp:36,68`, `for-capture-vanishing` at
  `compiler_loops_step.cpp:90`.
- **Representative**: `a := [[1]]` / `for(row : a) { f := x => row[0]; a.push([9]); print(f(0)) }`.
  - Legacy: `1`.
  - VM: `for-capture-vanishing`.
- **Second representative**: `async_future.f`. Legacy `unknown value or malformed expression:
  fu.await()`, exit 1.
- **Gates**:
  - sort-factory: `for-capture-vanishing` ×4;
  - v49-selector: `for-capture-vanishing`.

### 4.9 Kind inference (G `infer` 2, N `infer` 2)

- **Raise sites**:
  - `interpolation:unknown-type`: `compiler_interp.cpp:274`;
  - `method:sort_by`: `compiler_call.cpp:204`;
  - `postfix-receiver:member`: `compiler_rules.cpp:53`;
  - `builtin-arg-fault-context`: `compiler_native_args.cpp:42`.
- **Representative**: `fn(f(x)) { return "v=$[x]" }` / `print(f(1))` / `print(f("s"))`.
  - Legacy: `v=1`, `v=s`, exit 0.
  - VM: `interpolation:unknown-type`.
- **Gates**: sort-factory, `method:sort_by` (`nested-returned-factory`: legacy `[1,2,3]` then `13`,
  exit 0).
- **Impact**: high. Interpolating a parameter of mixed kind is ordinary.
- **PROPOSAL**: a runtime-kind render path, as in the row's own disposition.

### 4.10 Nesting: `opaque:nesting-limit` vs `nesting-limit` (method step 3)

There are two distinct emitters:

- **The compiler's unit-depth bound.** `check_unit_depth`, `compiler.cpp:255-262`, sets
  `result.reason = "nesting-limit"`. The parser-hardening gate emits it once.
- **The parser's depth bound.** It marks the node opaque with reason `nesting-limit`; `opaque_reason_name`
  (`ast.cpp:60`) reports it as `opaque:nesting-limit`. It is raised at `expr_parser.cpp:255,307,341`,
  `expr_postfix.cpp:98` and `statement_parser.cpp:50`.
- **Probe**: `print(` + 400 `(` + `1` + 400 `)` + `)`.
  - Legacy: `1`, exit 0.
  - VM: `opaque:nesting-limit`, exit 1.
- The adversarial deeptree family shows it too.

**Verdict: the mismatch was tsv-side.** The existing row named the compiler reason but cited the
parser file. Fix: the raised-at column now reads `compiler.cpp:259`, and an N row was added for
`opaque:nesting-limit`.

Emitter-side PROPOSAL (not done): name the two reasons distinctly, for example `parse-nesting-limit`
and `compile-nesting-limit`, or have the parser defer to the compiler's limit. No `opaque:*` family row
exists.

### 4.11 Portability: `method:sort` (G `portability` 1)

- **Raise site**: `compiler_call.cpp:204`.
- **Representative**: `xs := [3, 1, 2]` / `xs.sort((a, b) => a < b)` / `print(xs.stringify())`.
  - Legacy: `[1,2,3]`.
  - VM: `method:sort`.
- **Gate**: cp14b-sort passes, with 234 unverified comparator cases.
- **Approval**: "owner 2026-10-25 comparator profile" is cited, but the row is not approved.
- **PROPOSAL**: verify on GCC 15.2 on Linode (section 10), then rule.

### 4.12 Aliasing: `path-in-call-argument` (G 1, implement)

- Corpus 2/0/2. It masks `method:push`. It stays implement.

## 5. Variadics, spread and destructuring

- **Raise sites**:
  - `fn-param`: `resolver.cpp:70`;
  - `lambda-variadic`: `compiler_closure.cpp:80`;
  - `spread`: `compiler_call.cpp:87,160`;
  - `declare-pattern`: `compiler_assign.cpp:48`.
- **Representatives** (VM refuses each, exit 1):

  | Program | Legacy |
  | --- | --- |
  | `fn(sum(...xs)) { t := 0; for (x : xs) { t += x }; return t }` / `print(sum(1, 2, 3))` | `6` |
  | `fn(add(a, b)) { return a + b }` / `xs := [1, 2]` / `print(add(...xs))` | `3` |
  | `f := (...xs) => xs.length()` / `print(f(1, 2))` | `2` |
  | `[x, y] := [1, 2]` / `print(x + y)` | `3` |

- **Corpus**: 10/1/6. `fn-param` 4/0/4, `spread` 3/1/0, `lambda-variadic` 2/0/1, `declare-pattern`
  1/0/1.
- `fn-param` masks 8 `method:*` rows, including `method:hidden` and `method:to_global`.
- **Frequency in the test corpus**: `...name` appears in 21 `.f` files and destructuring declarations in
  5.
- **Smoke**: escaped-ref `destruct` fails on `declare-pattern` (section 9).
- **Approval**: none. `declare-pattern` is implement.
- **PROPOSAL**: one variadic and spread checkpoint (rest parameters, call spread, lambda rest), with
  destructuring next to it.

## 6. Exports and modules

- **Raise sites**:
  - `statement:Export` in a directly run file: `compiler.cpp:156`;
  - `export-struct`: `module_graph.cpp:350`;
  - `import-name-clash`: `module_link.cpp:338`;
  - `module-bare-return`: `module_link.cpp:304`;
  - `module-host-op:*`: `module_host_op.h:29`;
  - `import-dynamic`: the import resolver.
- **Representatives**:

  | Program | Legacy | VM |
  | --- | --- | --- |
  | Importing `m.f` with `export(twice)` | `4` | runs, legacy-equal (not a refusal) |
  | `fn(twice(n)) {...}` / `export(twice)` / `print(twice(2))` in the entry file | `4`, exit 0 | `statement:Export`, exit 1 |
  | `struct(P) { v := 3 }` / `export(P)` imported, then `P().v` | `3` | `export-struct` |
  | Module top level `x := exists("a.txt")` | `false` | `module-host-op:exists` |
  | N: `is_file` in a nested module function | `false` | `module-host-op:is_file` |
  | `same-line-invalid.f` | `before`, then `import path must be a string expression`, exit 1 | `import-dynamic` |

- **Corpus**: 56/0/43. `statement:Export` is 45/0/33, the largest single G reason.
- `export(` appears in 197 test `.f` files. A library file that is also run directly is an ordinary
  pattern.
- **Gates**: sort-factory `import-name-clash`.
- **Approval**: `import-dynamic` cites D-037 (owner 2026-10-20, dynamic imports rejected before
  execution), but with "G2 release approval pending". D-NEXT-packages #1 covers resolution errors only.
  It is therefore G, not A.
- **PROPOSAL**:
  - compile `export(...)` in an entry unit as a no-op, since legacy just runs;
  - admit module-init host ops in order (the module_host_order tests already pin that order);
  - ask the owner to state whether D-037 is the G2 approval for `import-dynamic`.

## 7. Dynamic scope

- **Rows**: `free-name` and `free-name:dynamic-caller`; `free-name:undeclared` is D.
- **Representative**: `fn(read_hidden()) { return hidden }` / `fn(mk(hidden)) { return read_hidden() }` /
  `print(mk(7))`.
  - Legacy: `7`, exit 0. A callee reads its caller's local.
  - VM: `free-name`, exit 1.
- **Gates**: sort-factory, `free-name` (`dynamic-capture-dependency`).
- **Corpus**: 1/0/2.
- **Approval**: none. The 2026-10-29 "keep refused" ruling is cited, but P1-2 found no decision text.
- **PROPOSAL**: the owner rules legacy dynamic scope out of the release contract (class X), or keeps it
  as a blocker. This also decides section 4.2.

## 8. Src-side proposals (none applied)

1. **F12, the inventory limits.** In `src/rt/refusal_inventory.cpp`, after `CompileLimits limits;` at
   line 163, call `cooperative_switches(limits);` as `script_runner.cpp:311` does. Declare it in
   `compiler_coro_admit.h:40`.
   - This makes classify-all compile with the run-time `parallel_tasks`.
   - The 17 F12 rows would then stop being emitted and could be retired.
   - It changes nift's sha256, because the file is linked into nift (CLI_OBJECTS).
2. **Nesting names**: see section 4.10.
3. **Checkpoint-2 scope flag (explicit).**
   - `struct-construction` is derivative of `context:struct-new` (its structarr evidence refuses with
     `context:struct-new` first). It belongs in checkpoint 2's struct-construction-in-loops work.
   - `struct-value` (copy) and `method:m` are derivative of `struct-private`/`context:struct-new` and
     `struct-after-statement`, which are not in checkpoint 2.
   - `export-struct` touches structs but is not in checkpoint 2.
   - Struct defaults (`struct-field-init`, implement) are unchanged.
   - No implement row was added or removed. The owner should confirm whether checkpoint 2 also takes
     `struct-after-statement`, which masks `method:m`.
4. The 13:26 implement rows for checkpoints 2–5 stay implement. Their only scope change is the E1
   narrowing of `compound-target` and `index-assign-target` (section 3).

## 9. Release-gate issue, still outstanding

`tests/v44_root_path_corruption_reproducers.sh` **FAILS** at 0c66cad6 (rc=1). It is not passing.

| Check | Result |
| --- | --- |
| `destruct` | FAIL, `declare-pattern` |
| `structarr` | FAIL, `context:struct-new` |
| `mapstruct` | FAIL, `context:struct-new` |
| `escaped-ref` | now passes (C107) |

These three are recorded in the evidence of the `declare-pattern` and `context:struct-new` rows as
RELEASE-GATE items.

## 10. Linode-only items (not run here)

- The execute pass on the release toolchain: `make cp25r-inventory` on Linode, with the full corpus and
  that toolchain's nift.
- `method:sort` comparator portability on GCC 15.2 (cp14b-sort's 234 unverified cases).
- Sanitizer runs (ASan/UBSan) of the gates above.

## 11. Gates at the tip (C111)

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS (historical control and the 31-case classification self-test) |
| `make test-rt-lint` | PASS (boundary: 0 problems in 710 files) |
| `make test-test-integrity` | clean (700 files scanned, 0 findings) |
| `inventory.py report <tip collection> --check` | **exit 3** |
| `python3 -B tests/runtime2/assign_family.py --nift $PWD/nift` | PASS (94 cases: 47 legacy-equal, 47 pinned differences or refusals) |

- **Why the inventory check exits 3**: G1 PASSES (155 reasons seen, all classified: approved 8,
  deferred-named 17, implement 17, PENDING-OWNER 113, retired 0). It exits 3 because 113 seen reasons
  are PENDING-OWNER, which are release blockers awaiting the owner. That is the intended BLOCKED state,
  not a tool failure.
- No retired reason is seen again.
- The 45 N rows are not seen by the inventory corpora; the passing VM tests and probes are where they
  were found.
- The check ran on the tip collection, which this commit's tsv edits do not affect.

**Production code**: `git diff 0c66cad6 -- src/` is empty, and the nift sha256 is still
`8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.

**Scratch**: the pre-CP1 mutant (`.build/mutant-nift`) and the smoke extraction were deleted.
