# Post-checkpoint-1 inventory rescan (C111, repaired in C112-C116)

Base: frozen checkpoint-1 candidate `0c66cad6` (owner 21:30: CHECKPOINT 1 ACCEPTED). C111 (`73886c2c`) was the
rescan; its independent review came back NOT CONFIRMED (no P1; classifications, evidence and gate counts wrong).
C112 repaired it; its confirmation review was again NOT CONFIRMED (a session-only retirement, rows missing for
run-time-built reason names), and C113 repaired that; C114 makes the last repairs and adds the decision shape
(section 1); C115 applies owner rulings 00:08 and 00:29 and the C114 confirmation review. nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4` is unchanged and
`git diff 0c66cad6 -- src/` is empty. Only `tests/runtime2/cp25/classification.tsv` and the docs change. Nothing
is approved or deferred, and no rt-diff or pending rule is touched. **Every proposed disposition is a PROPOSAL;
the owner decides the families.**

## 1. How to read the 535 (decision shape; the 505 of C113 plus later rows)

Row count: C113 had 505 PENDING-OWNER + 23 implement = 528; C114 added `type:for-source` (restored) and
`coro-future-flow:Coalesce` (new): 530; C115 adds `fn-redeclared`, `too-many-functions`, `limit:invocation-arguments`,
`session:opaque` and `difference:import-depth` (535); C116 un-retires `free-name:legacy-binding`, `coro-collection-callback`,
`coro-main-cell`: 515 PENDING-OWNER + 23 implement = 538 rows in all. Every one carries exactly one outcome class at the start of its tsv disposition
(`outcome:i`, `outcome:ii-A`/`ii-B`/`ii-C`, `outcome:iii`, `outcome:D`, `outcome:E`), so this split can be recomputed from the tsv:

- **(i)** the VM refuses a program legacy runs to completion: a real capability gap;
- **(ii)** legacy also errors on the executed path; only the message, the position, the earlier output or the exit
  differ (because the VM refuses before running anything);
- **(iii)** session-only;
- **(iv)** the VM accepts where legacy errors: one row (`difference:import-depth`), plus documented differences;
- **D** derivative; **E** test or inventory artefact (E1/E2).

Method: the class comes from the row's recorded programs (tsv legacy column, C111-C114 probes, the rows.json evidence
runs), counting only a program on which the VM refuses with the row's own reason, and never a session's or an
uncalled body's legacy exit. Rows without such a program were probed in C114 (36 programs). This independent split
agrees with the confirmation review's approximate one; the differences are listed under the table.

| Family | (i) | (ii) | (iii) | (iv) | D | E | rows |
| --- | --- | --- | --- | --- | --- | --- | --- |
| builtin-arity | 5 | 123 | 0 | 0 | 0 | 1 | 129 |
| method (undefined or wrong-kind method) | 2 | 41 | 0 | 0 | 29 | 7 | 79 |
| builtin-receiver | 0 | 77 | 1 | 0 | 0 | 1 | 79 |
| callable (unbound callee) | 17 | 20 | 0 | 0 | 0 | 2 | 39 |
| coroutines and coro-future-flow | 12 | 8 | 0 | 0 | 0 | 9 | 29 |
| module-host-op (host op in a program with imports) | 27 | 0 | 0 | 0 | 0 | 0 | 27 |
| syntax forms | 19 | 5 | 0 | 0 | 0 | 0 | 24 |
| type:<op> and type checks | 18 | 0 | 0 | 0 | 2 | 0 | 20 |
| static checks, const, interpolation | 3 | 14 | 0 | 0 | 0 | 0 | 17 |
| structs | 12 | 3 | 1 | 0 | 1 | 0 | 17 |
| context and prepared code | 11 | 3 | 0 | 0 | 0 | 0 | 14 |
| built-in argument rules | 11 | 1 | 0 | 0 | 0 | 0 | 12 |
| other (singles) | 7 | 1 | 1 | 0 | 0 | 2 | 11 |
| dynamic scope and free names | 4 | 2 | 0 | 0 | 4 | 0 | 10 |
| expression and statement forms | 7 | 1 | 0 | 0 | 2 | 0 | 10 |
| exports and imports | 4 | 3 | 0 | 1 | 0 | 0 | 8 |
| bytes | 4 | 0 | 0 | 0 | 1 | 0 | 5 |
| variadics, spread, destructuring | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| capacity limits | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| **total** | **171** | **302** | **3** | **1** | **39** | **22** | **538** |

Changes in C119 (review of C117/C118, P1-2): 19 kind-based rows move from (ii) to (i), as **mixed** rows: the 16
`type:<op>` rows, `interpolation:non-scalar`, `coro-future-flow:ForBinding` and `coro-future-flow:Throw`. Each refuses
both a single-kind operand (legacy errors: still ii-B) and an inferred union (legacy runs it when the run-time kind fits:
a capability gap); both programs are in each row's evidence (dead-code-admission.md section 1.1). The table above shows
the C119 counts: (i) 171, (ii) 302. The C115-C118 counts in the rest of this file (152 and 321) are historical.

Changes in C115 (confirmation review P2-1, verified): 14 rows moved from (ii) to (i), because legacy runs the refused
construct and the earlier probe's legacy error came from something downstream: `builtin-arity:entries`/`values`,
`builtin-lazy-args:slice`, `builtin-default:get`, `builtin-literal-arg:validate`, `context:modulo-assignment`,
`context:number-to-int` (C114 was wrong: `x := 3.5` then `y := x * 2` / `y.to_int()` in a loop prints 7 in legacy),
`param-shadows-fn`, `struct-name`, `struct-nested`, `struct-redeclared`, `struct-member-clash` (legacy fails only when the
struct is used); and, from the C115 corpus measurement, `prepared-decl-type` (call-locals-2000/4000/8000, official-bfs
complete in legacy) and `opaque:legacy-runtime-error` (b4.f, s.f, names.f complete in legacy). This gives 149 rather
than the review's 147; with the three new reachable rows, (i) is 152. Every remaining (ii) probe was re-run with the
refused construct's result consumed by a plain assignment (`zz := X`, then `print("ok")`): legacy still errors at the
refused construct in every one (288 programs; the other rows' construct is not a printed expression and was checked
by position). `builtin-arity:contains`/`set` stay (ii); `keys`, `copy`, `move` are (i).

**(iv), the VM accepts where legacy errors:** `difference:import-depth` (an import chain 33 to 63 modules deep runs on the VM;
legacy fails at depth 33; C115 probe). Documented differences without rows: async self-recursion (CP23
qualification `qualified:legacy-await-chain-defect`, deep_chain.f); a struct method named like a built-in, under D-009 #3;
the B5 aliasing approvals (D-NEXT-refsem-b, Q3); B3's explicit `.stringify()` on a struct with its own method.

### Governing policy for class (ii): early refusal (owner ruling 00:29)

Rejecting a program before execution whenever the VM can determine that it will fail is the established Runtime 2.0
design (D-013), not a new policy. Earlier error timing, and the absence of legacy's earlier output or side effects, are
approved consequences. Case B stays B; case C covers only internal failures and genuinely different failure categories.
The ruling applies only where the program contains an error that legacy also raises **at the same construct**; valid
constructs the VM refuses stay capability gaps (class (i)); early detection does not excuse a wrong position; internal
failures are not covered. The (ii) rows are tagged `outcome:ii-B, policy:early-refusal(D-013, owner 00:29)` and are
closed under this policy as admission-timing findings, without individual approvals.

**Why they stay PENDING-OWNER.** The policy covers programs that will fail. Every (ii) refusal also fires in code that
never runs, where legacy completes the program (below). Until the owner rules on that dead-code question (section
13.1), a (ii) row is not fully covered, so no status changes; nothing is approved.

| Case | Definition | Rows |
| --- | --- | --- |
| ii-A | both engines reject before any side effect; only the wording differs | 0 |
| ii-B | both reject the same error, the VM earlier (approved timing, 00:29) | 302 (321 before C119) |
| ii-C | internal failure or a genuinely different failure category | 0 (no exit 70, no defect text, exit 1 on both engines in every probe) |

| Contract | Rows | Meets the 00:29 rule? |
| --- | --- | --- |
| B-same-position | 290 (304 before C119) | yes for executed errors (wording and timing differ, as approved); the dead-code question remains |
| B-different-position | 12 (17 before C119) | only where the VM position is accepted: 7 marked "fix: take legacy's position" (implement, D-067 #1); 5 "VM more precise", approved in C119 after the position review (section 19); the other 5 verified positions are on rows that C119 moved to (i) mixed |

**Dead code (C115 measurement).** Each (ii) row's failing statements were wrapped in `if (false) { ... }` (declarations
kept at top level), then `print("n")`: legacy completes every such program. The refusal still fires in 316 of the 321
rows (313 with the generic wrapper, `jump-outside-loop`, `fn-called-before-declaration` and `opaque:struct-body` with a
targeted probe): a static check. It does not fire for `coro-module-before-import` (a flow-sensitive check, effectively
run-time ordering); `import-dynamic` in a dead block is refused as `import-nested` instead; for `import-limit`,
`import-opaque:struct-body` and `import-opaque:legacy-runtime-error` the failing code is module initialisation, which
always runs. **Corpus impact:** of the 94 corpus units (inventory corpora) whose first reason was a C114 (ii) reason, 7
complete in legacy, all refused with `prepared-decl-type` or `opaque:legacy-runtime-error`, which C115 moves to (i); for
the remaining 321 (ii) rows, 0 corpus programs contain such dead code.

**Reading it.** The real gaps (171 rows since C119, class (i); 152 before) fall into the families below; the 302 (ii) rows are governed by
the early-refusal policy, subject to the dead-code and position questions (section 13.1).

### 1.1 Class (i) families ranked by ordinary-scripting impact

| # | Family | (i) rows | Gates (cases) | Representative | Legacy / VM | PROPOSAL |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Host operations in any program with an import | 27: `module-host-op:arch`, `module-host-op:cat`, `module-host-op:cd`, `module-host-op:cp`, `module-host-op:env`, `module-host-op:epoch`, `module-host-op:exists`, `module-host-op:file`, ... | none of the re-run gates | `import("./m.f")` / `print(pwd())` | legacy prints the directory; VM `module-host-op:pwd` | root cause and the reviewer's bounded repair: section 11 (PROPOSAL); raise site script_runner.cpp:325-339 with module_host_op.h:29 (`first_module_host_op`) |
| 2 | Postfix spellings | 1: `postfix-spelling` | test-v410-filesystem-recipe-parity 49, test-v48-prepared-collection-parity 2 | `m.set(k,v+1)` style spellings (filesystem-recipe) | legacy runs; VM `postfix-spelling` | admit the legacy spellings (implement row) |
| 3 | Nested and redeclared function declarations | 2: `fn-redeclared`, `statement:FnDecl` | none of the re-run gates | `fn(f()) { fn(g()) { return 1 } return g() }` / `print(f())` | legacy prints 1; VM `statement:FnDecl` | compile nested `fn` declarations (in functions, loops, ifs) as local function bindings |
| 4 | Safe navigation `?.` / `?[]` | 2: `expression:SafeIndex`, `expression:SafeMember` | none of the re-run gates | `o := {"a": 1}` / `print(o?.a)` | legacy prints 1; VM `expression:SafeMember` | compile SafeMember/SafeIndex as a null-checked member/index |
| 5 | Irregular legacy parses | 2: `import-opaque:irregular-legacy-parse`, `opaque:irregular-legacy-parse` | test-v48-comparison-parity 4 | `if({"a": 1} == {"a": 1}) { print("yes") }` | legacy prints yes; VM `opaque:irregular-legacy-parse` | structure the `if(` and `await x ?? y` spellings in the parser |
| 6 | `export(...)` in an entry file | 1: `statement:Export` | none of the re-run gates | `fn(twice(n)) { return n * 2 }` / `export(twice)` / `print(twice(2))` | legacy prints 4; VM `statement:Export` | compile `export` in an entry unit as a no-op |
| 7 | Structs | 12: `context:struct-new`, `export-struct`, `struct-after-statement`, `struct-assign`, `struct-field-init`, `struct-member-clash`, `struct-name`, `struct-nested`, ... | v44_root_path_corruption_reproducers 2 | `x := P()` / `x = P()` / `x.v = 3` / `print(x.v)` | legacy prints 3; VM `struct-assign` | the reviewer's grouping and CP2/CP2b split: section 12 (PROPOSAL) |
| 8 | Const objects | 1: `declare-const` | none of the re-run gates | `const o := {"k": 1}` / `print(o["k"])` | legacy prints 1; VM `declare-const` | admit const aggregates (binding-only const, B5 Q2) |
| 9 | Interpolation of unproven kinds | 1: `interpolation:unknown-type` | none of the re-run gates | `fn(f(x)) { return "v=$[x]" }` / `print(f(1))` / `print(f("s"))` | legacy prints v=1, v=s; VM `interpolation:unknown-type` | a run-time-kind render path |
| 10 | Variadics, spread, destructuring | 4: `declare-pattern`, `fn-param`, `lambda-variadic`, `spread` | v44_root_path_corruption_reproducers 1, test-v410-filesystem-recipe-parity 1 | `fn(sum(...xs)) {...}` / `print(sum(1, 2, 3))` | legacy prints 6; VM `fn-param` | one variadic/spread checkpoint, destructuring next to it |
| 11 | `a[0]++` and compound assignment as a value | 3: `coro-future-flow:Assign`, `expression:assignment-target`, `incdec-target` | none of the re-run gates | `a := [1]` / `print(a[0] += 5)` | legacy prints 6; VM `expression:assignment-target` | the owner-excluded checkpoint-1 forms, scheduled together (section 13.1 item 4) |
| 12 | Lazy built-in argument order | 7: `builtin-lazy-args:move`, `builtin-lazy-args:range`, `builtin-lazy-args:replace`, `builtin-lazy-args:setenv`, `builtin-lazy-args:slice`, `builtin-lazy-args:substr`, `builtin-lazy-args:validate` | test-v410-filesystem-recipe-parity 13, test-cp19-s1-dispatch 1 | `s := "abc"` / `t := "b"` / `print(s.replace("a", t + "x"))` | legacy prints bxbc; VM `builtin-lazy-args:replace` | one rule: evaluate built-in arguments in legacy's (lazy) order |
| 13 | Futures in ordinary expressions | 8: `coro-future-flow:Assign`, `coro-future-flow:Binary`, `coro-future-flow:Call`, `coro-future-flow:Coalesce`, `coro-future-flow:ExprStmt`, `coro-future-flow:Logical`, `coro-future-flow:Property`, `coro-future-flow:String` | none of the re-run gates | `f := af(1) ?? af(2)` / `print(await f)` | legacy prints 1; VM `coro-future-flow:Coalesce` | admit futures as ordinary values in these parent positions |
| 14 | Loop-context built-ins (`.length()`, `empty()`) | 2: `context:collection-empty`, `context:object-length` | none of the re-run gates | `m := map()` / a while loop with `print(m.empty())` | legacy prints false; VM `context:collection-empty` | implement legacy's loop-context results |

The other 80 class (i) rows (each a smaller family; gates and programs in their tsv rows): `alias-argument`, `binding-name-reserved`, `binding-shadows-fn`, `builtin-arity:copy`, `builtin-arity:entries`, `builtin-arity:keys`, `builtin-arity:move`, `builtin-arity:values`, `builtin-bytes:bytes`, `builtin-bytes:encode`, `builtin-bytes:open_bytes`, `builtin-bytes:secure_random_bytes`, `builtin-default:get`, `builtin-literal-arg:validate`, `builtin-presentation-chain`, `callable:build`, `callable:build_all`, `callable:build_names`, `callable:err`, `callable:minify`, `callable:module_path`, `callable:prique`, `callable:queue`, `callable:read`, `callable:secure_random_bytes`, `callable:sorted_map`, `callable:sorted_set`, `callable:status`, `callable:track`, `callable:tracked`, `callable:untrack`, `callable:warn`, `comparison-chain`, `context:modulo-assignment`, `context:negative-zero`, `context:number-to-int`, `context:parameter-type`, `context:string-append-type`, `coro-async-value`, `fn-flags`, `fn-name-reserved`, `for-capture-vanishing`, `free-name`, `free-name:dynamic-caller`, `host-literal-arg`, `import-collision`, `import-name-clash`, `import-opaque:nesting-limit`, `import-opaque:template-text`, `import-reinitialised`, `index-target`, `jump-not-last`, `limit:atoms`, `limit:invocation-arguments`, `limit:jump distance exceeds 16 bits`, `limit:too many registers`, `method:sort`, `method:sort_by`, `module-bare-return`, `nesting-limit`, `object-key`, `opaque:legacy-runtime-error`, `opaque:nesting-limit`, `opaque:template-text`, `param-name-reserved`, `param-shadows-fn`, `path-in-call-argument`, `path-in-call-argument:imported`, `postfix-receiver:member`, `prefix-not-precedence`, `prepared-decl-type`, `prepared-fn-unprepared`, `prepared-unpreparable`, `shebang-statement`, `statement:Command`, `string-literal`, `too-many-functions`, `type:element-assignment`, `type:for-source`, `unresolved-name`.

**(iii)**: `builtin-receiver:stringify` and `presentation-struct-method:*` (sessions only).

**Rows whose recorded evidence fails in legacy although the capability is a gap** (confirmation review): `declare-const`,
`struct-value`, `struct-field-init` and `module-host-op:*` now carry a program legacy runs (C112/C114 evidence; for
`module-host-op:stat/ls/cd/mkdir/make_dir/env/sleep/remove` the C113 programs failed only on the argument or on
printing the value; C114 replaced them). `builtin-arity:copy`/`move` cite the recorded oracle cases.

### 1.3 Completeness boundary (owner 00:08)

Inventory identity: `0c66cad6` `src/` (unchanged at C115), nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`,
tsv revision = the C116 commit (tests/runtime2/cp25/classification.tsv, 607 rows).
The inventory is complete when it covers:

| Criterion | Coverage at C114 |
| --- | --- |
| every statically named VM refusal reason | C115 statement-level scan of 15 emit idioms in `src/rt` (sites / reason literals): `fail(` 595/271, `refuse(` 42/34, `reject(` 10/9, `add(` 120/7, `kinds_within(` 12/10, `write_refusal(` 7/6, `record_entry`/`record_linked(` 5/2, `reason =` 85/24, `reason_ =` 4/2, `why =` 12/3, `refusal =` 8/7, `stop =` 20/9, `return "..."` 328/21, `return {..., "..."}` 2/2, concatenation `"prefix:" +` 42/44: 255 distinct literals. Each has a row or a section 9 disposition. C115 found 13 the C112 literal scan missed: `fn-redeclared`, `too-many-functions`, `limit:invocation-arguments`, `session:opaque` (rows, reachable), `fn-shape`, `statement:Try`, `session:function-assignment`, `session:capture-contract` (rows, retired at creation; no context emits them, classify-all included), `free-name:legacy-binding` (D), `coro-collection-callback`, `coro-main-cell` (E2/F12; un-retired in C116 because classify-all emits the first two), and the non-reason strings `lambda:`/`named:`/`nift:` (render text), `opaque-limit`/`round-limit`/`unmaskable:` (inventory stop markers) |
| every run-time-generated family | 17 concatenated-name families (section 9.2), enumerated from their emit sites and probed with 1010 generated programs plus the C114 spellings; every reached suffix has an exact row or is absorbed by a stated family row (`method:*`, `callable:*`, `type:*` absorbs nothing producible) |
| every reachable refusal in the approved test corpus | fresh `inventory.py collect` (vm-corpus, semantics, rt-diff-corpus, conformance, cp410 probes, smoke heredocs): 155 reasons seen, G1 PASS (all classified), no retired reason seen |
| the contexts | script, module import, `nift -i` session, function called from a loop, uncalled function and lambda bodies, and classify-all (the inventory's own mode, C116), for every retired row; the first five for every unreachable claim (sections 9.1, 9.3, 9.4, 9.8) |
| async expressions | 25 coro-future-flow spellings (section 9.2; `:Binary`, `:Coalesce` found by them), coroutine-module forms, thread values |

After confirmation the inventory freezes for this campaign; later findings are tracked as separate issues.

### 1.2 Method

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
- **C113 four-way probe rule.** Before a row stays retired or a literal is called unreachable or not reached, it is
  probed in a script, a module import, a `nift -i` session and a function called from a loop (section 9.1, 9.3).
- **C113 dynamic families.** Every reason family whose name is built at run time was enumerated from its emit site
  and probed (1010 generated programs; section 9.2). A fresh C113 collect gives the same 155 seen reasons.

## 2. Summary

| Class | C111 | C112 | C113 | C114 | C115 | C116 | Meaning |
| --- | --- | --- | --- | --- | --- | --- | --- |
| E1 | 2 | 2 | 2 | 2 | 2 | 2 | eliminated for its evidence by checkpoint 1 (C107) |
| E2 | 19 | 18 (+2 from N) | 18 (+1 from N) | 18 (+1 from N) | 18 (+1 from N) | 18 (+3 outside the 180) | 16 F12, `method:join`, `method:increment` (row deleted in C113); `callable:prit`; C116: `coro-collection-callback`, `coro-main-cell` (F12) |
| A | 4 | 3 | 3 | 3 | 3 | 3 | covered by an approved ruling, not yet that behaviour |
| Q | 0 | 1 | 1 | 1 | 1 | 1 | owner question: `fn-flags` |
| D | 42 | 39 | 39 | 38 | 38 | 38 (+1 outside) | derivative (C116: `free-name:legacy-binding`), with a named root |
| G | 113 | 117 (+2) | 117 (+8) | 118 (+9) | 118 (+9) | 118 (+9) | genuine remaining capability |
| **classified** | **180** | **180** | **180** | **180** | **180** | **180** | = 157 PENDING-OWNER + 23 implement at 0c66cad6 |
| N | 45 | 58 | 340 | 341 | 346 | 346 | + `fn-redeclared`, `too-many-functions`, `limit:invocation-arguments`, `session:opaque`, `difference:import-depth` (C115) |

Rows by class at C116: E1 2, E2 20, A 3, Q 1, D 39, G 127, N 346 = 538 (= PENDING-OWNER 515 + implement 23). Row count
history: 528 at C113; +2 in C114 (`type:for-source` restored, `coro-future-flow:Coalesce` new) = 530; +5 in C115 = 535;
+3 in C116 (un-retired). Four C115 rows remain retired at creation (no context emits them, classify-all included).

| Status | 0c66cad6 | C111 | C112 | C113 | C114 | C115 | C116 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| approved | 44 | 44 | 44 | 44 | 44 | 44 | 44 |
| deferred-named | 17 | 17 | 17 | 17 | 17 | 17 | 17 |
| implement | 23 | 23 | 23 | 23 | 23 | 23 | 23 |
| PENDING-OWNER | 157 | 200 | 217 | 505 | 507 | 512 | 515 |
| retired | 7 | 9 | 9 | 5 | 4 | 11 | 8 |
| rows | 248 | 293 | 310 | 594 | 595 | 607 | 607 |

**C117 (owner rulings D-067):** approved 46, deferred-named 36, implement 76, PENDING-OWNER 443, retired 8; 609 rows
(+2 family rows `builtin-arity:*`, `builtin-receiver:*`). Section 18 lists the changes.

Inventory identity: `0c66cad6` `src/`, nift sha256 `8195f1b3...`, tsv revision = the C117 commit (C118 adds dead-code tags only).

### 2.1 Per-row classes (C115)

- **E1 (2)**
  - assign (2): `compound-target`, `index-assign-target`
- **E2 (20)**
  - f12 (18): `builtin-arity:join`, `builtin-receiver:join`, `callable:thread`, `coro-blocking-host:exists`, `coro-blocking-host:file`, `coro-blocking-host:rm`, `coro-collection-callback`, `coro-main-cell`, `coro-observe:done`, `coro-observe:print`, `coro-observe:status`, `coro-struct`, `method:close`, `method:done`, `method:open`, `method:save`, `method:status`, `method:write`
  - f12+D (1): `method:join`
  - test-artefact (1): `callable:prit`
- **A (3)**
  - approved (3): `alias-argument`, `context:assignment-error`, `fn-name-reserved`
- **Q (1)**
  - owner-question (1): `fn-flags`
- **D (39)**
  - derivative (38): `assign-free-name:undeclared`, `builtin-bytes:decode`, `free-name:undeclared`, `method:array_value`, `method:await`, `method:call`, `method:call_helper`, `method:call_step`, `method:catch`, `method:command`, `method:count`, `method:decode`, `method:encode`, `method:error`, `method:filter`, `method:fixed`, `method:group_by`, `method:helper`, `method:hidden`, `method:m`, `method:map`, `method:public_helper`, `method:push`, `method:q`, `method:read`, `method:secret`, `method:stringify`, `method:sum`, `method:throw`, `method:to_global`, `method:to_string`, `method:trim`, `statement:Import`, `statement:TemplateStmt`, `struct-construction`, `type:index`, `type:push`, `unresolved-name:legacy-binding`
  - inventory refinement (1): `free-name:legacy-binding`
- **G (127)**
  - aliasing (1): `path-in-call-argument`
  - builtins (19): `builtin-arity:copy`, `builtin-arity:move`, `builtin-bytes:bytes`, `builtin-bytes:encode`, `builtin-bytes:open_bytes`, `builtin-bytes:secure_random_bytes`, `builtin-lazy-args:move`, `builtin-lazy-args:setenv`, `builtin-lazy-args:substr`, `builtin-lazy-args:validate`, `callable:err`, `callable:minify`, `callable:module_path`, `callable:read`, `callable:secure_random_bytes`, `callable:sorted_map`, `callable:sorted_set`, `callable:warn`, `statement:Command`
  - context (8): `context:assignment-type`, `context:number-to-int`, `context:object-length`, `jump-not-last`, `prepared-decl-type`, `prepared-fn-unprepared`, `prepared-unpreparable`, `string-arg-type`
  - coro (2): `coro-future-flow:Member`, `for-capture-vanishing`
  - cp24 (9): `callable:build`, `callable:build_all`, `callable:build_names`, `callable:inject`, `callable:status`, `callable:track`, `callable:tracked`, `callable:untrack`, `opaque:template-text`
  - dynscope (2): `free-name`, `free-name:dynamic-caller`
  - exports (8): `export-struct`, `import-collision`, `import-dynamic`, `import-name-clash`, `module-bare-return`, `module-host-op:cat`, `module-host-op:exists`, `statement:Export`
  - infer (4): `interpolation:unknown-type`, `method:sort_by`, `type:element-assignment`, `type:for-source`
  - portability (1): `method:sort`
  - static (34): `builtin-arity:contains`, `builtin-arity:last`, `builtin-arity:set`, `builtin-arity:sha256`, `builtin-arity:sha256_file`, `builtin-arity:split`, `builtin-receiver:stringify`, `builtin-receiver:substr`, `builtin-receiver:to_string`, `coro-async-value`, `coro-module-before-import`, `coro-module-private-async`, `declare-const`, `error-result-expression`, `host-handle-flow`, `jump-outside-loop`, `non-transferable`, `number-literal`, `opaque:legacy-runtime-error`, `opaque:struct-body`, `static-error:arity`, `static-error:member-assign`, `static-error:redeclared`, `static-error:struct-arity`, `struct-private`, `type:+`, `type:<`, `type:>`, `type:compound-assignment`, `type:increment`, `type:length`, `type:member`, `type:script-return`, `type:unary`
  - structs (6): `context:struct-new`, `presentation-struct-method:*`, `struct-after-statement`, `struct-field-init`, `struct-no-method`, `struct-value`
  - syntax (12): `binding-name-reserved`, `coro-future-flow:Assign`, `expression-statement`, `expression:assignment-target`, `incdec-target`, `index-target`, `nesting-limit`, `opaque:irregular-legacy-parse`, `param-name-reserved`, `postfix-spelling`, `shebang-statement`, `text-type-name`
  - undef (17): `assign-free-name`, `binding-shadows-fn`, `callable:*`, `callable:assert`, `callable:exit`, `callable:len`, `callable:nope`, `callable:nosuch`, `callable:to_string`, `callable:undefined_fn`, `callable:undefined_thing`, `callable:unknown`, `fn-called-before-declaration`, `method:*`, `method:nope`, `method:nosuch`, `unresolved-name`
  - variadic (4): `declare-pattern`, `fn-param`, `lambda-variadic`, `spread`
- **N (346)**
<details><summary>N rows by family</summary>

  - builtins (5): `builtin-lazy-args:range`, `builtin-lazy-args:replace`, `builtin-literal-arg:validate`, `builtin-presentation-chain`, `host-literal-arg`
  - context (7): `context:collection-empty`, `context:modulo-assignment`, `context:negative-zero`, `context:parameter-type`, `context:string-append-type`, `context:struct-field`, `for-missing-write`
  - dynamic: C115 (5): `difference:import-depth`, `fn-redeclared`, `limit:invocation-arguments`, `session:opaque`, `too-many-functions`
  - dynamic: builtin-arity (116): `builtin-arity:arch`, `builtin-arity:atomic<bool>`, `builtin-arity:atomic<int>`, `builtin-arity:attr_escape`, `builtin-arity:cancel`, `builtin-arity:cat`, `builtin-arity:cd`, `builtin-arity:ceil`, `builtin-arity:channel`, `builtin-arity:chunk`, `builtin-arity:close`, `builtin-arity:cp`, `builtin-arity:deepcopy`, `builtin-arity:drop`, `builtin-arity:empty`, `builtin-arity:ends_with`, `builtin-arity:entries`, `builtin-arity:env`, `builtin-arity:eof`, `builtin-arity:epoch`, `builtin-arity:exists`, `builtin-arity:first`, `builtin-arity:flatten`, `builtin-arity:floor`, `builtin-arity:flush`, `builtin-arity:from_entries`, `builtin-arity:get`, `builtin-arity:getenv`, `builtin-arity:hardware_concurrency`, `builtin-arity:has`, `builtin-arity:highlight`, `builtin-arity:html_escape`, `builtin-arity:indexOf`, `builtin-arity:index_of`, `builtin-arity:is_array`, `builtin-arity:is_bool`, `builtin-arity:is_bytes`, `builtin-arity:is_collection`, `builtin-arity:is_dir`, `builtin-arity:is_enum`, `builtin-arity:is_file`, `builtin-arity:is_float`, `builtin-arity:is_function`, `builtin-arity:is_int`, `builtin-arity:is_null`, `builtin-arity:is_number`, `builtin-arity:is_object`, `builtin-arity:is_string`, `builtin-arity:is_struct`, `builtin-arity:last_index_of`, `builtin-arity:length`, `builtin-arity:lock`, `builtin-arity:locked`, `builtin-arity:ls`, `builtin-arity:make_dir`, `builtin-arity:map`, `builtin-arity:max`, `builtin-arity:merge`, `builtin-arity:merge_deep`, `builtin-arity:min`, `builtin-arity:mkdir`, `builtin-arity:modified`, `builtin-arity:mutex`, `builtin-arity:mv`, `builtin-arity:omit`, `builtin-arity:open`, `builtin-arity:os`, `builtin-arity:path`, `builtin-arity:pick`, `builtin-arity:platform`, `builtin-arity:prettify`, `builtin-arity:pwd`, `builtin-arity:range`, `builtin-arity:read`, `builtin-arity:read_all`, `builtin-arity:read_line`, `builtin-arity:recv`, `builtin-arity:remove`, `builtin-arity:replace`, `builtin-arity:revert`, `builtin-arity:rm`, `builtin-arity:round`, `builtin-arity:same`, `builtin-arity:save`, `builtin-arity:seek`, `builtin-arity:send`, `builtin-arity:setenv`, `builtin-arity:size`, `builtin-arity:sleep`, `builtin-arity:slice`, `builtin-arity:stack`, `builtin-arity:starts_with`, `builtin-arity:stat`, `builtin-arity:substr`, `builtin-arity:sum`, `builtin-arity:take`, `builtin-arity:tell`, `builtin-arity:to_double`, `builtin-arity:to_int`, `builtin-arity:to_lower`, `builtin-arity:to_string`, `builtin-arity:to_upper`, `builtin-arity:touch`, `builtin-arity:trim_end`, `builtin-arity:trim_start`, `builtin-arity:try_lock`, `builtin-arity:type`, `builtin-arity:unique`, `builtin-arity:unlock`, `builtin-arity:unsetenv`, `builtin-arity:url_encode`, `builtin-arity:validate`, `builtin-arity:values`, `builtin-arity:which`, `builtin-arity:write`, `builtin-arity:write_line`
  - dynamic: builtin-default (1): `builtin-default:get`
  - dynamic: builtin-lazy-args (1): `builtin-lazy-args:slice`
  - dynamic: builtin-receiver (68): `builtin-receiver:abs`, `builtin-receiver:ceil`, `builtin-receiver:chunk`, `builtin-receiver:close`, `builtin-receiver:compare_exchange`, `builtin-receiver:contains`, `builtin-receiver:copy`, `builtin-receiver:decode`, `builtin-receiver:drop`, `builtin-receiver:encode`, `builtin-receiver:ends_with`, `builtin-receiver:entries`, `builtin-receiver:eof`, `builtin-receiver:exchange`, `builtin-receiver:exists`, `builtin-receiver:fetch_add`, `builtin-receiver:fetch_sub`, `builtin-receiver:flatten`, `builtin-receiver:floor`, `builtin-receiver:flush`, `builtin-receiver:from_entries`, `builtin-receiver:front`, `builtin-receiver:get`, `builtin-receiver:has`, `builtin-receiver:indexOf`, `builtin-receiver:last`, `builtin-receiver:last_index_of`, `builtin-receiver:load`, `builtin-receiver:lock`, `builtin-receiver:locked`, `builtin-receiver:max`, `builtin-receiver:merge`, `builtin-receiver:merge_deep`, `builtin-receiver:min`, `builtin-receiver:modified`, `builtin-receiver:move`, `builtin-receiver:omit`, `builtin-receiver:open`, `builtin-receiver:path`, `builtin-receiver:pick`, `builtin-receiver:read`, `builtin-receiver:read_all`, `builtin-receiver:read_line`, `builtin-receiver:remove`, `builtin-receiver:replace`, `builtin-receiver:revert`, `builtin-receiver:round`, `builtin-receiver:save`, `builtin-receiver:seek`, `builtin-receiver:set`, `builtin-receiver:slice`, `builtin-receiver:split`, `builtin-receiver:starts_with`, `builtin-receiver:store`, `builtin-receiver:take`, `builtin-receiver:tell`, `builtin-receiver:to_int`, `builtin-receiver:to_lower`, `builtin-receiver:top`, `builtin-receiver:trim`, `builtin-receiver:trim_end`, `builtin-receiver:trim_start`, `builtin-receiver:try_lock`, `builtin-receiver:unique`, `builtin-receiver:unlock`, `builtin-receiver:values`, `builtin-receiver:write`, `builtin-receiver:write_line`
  - dynamic: callable (10): `callable:build_repair`, `callable:ffi_buffer`, `callable:ffi_bytes`, `callable:ffi_sizeof`, `callable:ffi_snapshot_bytes`, `callable:ffi_struct`, `callable:package_path`, `callable:page`, `callable:prique`, `callable:queue`
  - dynamic: coro-future-flow (10): `coro-future-flow:Binary`, `coro-future-flow:Coalesce`, `coro-future-flow:CompoundAssign`, `coro-future-flow:ExprStmt`, `coro-future-flow:ForBinding`, `coro-future-flow:IncDec`, `coro-future-flow:Logical`, `coro-future-flow:Property`, `coro-future-flow:String`, `coro-future-flow:Throw`
  - dynamic: expression (2): `expression:SafeIndex`, `expression:SafeMember`
  - dynamic: four-way (6): `compound-operator`, `import-opaque:legacy-runtime-error`, `limit:atoms`, `limit:jump distance exceeds 16 bits`, `limit:too many registers`, `path-in-call-argument:imported`
  - dynamic: import-opaque (4): `import-opaque:irregular-legacy-parse`, `import-opaque:nesting-limit`, `import-opaque:struct-body`, `import-opaque:template-text`
  - dynamic: message (2): `async(callable, ...) was removed; declare an async function/lambda and call it directly`, `await(...) was removed; use 'await future' or 'await async_fn(args)'`
  - dynamic: method (37): `method:all`, `method:any`, `method:append`, `method:cat`, `method:clear`, `method:count_by`, `method:cwd`, `method:env`, `method:file`, `method:find`, `method:find_index`, `method:group_by_each`, `method:index_by`, `method:insert`, `method:insert_after`, `method:insert_before`, `method:max_by`, `method:min_by`, `method:mv`, `method:partition`, `method:pop`, `method:prepend`, `method:read_all_bytes`, `method:read_bytes`, `method:read_val`, `method:reduce`, `method:reset`, `method:reverse`, `method:running`, `method:splice`, `method:stat`, `method:stderr`, `method:stdin`, `method:stdout`, `method:unique_by`, `method:write_bytes`, `method:write_val`
  - dynamic: module-host-op (19): `module-host-op:arch`, `module-host-op:cd`, `module-host-op:env`, `module-host-op:epoch`, `module-host-op:hardware_concurrency`, `module-host-op:ls`, `module-host-op:make_dir`, `module-host-op:mkdir`, `module-host-op:open`, `module-host-op:os`, `module-host-op:platform`, `module-host-op:pwd`, `module-host-op:setenv`, `module-host-op:sha256_file`, `module-host-op:sleep`, `module-host-op:stat`, `module-host-op:touch`, `module-host-op:unsetenv`, `module-host-op:which`
  - dynamic: statement (1): `statement:FnDecl`
  - exports (8): `import-limit`, `import-reinitialised`, `module-host-op:cp`, `module-host-op:file`, `module-host-op:getenv`, `module-host-op:is_dir`, `module-host-op:is_file`, `module-host-op:remove`
  - infer (4): `builtin-arg-fault-context`, `interpolation:non-scalar`, `postfix-receiver:member`, `type:for-source-union`
  - static (25): `assign-catch-binding`, `await-outside-coroutine-unit`, `builtin-arity:abs`, `builtin-arity:keys`, `builtin-arity:stringify`, `builtin-arity:trim`, `builtin-receiver:empty`, `builtin-receiver:first`, `builtin-receiver:index_of`, `builtin-receiver:keys`, `builtin-receiver:sum`, `builtin-receiver:to_double`, `builtin-receiver:to_upper`, `coro-future-flow:Call`, `coro-future-flow:method`, `coro-thread-callee`, `method:replace_once`, `push-receiver`, `static-error:push-arity`, `type:%`, `type:*`, `type:-`, `type:/`, `type:<=`, `type:>=`
  - structs (7): `struct-assign`, `struct-member-clash`, `struct-name`, `struct-nested`, `struct-no-field`, `struct-redeclared`, `struct-shadows-fn`
  - syntax (6): `comparison-chain`, `object-key`, `opaque:nesting-limit`, `param-shadows-fn`, `prefix-not-precedence`, `string-literal`
  - undef (2): `callable:stringify`, `import-scope`

</details>


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
- **`method:increment` (row deleted in C113).** Its evidence copy.f runs, but `x := 5` / `x.increment()` still emits the
  reason in all four contexts (section 9.3). inventory.py matches an exact row before `<prefix>:*` (`row_for`, line 194),
  so the retired row, not `method:*`, would have matched a sighting ("retired reason seen again"), contrary to C112's
  text. Retired means "resolved or not emitted any more" (status header, review L-1); an emitted reason cannot keep that
  status, so the row is deleted and the `method:*` family row (PENDING-OWNER) classifies it. No corpus program emits it
  (fresh C113 collect).
- **`callable:prit`**: a typo mutation inside test-rt-vm (an undefined callable); a test artefact, kept PENDING-OWNER
  like every undefined callable.
- **`builtin-receiver:stringify` (restored to PENDING-OWNER, G sessions, C113).** C112 retired it after probing scripts
  and a struct session only. `nift -i` with `f := (v) => v.stringify()` / `print(f({"a": 1}))` refuses with it; legacy
  prints `{"a":1}`; the same lambda runs in a script, a module and a loop-called function (section 9.3).
- **C111's retirement of `presentation-struct-method:*` was wrong**; it is restored as G (section 5.7).

### 3.3 A (3 rows)

| Row | Ruling | Why still PENDING-OWNER |
| --- | --- | --- |
| `context:assignment-error` | D-009 #1 (owner 2026-10-17): one top-level wording in loops | the refusal defers implementing it. Gates: filesystem-recipe 1 case |
| `fn-name-reserved` | D-009 #3: user functions take precedence | the refusal defers that change. Gates: filesystem-recipe 7, sha256 4, script_import_syntax 1 |
| `alias-argument` | B5 shared references (D-NEXT-refsem-b, Q3 list) | aliasing is the approved semantics; the refusal predates B5 |

### 3.4 Q: owner question (1 row)

`fn-flags`: D-047 #1 / D-048 #1 approve rejecting plain `@fn` before execution in **both** engines. The legacy side
was declined, and `known-limitations.md` itself says the VM-only refusal is not what was approved. Representative:
`@fn(v()) { return 3 }` / `print(v())`: legacy prints `3`, exit 0; VM `fn-flags`, exit 1. **Question:** does D-048's
rejection-before-execution cover a VM-only refusal now that the legacy change is declined?

### 3.5 D (38 rows since C114): roots and the root-free check

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
| `text-type-name` | binding-name-reserved / fn-name-reserved (consequential only, gate logs) | **C114: not derivative**: `x := await` inside a lambda body emits it on its own, so it moved to G (section 9.4); C112 had probed `type(x) == int` only |
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

### 5.1 Static errors raised at the wrong point (G 34, N 25)

- **Rows**: G `builtin-arity:contains`, `builtin-arity:last`, `builtin-arity:set`, `builtin-arity:sha256`, `builtin-arity:sha256_file`, `builtin-arity:split`, `builtin-receiver:stringify`, `builtin-receiver:substr`, `builtin-receiver:to_string`, `coro-async-value`, `coro-module-before-import`, `coro-module-private-async`, `declare-const`, `error-result-expression`, `host-handle-flow`, `jump-outside-loop`, `non-transferable`, `number-literal`, `opaque:legacy-runtime-error`, `opaque:struct-body`, `static-error:arity`, `static-error:member-assign`, `static-error:redeclared`, `static-error:struct-arity`, `struct-private`, `type:+`, `type:<`, `type:>`, `type:compound-assignment`, `type:increment`, `type:length`, `type:member`, `type:script-return`, `type:unary`.
  N `assign-catch-binding`, `await-outside-coroutine-unit`, `builtin-arity:abs`, `builtin-arity:keys`, `builtin-arity:stringify`, `builtin-arity:trim`, `builtin-receiver:empty`, `builtin-receiver:first`, `builtin-receiver:index_of`, `builtin-receiver:keys`, `builtin-receiver:sum`, `builtin-receiver:to_double`, `builtin-receiver:to_upper`, `coro-future-flow:Call`, `coro-future-flow:method`, `coro-thread-callee`, `method:replace_once`, `push-receiver`, `static-error:push-arity`, `type:%`, `type:*`, `type:-`, `type:/`, `type:<=`, `type:>=`.
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

### 5.2 Undefined names and calls (G 18, N 2)

- **Rows**: G `assign-free-name`, `binding-shadows-fn`, `callable:*`, `callable:assert`, `callable:exit`, `callable:len`, `callable:nope`, `callable:nosuch`, `callable:to_string`, `callable:undefined_fn`, `callable:undefined_thing`, `callable:unknown`, `fn-called-before-declaration`, `method:*`, `method:nope`, `method:nosuch`, `unresolved-name`, `method:*`.
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

### 5.3 Context-dependent legacy behaviour (G 8, N 7)

- **Rows**: G `context:assignment-type`, `context:number-to-int`, `context:object-length`, `jump-not-last`, `prepared-decl-type`, `prepared-fn-unprepared`, `prepared-unpreparable`, `string-arg-type`.
  N `context:collection-empty`, `context:modulo-assignment`, `context:negative-zero`, `context:parameter-type`, `context:string-append-type`, `context:struct-field`, `for-missing-write`.
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

### 5.4 Syntax and statement forms (G 12, N 6)

- **Rows**: G `binding-name-reserved`, `coro-future-flow:Assign`, `expression-statement`, `expression:assignment-target`, `incdec-target`, `index-target`, `nesting-limit`, `opaque:irregular-legacy-parse`, `param-name-reserved`, `postfix-spelling`, `shebang-statement`, `text-type-name`.
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
- **Corpus** (tip, full/consequential/fallback): 17/41/14.
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

### 5.7 Structs (G 6, N 7)

- **Rows**: G `context:struct-new`, `presentation-struct-method:*`, `struct-after-statement`, `struct-field-init`, `struct-no-method`, `struct-value`.
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
- **Ordinary-scripting impact**: high: `x = P()` (`struct-assign`), returning either of two structs (`struct-value`), array defaults with push methods (`struct-field-init`) and structs declared after a `print` (`struct-after-statement`) are ordinary. The escaped-ref smoke fails on `context:struct-new` (section 15).
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

### 5.9 Kind inference (G 4, N 4)

- **Rows**: G `interpolation:unknown-type`, `method:sort_by`, `type:element-assignment`, `type:for-source`.
  N `builtin-arg-fault-context`, `interpolation:non-scalar`, `postfix-receiver:member`, `type:for-source-union`.
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
- **PROPOSAL**: verify on GCC 15.2 on Linode (section 16), then rule.

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
| N row added | 21 |
| not a compile refusal | 14 |
| not reached | 11 |
| unreachable | 10 |
| parser-shadowed | 5 |
| consequential-only | 3 |
| inventory refinement of `free-name` | 3 |
| eval-entry only | 2 |
| classify-all/session only | 2 |
| resolver-shadowed | 2 |
| unreachable as a refusal | 2 |
| unreachable in practice | 2 |
| dynamic family | 1 |
| **total** | **108** |

<details><summary>All 108</summary>

| Literal | Disposition |
| --- | --- |
| `analysis-limit:kinds` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `assign-catch-binding` | N row added (reachable; program in its evidence) |
| `assign-target` | consequential-only (tip corpus: consequential sightings only; probes give another reason first) |
| `await-shape` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `binary-operator` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `binding-call-reserved` | resolver-shadowed: binding-name-reserved fires first; legacy errors |
| `builtin-literal-arg:validate` | N row added (reachable; program in its evidence) |
| `builtin-plain-arg:` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `cleanup-incomplete` | not a compile refusal: an isolate-transfer, handle or process error code |
| `compound-operator` | N row added (C113: reached by the four-way probe, section 9.1) |
| `context-end` | not a compile refusal: an isolate-transfer, handle or process error code |
| `context:collection-empty` | N row added (C113: reached by the four-way probe, section 9.1) |
| `context:struct-field` | N row added (reachable; program in its evidence) |
| `coro-async-recursion` | classify-all/session only: needs parallel_tasks off (F12 default limits) or a session unit (refused as session:coroutine first) |
| `coro-capture` | classify-all/session only: needs parallel_tasks off (F12 default limits) or a session unit (refused as session:coroutine first) |
| `coro-future-flow:method` | N row added (reachable; program in its evidence) |
| `coro-future-flow:type` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `coro-observe:epoch` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `coro-task-loop` | consequential-only (tip corpus: consequential sightings only; probes give another reason first) |
| `coro-task-native:` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `coro-task-op:` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `coro-task-recursion` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `coro-thread-callee` | N row added (reachable; program in its evidence) |
| `entry-out-of-range` | not a compile refusal: an isolate-transfer, handle or process error code |
| `eval:error-aggregate-presentation` | eval-entry only (cp19 evaluated expressions, not script or session units) |
| `eval:quoted-callback` | eval-entry only (cp19 evaluated expressions, not script or session units) |
| `execution-error` | not a compile refusal: an isolate-transfer, handle or process error code |
| `expression-nesting` | parser-shadowed: the parser's opaque:nesting-limit fires first (row added in C111) |
| `expression:` | dynamic family (section 9.2): rows added for SafeMember, SafeIndex, assignment-target |
| `expression:assignment-target` | N row added (C113: reached by the four-way probe, section 9.1) |
| `file-capture` | not a compile refusal: an isolate-transfer, handle or process error code |
| `free-name:before-declaration` | inventory refinement of `free-name` (globals plan names the sub-case; the runner reports free-name, which has a row) |
| `free-name:dynamic-shadow` | inventory refinement of `free-name` (globals plan names the sub-case; the runner reports free-name, which has a row) |
| `free-name:not-one-top-level-binding` | inventory refinement of `free-name` (globals plan names the sub-case; the runner reports free-name, which has a row) |
| `import-limit` | N row added (reachable; program in its evidence) |
| `import-link` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `import-outside-project` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `import-parse-error` | not reached: a module parse error is reported as module-error:syntax (row exists), legacy-equal message [C113 four-way probe: section 9.1] |
| `import-reinitialised` | N row added (reachable; program in its evidence) |
| `interpolation:internal` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `interpolation:non-scalar` | N row added (reachable; program in its evidence) |
| `invalid-graph` | not a compile refusal: an isolate-transfer, handle or process error code |
| `invalid-input` | not a compile refusal: an isolate-transfer, handle or process error code |
| `lambda-capture` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `lambda-param` | parser-shadowed: opaque:legacy-runtime-error first; legacy errors (malformed parameter list, duplicate key, invalid signature) |
| `lambda-shape` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `limit:arguments` | unreachable in practice: capacity limit (0x7fff arguments, 0xffff atoms or lambdas) [C113 four-way probe: section 9.1] |
| `limit:atoms` | N row added (C113: reached by the four-way probe, section 9.1) |
| `limit:lambdas` | unreachable in practice: capacity limit (0x7fff arguments, 0xffff atoms or lambdas) [C113 four-way probe: section 9.1] |
| `limit:session-bindings` | session-only (the session compiler's contract checks) |
| `limit:session-protos` | session-only (the session compiler's contract checks) |
| `logical-operator` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `method:splice` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `object-duplicate-key` | parser-shadowed: opaque:legacy-runtime-error first; legacy errors (malformed parameter list, duplicate key, invalid signature) |
| `param-duplicate` | parser-shadowed: opaque:legacy-runtime-error first; legacy errors (malformed parameter list, duplicate key, invalid signature) |
| `param-type-unpredicted` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
| `parse-error` | not a compile refusal: the runner's stop reason for a parse error (legacy-identical diagnostic, CP10) |
| `path-in-call-argument:imported` | N row added (C113: reached by the four-way probe, section 9.1) |
| `path-in-index` | not reached: the probe tried ran on the VM or refused with another reason (see probe list); no script found [C113 four-way probe: section 9.1] |
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
| `throw-expression` | unreachable as a refusal: the parse error comes first and is legacy-identical [C113 four-way probe: section 9.1] |
| `trailing-comma` | unreachable as a refusal: the parse error comes first and is legacy-identical [C113 four-way probe: section 9.1] |
| `unary-operator` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `unknown-effects` | unreachable: defensive or internal arm (no program found; parser/resolver guarantees the shape) [C113 four-way probe: section 9.1] |
| `unsupported-constant` | not a compile refusal: an isolate-transfer, handle or process error code |
| `unsupported-value` | not a compile refusal: an isolate-transfer, handle or process error code |

</details>

Probe notes for the "not reached" rows: `a.empty()` in a loop-called function, an async function returning epoch(),
a non-suspending loop, print and recursion in tasks, `splice` with a sorted argument, a reassigned parameter, an
index inside a call argument, and an import outside a project root all ran or gave another reason. **Observation**
(not a refusal): an async function awaiting its own recursive call runs on the VM (prints 0) where legacy errors
`future: return: await: expected future`; flagged for the owner (section 13).

The N evidence of every C111 N row is now a minimal program with its exact VM reason and legacy output (in the tsv).

### 9.1 C113 four-way probes of every unreachable or not-reached disposition

Rule (confirmation review P2-A): a literal is called unreachable or not reached only after a probe in a script, a
module import (the program as the imported module), a `nift -i` session and a function called from a loop. Session
legacy exits are 0 by design (a session reports and continues). Cells: VM outcome; legacy exit.

| Literal | Probe | script | module | session | loop-called fn | C113 disposition |
| --- | --- | --- | --- | --- | --- | --- |
| `analysis-limit:kinds` | `x := 1 / i := 0 / while (i < 3) { x = [x]; i += 1 } / print(x.stringify())` | `context:assignment-error`; legacy rc=1 | `context:assignment-error`; legacy rc=1 | `session:binding-shape-contract`; legacy rc=0 | `context:assignment-error`; legacy rc=1 | not reached (other reasons first) |
| `await-shape` | `x := await / print(1)` | `unresolved-name`; legacy rc=1 | `unresolved-name`; legacy rc=1 | `unresolved-name`; legacy rc=0 | `prepared-decl-type`; legacy rc=1 | not reached (a bare `await` is a name) |
| `binary-operator` | `print(1 ^ 2)` | `opaque:legacy-runtime-error`; legacy rc=1 | `import-opaque:legacy-runtime-error`; legacy rc=1 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=1 | parser-shadowed (`^` is opaque) |
| `compound-operator` | `x := 6 / x &= 3 / print(x)` | `compound-operator`; legacy rc=1 | `compound-operator`; legacy rc=1 | runs; legacy rc=0 | `compound-operator`; legacy rc=1 | **reached: N row added** (`&=`, `\|=`, `^=`) |
| `interpolation:internal` | `x := "" / print("$[x]") / print("")` | runs; legacy rc=0 | runs; legacy rc=0 | `interpolation:unknown-type`; legacy rc=0 | runs; legacy rc=0 | not reached (an empty string is a constant) |
| `lambda-capture` | `i := 0 / while (i < 1) { j := i / f := () => j / print(f()) / i += 1 }` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | `prepared-decl-type`; legacy rc=0 | not reached (defensive arm) |
| `lambda-shape` | `f := () => / print(1)` | `opaque:legacy-runtime-error`; legacy rc=0 | `import-opaque:legacy-runtime-error`; legacy rc=0 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=0 | parser-shadowed (`opaque:legacy-runtime-error`; legacy runs: row exists) |
| `logical-operator` | `print(null ?? 1) / print(1 and 2)` | `opaque:legacy-runtime-error`; legacy rc=1 | `import-opaque:legacy-runtime-error`; legacy rc=1 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=1 | parser-shadowed |
| `unary-operator` | `print(~1)` | `opaque:legacy-runtime-error`; legacy rc=1 | `import-opaque:legacy-runtime-error`; legacy rc=1 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=1 | parser-shadowed |
| `unknown-effects` | `fn(f(a)) { a.sort((x, y) => x < y) / return a } / print(f([2, 1]).stringify())` | `method:sort`; legacy rc=0 | `method:sort`; legacy rc=0 | `method:sort`; legacy rc=0 | `method:sort`; legacy rc=0 | shadowed by `method:sort` (row exists) |
| `import-link` | `import("./m.f") / print(x)` | `statement:Command`; legacy rc=0 | `statement:Command`; legacy rc=0 | `statement:Import`; legacy rc=0 | `import-nested`; legacy rc=0 | not reached (invariant: every module parses alone) |
| `throw-expression` | `try { throw } catch(e) { print("c") }` | error: throw requires an Error expression; legacy rc=1 | `module-error:syntax`; legacy rc=1 | `session:parse`; legacy rc=0 | error: throw requires an Error expression; legacy rc=1 | unreachable as a refusal: legacy-identical parse error first |
| `trailing-comma` | `a := [1, 2,] / print(a.size())` | error: expression cannot be empty; legacy rc=1 | `module-error:syntax`; legacy rc=1 | `session:opaque`; legacy rc=0 | error: expression cannot be empty; legacy rc=1 | unreachable as a refusal: legacy-identical parse error first |
| `limit:arguments` | `print(1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,...` | `static-error:arity`; legacy rc=1 | `static-error:arity`; legacy rc=1 | `static-error:arity`; legacy rc=0 | `static-error:arity`; legacy rc=1 | shadowed by `static-error:arity` (33000 arguments) |
| `limit:atoms` | `o := {"k0": 0, "k1": 1, "k2": 2, "k3": 3, "k4": 4, "k5": 5, "k6": 6, "k7": 7, "k8": 8, "k9...` | `limit:atoms`; legacy rc=0 | `limit:atoms`; legacy rc=0 | `limit:atoms`; legacy rc=0 | `limit:atoms`; legacy rc=0 | **reached: N row added** (70000 keys; legacy runs) |
| `limit:lambdas` | `f0 := () => 0 / f1 := () => 1 / f2 := () => 2 / f3 := () => 3 / f4 := () => 4 / f5 := () =...` | timeout (120 s); legacy rc=-9 | timeout (120 s); legacy rc=-9 | `binding-name-reserved`; legacy rc=-9 | timeout (120 s); legacy rc=-9 | not reached: 66000 lambdas time out on both engines; session hits `binding-name-reserved` (`f32`) |
| `limit:too many constants` | `x := ["s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12", "s1...` | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | shadowed by `limit:too many registers` (row added) |
| `limit:too many registers` | `fn(g()) { return 1 } / print([g(), g(), g(), g(), g(), g(), g(), g(), g(), g(), g(), g(), ...` | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | `limit:too many registers`; legacy rc=0 | **reached: N row added** (legacy runs) |
| `limit:jump distance exceeds 16 bits` | `x := 0 / if (true) { / x += 1 / x += 1 / x += 1 / x += 1 / x += 1 / x += 1 / x += 1 / x +=...` | runs; legacy rc=0 | runs; legacy rc=0 | `limit:jump distance exceeds 16 bits`; legacy rc=0 | runs; legacy rc=0 | **reached in a session: N row added** (legacy runs) |
| `limit:too many struct types` | `struct(S0) { v := 1 } / struct(S1) { v := 1 } / struct(S2) { v := 1 } / struct(S3) { v := ...` | `type:member`; legacy rc=1 | `type:member`; legacy rc=1 | timeout (120 s); legacy rc=-9 | `struct-nested`; legacy rc=1 | not reached: 65600 structs give `type:member` / `struct-nested`; session times out |
| `coro-future-flow:type` | `fn[async](af(x)) { return x } / x := af(1) / if (true) { x = 1 } / print(await x)` | error: cannot assign int to string binding 'x'; legacy rc=1 | `coro-module-init-task`; legacy rc=1 | `session:coroutine`; legacy rc=0 | error: cannot assign int to string binding 'x'; legacy rc=1 | not reached (legacy-equal type error first) |
| `coro-observe:epoch` | `fn(w()) { return epoch() > 0 } / t := thread(w) / print(t.join())` | runs; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | runs; legacy rc=0 | classify-all only (F12 limits); runs on the runner |
| `coro-task-recursion` | `fn(w(n)) { if (n == 0) { return 0 } / return w(n - 1) } / t := thread(w, 2) / print(t.join...` | runs; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | runs; legacy rc=0 | classify-all only; runs |
| `coro-task-native:` | `fn(w()) { print("x") / return 1 } / t := thread(w) / print(t.join())` | runs; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | runs; legacy rc=0 | classify-all only; runs |
| `coro-task-op:` | `fn(w()) { a := [1] / a.push(2) / return a.size() } / t := thread(w) / print(t.join())` | runs; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | runs; legacy rc=0 | classify-all only; runs |
| `coro-async-recursion` | `fn[async](f(n)) { if (n == 0) { return 0 } / return await f(n - 1) } / print(await f(2))` | runs; legacy rc=1 | `coro-module-init-task`; legacy rc=1 | `session:coroutine`; legacy rc=0 | runs; legacy rc=1 | classify-all only; the VM runs where legacy errors (section 13.2 item 9) |
| `coro-capture` | `k := 2 / f := async (x) => x * k / print(await f(1))` | runs; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | `prepared-decl-type`; legacy rc=0 | classify-all or session only (`session:coroutine` first) |
| `method:splice` | `a := [3, 1] / b := [2] / a.splice(0, 1, b.sort()) / print(a.stringify())` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | not reached; runs |
| `param-type-unpredicted` | `fn(f(x)) { if (x > 0) { return x } / return 0 } / print(f(1)) / print(f(2.5))` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | not reached; runs |
| `path-in-index` | `o := {"k": [0]} / a := [5] / print(a[o.k[0]])` | `opaque:legacy-runtime-error`; legacy rc=1 | `import-opaque:legacy-runtime-error`; legacy rc=1 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=0 | parser-shadowed |
| `path-in-call-argument:imported` | `import("./m.f") / fn(id(x)) { return x } / print(id(t.k[0]))` | `path-in-call-argument:imported`; legacy rc=0 | `path-in-call-argument:imported`; legacy rc=0 | `statement:Import`; legacy rc=0 | `path-in-call-argument:imported`; legacy rc=0 | **reached: N row added** (legacy runs) |
| `builtin-plain-arg:` | `fn(f(x)) { return "a".contains(x) } / print(f("a")) / print(f(1))` | error: contains: argument must be a string; legacy rc=1 | error: import: contains: argument must be a string; legacy rc=1 | runs; legacy rc=0 | error: contains: argument must be a string; legacy rc=1 | not reached: legacy-equal error |
| `import-outside-project` | `import("/dev/null") / print(1)` | `import-error:missing`; legacy rc=1 | `import-error:missing`; legacy rc=1 | `statement:Import`; legacy rc=0 | `import-nested`; legacy rc=1 | not reached: `import-error:missing` (row exists) first |
| `import-parse-error` | `import("./m.f") / print(1)` | `module-error:syntax`; legacy rc=1 | `module-error:syntax`; legacy rc=1 | `statement:Import`; legacy rc=0 | `import-nested`; legacy rc=1 | not reached: `module-error:syntax` (row exists) |
| `opaque:deferred-translation` | `f := (x) => @fn / print(1)` | `opaque:legacy-runtime-error`; legacy rc=0 | `import-opaque:legacy-runtime-error`; legacy rc=0 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=0 | not reached: `opaque:legacy-runtime-error` / `opaque:template-text` first |
| `context:collection-empty` | `m := map() / i := 0 / while (i < 1) { /   print(m.empty()) /   i += 1 / }` | `context:collection-empty`; legacy rc=0 | `context:collection-empty`; legacy rc=0 | `context:collection-empty`; legacy rc=0 | `context:collection-empty`; legacy rc=0 | **reached: N row added** (P2-B) |
| `expression:assignment-target` | `a := [1] / print(a[0] += 5)` | `expression:assignment-target`; legacy rc=0 | `expression:assignment-target`; legacy rc=0 | `expression:assignment-target`; legacy rc=0 | `expression:assignment-target`; legacy rc=0 | **reached: row added** (owner-excluded checkpoint-1 form, P2-B) |
| `coro-future-flow:Call (C112 probed as `:type`)` | `fn[async](af(x)) { return x } / fn(w(x)) { return x } / x := af(1) / if (true) { x = threa...` | `coro-future-flow:Call`; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `session:coroutine`; legacy rc=0 | `coro-future-flow:Call`; legacy rc=0 | **reached: N row added** (P2-B) |

The C112 categories "session-only" (30), "not a compile refusal" (14), "inventory refinement" (3), "eval-entry only" (2)
and the parser- or resolver-shadowed rows (7) are unchanged; the confirmation review re-sampled several of them.

### 9.2 Reason families built at run time

The literal scan cannot see names built by concatenation. Every such emit site in `src/rt`, its producible suffixes and
the C113 probes (1010 generated programs, VM and legacy, script context; module host operations need imports):

| Family | Emit sites | Producible suffixes (source of the enumeration) | Probed | Rows added |
| --- | --- | --- | --- | --- |
| `type:<op>` | compiler_expr.cpp:165,170,374; literals in compiler_*.cpp | op_name of Add..Mod, Lt..Ge (+ - * / % < <= > >=) and 11 literal names | all | 4 (P2-B) |
| `coro-future-flow:<parent kind>` | compiler_coro.cpp:163 (and :Member, :method, :type literals) | node_kind_name of any parent of a future holder that `allowed()` rejects | 20 parent kinds | 9 + 2 (P2-B) |
| `expression:<kind>` | compiler_expr.cpp:138 | node kinds without an arm in `expr`: SafeMember, SafeIndex, Spread, Property, StringPart, statement kinds | 9 forms | 2 |
| `statement:<kind>` | compiler.cpp:156 | statement kinds without an arm: Block, FnDecl, StructDecl, EnumDecl, Import, Export, Command, TemplateStmt, Pattern | 7 forms | 1 |
| `opaque:<reason>`, `import-opaque:<reason>` | script_runner.cpp:278, module_graph.cpp:326 | the 6 OpaqueReason names | 6 + 5 imported | 4 (import-opaque) + 1 four-way |
| `builtin-arity:<name>` | compiler_native_args.cpp:25, compiler_keyed.cpp:105,196 | every built-in with fixed max (builtin_list.inc, 187 specs): max+1 and min-1 arguments | all specs | 116 |
| `builtin-receiver:<name>` | compiler_dispatch.cpp:41,44, compiler_call.cpp:215 | every String/Array/Object/Number/Map/Set/Stack/Bool method on another kind | all specs | 68 |
| `builtin-lazy-args:<name>`, `builtin-default:get` | compiler_native_args.cpp:33-34 | every may-fail built-in with two or more arguments, second argument a call | all specs | 1 + 1 |
| `builtin-bytes:<name>`, `builtin-plain-arg:<name>`, `builtin-literal-arg:validate` | compiler_native_args.cpp:9,16,20 | bytes producers; unknown-kind arguments; validate | via specs | 0 (existing rows; plain-arg not reached, 9.1) |
| `callable:<name>` | compiler_thread.cpp:22 | any unbound callee: unbounded; enumerated over legacy_names.cpp kNames (225 names) | 225 | 10 exact + `callable:*` family (absorbs 101) |
| `method:<name>` | compiler_call.cpp:204, compiler_keyed.cpp:98,115,188,191, compiler_error.cpp:39, compiler_array_mutation.cpp:9,11, compiler_collection_lambda.cpp:8 | any method name not bound for the receiver kind: unbounded; enumerated over kMethods on string, array and object receivers | all kMethods × 3 | 37 exact; `method:*` covers the rest |
| `module-host-op:<name>` | module_host_op.h:29 | every host built-in (HostOpen..HostSecureRandomBytes, 26) in a program with imports | 26 | 19 |
| `limit:<codegen error>` | compiler.cpp:40 (codegen.cpp:16,48,59,92,116) | too many registers / struct types / constants, register-operand-constant, jump distance | 4 (9.1) | 2 (four-way) |
| removed `async(...)` / `await(...)` forms | compiler_thread.cpp:19-21 (the message is the reason) | 2 | 2 | 2 |
| `coro-blocking-host:<name>`, `coro-observe:*`, `coro-task-*:<x>` | compiler_coro_admit.cpp:131-249 | host built-ins, future ops and opcodes in tasks | 9.1 | 0: classify-all only (F12 limits); `coro-blocking-host:sha256_file` appears consequentially in the C113 collect |
| `presentation-struct-method:<name>`, `collection-*:<name>`, `invocation-assign:<name>` | compiler_presentation.cpp:40, compiler_collection*.cpp, compiler_invocation.cpp:81 | presentation methods; collection methods; invocation names | - | 0: family rows exist |
| refinements (`free-name:*`, `assign-free-name:*`, `unresolved-name:legacy-binding`) | refusal_inventory.cpp:65-80 | inventory sub-names of a runner reason | - | 0 (the runner reports the base reason, which has a row) |

Not reachable (probed, C114 natural spellings included): `coro-future-flow` parents Unary, Array, Declare, If, While,
Return, Index (`f := xs[0]`), Call (`id(af(1))`), Member and a property store run on the VM; Ternary and `await x ?? y`
give `opaque:irregular-legacy-parse`, a spread argument gives `spread`. C113 was wrong that the Coalesce parent is
unreachable: `f := af(1) ?? af(2)` (and `thread(w, 1) ?? 0`) gives `coro-future-flow:Coalesce`, legacy prints 1 (row
added in C114); `g := f == f` makes `:Binary` a program legacy runs (true). `expression:` Spread,
Property, Number, Coalesce and assignment/increment expressions run or give other reasons; `statement:` Block,
EnumDecl, StructDecl in a block, Import in a function, Pattern and `defer` give `opaque:legacy-runtime-error`,
a legacy-identical parse error, `struct-nested`, `import-nested` or `declare-pattern` (rows exist). Every
producible `builtin-*`, `module-host-op` and legacy-name `callable`/`method` suffix the probes reached has an exact row.

**`type:*` absorbed** (before C113, no exact row): `type:/`, `type:<=`, `type:>=`, `type:for-source-union`; all
emitted (P2-B probes), all now have rows. It absorbs no other producible reason: every op_name suffix and every
literal `type:` name has an exact row (`type:for-source` retired, not reached; `type:element-assignment` restored).

**`method:*` absorbs**: before C113 the 37 legacy method names below (all emitted, legacy errors) and `method:increment`
(emitted). The 37 now have exact rows; `method:increment`'s retired row is deleted, so `method:*` absorbs it and any
other identifier used as a method.

**`callable:*` absorbs** the 101 legacy special names that legacy itself reports as undefined callables (emitted, legacy-equal error), and any other undeclared name:

`Handover`, `OLDPWD`, `PWD`, `SHLVL`, `_`, `amplify`, `append`, `atomic`, `azure`, `bg`, `bool`, `break`, `buffer`, `callback_i64`, `catch`, `cloudflare`, `const`, `content`, `continue`, `css`, `cstr`, `defer`, `del`, `else`, `enum`, `eof`, `export`, `f32`, `f64`, `fg`, `firebase`, `first`, `flush`, `fn`, `for`, `highlight`, `html`, `i16`, `i32`, `i64`, `i8`, `if`, `immut`, `import`, `info`, `input`, `insert`, `insert_after`, `insert_before`, `jobs`, `js`, `json`, `jsx`, `lambda`, `loop`, `modified`, `name`, `netlify`, `path`, `prepend`, `prettify`, `project`, `ptr`, `read_all`, `read_all_bytes`, `read_bytes`, `read_line`, `read_val`, `render`, `replace`, `replace_once`, `return`, `revert`, `save`, `script`, `section`, `seek`, `self`, `split`, `struct`, `supabase`, `svg`, `tell`, `this`, `throw`, `title`, `try`, `u16`, `u32`, `u64`, `u8`, `unwatch`, `vercel`, `void`, `wait`, `watch`, `while`, `write`, `write_line`, `write_val`, `xml`

<details><summary>The 276 dynamic-family rows added (reason; legacy on the probe)</summary>

- **builtin-arity** (116): `builtin-arity:arch` (errors), `builtin-arity:atomic<bool>` (errors), `builtin-arity:atomic<int>` (errors), `builtin-arity:attr_escape` (errors), `builtin-arity:cancel` (errors), `builtin-arity:cat` (errors), `builtin-arity:cd` (errors), `builtin-arity:ceil` (errors), `builtin-arity:channel` (errors), `builtin-arity:chunk` (errors), `builtin-arity:close` (errors), `builtin-arity:cp` (errors), `builtin-arity:deepcopy` (errors), `builtin-arity:drop` (errors), `builtin-arity:empty` (errors), `builtin-arity:ends_with` (errors), `builtin-arity:entries` (errors), `builtin-arity:env` (errors), `builtin-arity:eof` (errors), `builtin-arity:epoch` (errors), `builtin-arity:exists` (errors), `builtin-arity:first` (errors), `builtin-arity:flatten` (errors), `builtin-arity:floor` (errors), `builtin-arity:flush` (errors), `builtin-arity:from_entries` (errors), `builtin-arity:get` (errors), `builtin-arity:getenv` (errors), `builtin-arity:hardware_concurrency` (errors), `builtin-arity:has` (errors), `builtin-arity:highlight` (errors), `builtin-arity:html_escape` (errors), `builtin-arity:indexOf` (errors), `builtin-arity:index_of` (errors), `builtin-arity:is_array` (errors), `builtin-arity:is_bool` (errors), `builtin-arity:is_bytes` (errors), `builtin-arity:is_collection` (errors), `builtin-arity:is_dir` (errors), `builtin-arity:is_enum` (errors), `builtin-arity:is_file` (errors), `builtin-arity:is_float` (errors), `builtin-arity:is_function` (errors), `builtin-arity:is_int` (errors), `builtin-arity:is_null` (errors), `builtin-arity:is_number` (errors), `builtin-arity:is_object` (errors), `builtin-arity:is_string` (errors), `builtin-arity:is_struct` (errors), `builtin-arity:last_index_of` (errors), `builtin-arity:length` (errors), `builtin-arity:lock` (errors), `builtin-arity:locked` (errors), `builtin-arity:ls` (errors), `builtin-arity:make_dir` (errors), `builtin-arity:map` (errors), `builtin-arity:max` (errors), `builtin-arity:merge` (errors), `builtin-arity:merge_deep` (errors), `builtin-arity:min` (errors), `builtin-arity:mkdir` (errors), `builtin-arity:modified` (errors), `builtin-arity:mutex` (errors), `builtin-arity:mv` (errors), `builtin-arity:omit` (errors), `builtin-arity:open` (errors), `builtin-arity:os` (errors), `builtin-arity:path` (errors), `builtin-arity:pick` (errors), `builtin-arity:platform` (errors), `builtin-arity:prettify` (errors), `builtin-arity:pwd` (errors), `builtin-arity:range` (errors), `builtin-arity:read` (errors), `builtin-arity:read_all` (errors), `builtin-arity:read_line` (errors), `builtin-arity:recv` (errors), `builtin-arity:remove` (errors), `builtin-arity:replace` (errors), `builtin-arity:revert` (errors), `builtin-arity:rm` (errors), `builtin-arity:round` (errors), `builtin-arity:same` (errors), `builtin-arity:save` (errors), `builtin-arity:seek` (errors), `builtin-arity:send` (errors), `builtin-arity:setenv` (errors), `builtin-arity:size` (errors), `builtin-arity:sleep` (errors), `builtin-arity:slice` (errors), `builtin-arity:stack` (errors), `builtin-arity:starts_with` (errors), `builtin-arity:stat` (errors), `builtin-arity:substr` (errors), `builtin-arity:sum` (errors), `builtin-arity:take` (errors), `builtin-arity:tell` (errors), `builtin-arity:to_double` (errors), `builtin-arity:to_int` (errors), `builtin-arity:to_lower` (errors), `builtin-arity:to_string` (errors), `builtin-arity:to_upper` (errors), `builtin-arity:touch` (errors), `builtin-arity:trim_end` (errors), `builtin-arity:trim_start` (errors), `builtin-arity:try_lock` (errors), `builtin-arity:type` (errors), `builtin-arity:unique` (errors), `builtin-arity:unlock` (errors), `builtin-arity:unsetenv` (errors), `builtin-arity:url_encode` (errors), `builtin-arity:validate` (errors), `builtin-arity:values` (errors), `builtin-arity:which` (errors), `builtin-arity:write` (errors), `builtin-arity:write_line` (errors)
- **builtin-default** (1): `builtin-default:get` (errors)
- **builtin-lazy-args** (1): `builtin-lazy-args:slice` (errors)
- **builtin-receiver** (68): `builtin-receiver:abs` (errors), `builtin-receiver:ceil` (errors), `builtin-receiver:chunk` (errors), `builtin-receiver:close` (errors), `builtin-receiver:compare_exchange` (errors), `builtin-receiver:contains` (errors), `builtin-receiver:copy` (errors), `builtin-receiver:decode` (errors), `builtin-receiver:drop` (errors), `builtin-receiver:encode` (errors), `builtin-receiver:ends_with` (errors), `builtin-receiver:entries` (errors), `builtin-receiver:eof` (errors), `builtin-receiver:exchange` (errors), `builtin-receiver:exists` (errors), `builtin-receiver:fetch_add` (errors), `builtin-receiver:fetch_sub` (errors), `builtin-receiver:flatten` (errors), `builtin-receiver:floor` (errors), `builtin-receiver:flush` (errors), `builtin-receiver:from_entries` (errors), `builtin-receiver:front` (errors), `builtin-receiver:get` (errors), `builtin-receiver:has` (errors), `builtin-receiver:indexOf` (errors), `builtin-receiver:last` (errors), `builtin-receiver:last_index_of` (errors), `builtin-receiver:load` (errors), `builtin-receiver:lock` (errors), `builtin-receiver:locked` (errors), `builtin-receiver:max` (errors), `builtin-receiver:merge` (errors), `builtin-receiver:merge_deep` (errors), `builtin-receiver:min` (errors), `builtin-receiver:modified` (errors), `builtin-receiver:move` (errors), `builtin-receiver:omit` (errors), `builtin-receiver:open` (errors), `builtin-receiver:path` (errors), `builtin-receiver:pick` (errors), `builtin-receiver:read` (errors), `builtin-receiver:read_all` (errors), `builtin-receiver:read_line` (errors), `builtin-receiver:remove` (errors), `builtin-receiver:replace` (errors), `builtin-receiver:revert` (errors), `builtin-receiver:round` (errors), `builtin-receiver:save` (errors), `builtin-receiver:seek` (errors), `builtin-receiver:set` (errors), `builtin-receiver:slice` (errors), `builtin-receiver:split` (errors), `builtin-receiver:starts_with` (errors), `builtin-receiver:store` (errors), `builtin-receiver:take` (errors), `builtin-receiver:tell` (errors), `builtin-receiver:to_int` (errors), `builtin-receiver:to_lower` (errors), `builtin-receiver:top` (errors), `builtin-receiver:trim` (errors), `builtin-receiver:trim_end` (errors), `builtin-receiver:trim_start` (errors), `builtin-receiver:try_lock` (errors), `builtin-receiver:unique` (errors), `builtin-receiver:unlock` (errors), `builtin-receiver:values` (errors), `builtin-receiver:write` (errors), `builtin-receiver:write_line` (errors)
- **callable** (10): `callable:build_repair` (errors), `callable:ffi_buffer` (errors), `callable:ffi_bytes` (errors), `callable:ffi_sizeof` (errors), `callable:ffi_snapshot_bytes` (errors), `callable:ffi_struct` (errors), `callable:package_path` (errors), `callable:page` (errors), `callable:prique` (errors), `callable:queue` (errors)
- **coro-future-flow** (10): `coro-future-flow:Binary` (errors), `coro-future-flow:Coalesce` (runs), `coro-future-flow:CompoundAssign` (errors), `coro-future-flow:ExprStmt` (runs), `coro-future-flow:ForBinding` (errors), `coro-future-flow:IncDec` (errors), `coro-future-flow:Logical` (runs), `coro-future-flow:Property` (runs), `coro-future-flow:String` (runs), `coro-future-flow:Throw` (errors)
- **expression** (2): `expression:SafeIndex` (runs), `expression:SafeMember` (runs)
- **free-name** (1): `free-name:legacy-binding` (errors)
- **import-opaque** (5): `import-opaque:irregular-legacy-parse` (runs), `import-opaque:legacy-runtime-error` (errors), `import-opaque:nesting-limit` (runs), `import-opaque:struct-body` (errors), `import-opaque:template-text` (runs)
- **limit** (4): `limit:atoms` (runs), `limit:invocation-arguments` (runs), `limit:jump distance exceeds 16 bits` (runs), `limit:too many registers` (runs)
- **message** (8): `async(callable, ...) was removed; declare an async function/lambda and call it directly` (errors), `await(...) was removed; use 'await future' or 'await async_fn(args)'` (errors), `compound-operator` (errors), `coro-collection-callback` (errors), `coro-main-cell` (errors), `fn-redeclared` (runs), `fn-shape` (errors), `too-many-functions` (runs)
- **method** (37): `method:all` (errors), `method:any` (errors), `method:append` (errors), `method:cat` (errors), `method:clear` (errors), `method:count_by` (errors), `method:cwd` (errors), `method:env` (errors), `method:file` (errors), `method:find` (errors), `method:find_index` (errors), `method:group_by_each` (errors), `method:index_by` (errors), `method:insert` (errors), `method:insert_after` (errors), `method:insert_before` (errors), `method:max_by` (errors), `method:min_by` (errors), `method:mv` (errors), `method:partition` (errors), `method:pop` (errors), `method:prepend` (errors), `method:read_all_bytes` (errors), `method:read_bytes` (errors), `method:read_val` (errors), `method:reduce` (errors), `method:reset` (errors), `method:reverse` (errors), `method:running` (errors), `method:splice` (errors), `method:stat` (errors), `method:stderr` (errors), `method:stdin` (errors), `method:stdout` (errors), `method:unique_by` (errors), `method:write_bytes` (errors), `method:write_val` (errors)
- **module-host-op** (19): `module-host-op:arch` (runs), `module-host-op:cd` (errors), `module-host-op:env` (errors), `module-host-op:epoch` (runs), `module-host-op:hardware_concurrency` (runs), `module-host-op:ls` (errors), `module-host-op:make_dir` (errors), `module-host-op:mkdir` (errors), `module-host-op:open` (runs), `module-host-op:os` (runs), `module-host-op:platform` (runs), `module-host-op:pwd` (runs), `module-host-op:setenv` (runs), `module-host-op:sha256_file` (runs), `module-host-op:sleep` (errors), `module-host-op:stat` (errors), `module-host-op:touch` (runs), `module-host-op:unsetenv` (runs), `module-host-op:which` (runs)
- **path-in-call-argument** (1): `path-in-call-argument:imported` (runs)
- **session** (3): `session:capture-contract` (errors), `session:function-assignment` (errors), `session:opaque` (errors)
- **statement** (2): `statement:FnDecl` (runs), `statement:Try` (errors)

</details>

### 9.3 Retired rows under the four-way rule

| Row | Probe | script | module | session | loop-called fn | C113 status |
| --- | --- | --- | --- | --- | --- | --- |
| `type:print` | `fn(each(xs)) { out := []; for(x : xs) { out.push(x * 2) } return out } / print(each([1, 2,...` | error: print: value is not directly renderable; legacy rc=1 | error: import: print: value is not directly renderable; legacy rc=1 | runs; legacy rc=0 | error: print: value is not directly renderable; legacy rc=1 | stays retired (no emit site; the VM raises legacy's error) |
| `aliasing` | `fn(g()) { a := []; a.push(1); return a } / fn(o()) { return {"a": 1} } / g() / o() / a := ...` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | `prepared-decl-type`; legacy rc=0 | stays retired (no emit site) |
| `reference-guard:*` | `a := [[1]] / b := a[0] / b.push(2) / a[0].push(3) / print(a.stringify()) / print(b.stringi...` | `opaque:legacy-runtime-error`; legacy rc=1 | `import-opaque:legacy-runtime-error`; legacy rc=1 | `session:opaque`; legacy rc=0 | `opaque:legacy-runtime-error`; legacy rc=1 | stays retired (no emit site) |
| `type:for-source` | `fn(f(x)) { for (e : x) { print(e) } / return 0 } / f([1, 2])` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | **restored to PENDING-OWNER, G (C114)**: an uncalled lambda emits it (9.4) |
| `type:for-source (args)` | `for (a : args) { print(a) } / print("n")` | runs; legacy rc=0 | `import-scope:invocation-init`; legacy rc=1 | runs; legacy rc=0 | runs; legacy rc=0 | (same row) |
| `coro-module` | `import("./m.f") / fn[async](af(x)) { return x + k } / print(await af(1))` | `coro-module-capture`; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `statement:Import`; legacy rc=0 | `coro-module-capture`; legacy rc=0 | stays retired (site exists, not reached) |
| `coro-module (thread)` | `import("./m.f") / fn(w(x)) { return x + k } / t := thread(w, 1) / print(t.join())` | `coro-module-capture`; legacy rc=0 | `coro-module-init-task`; legacy rc=0 | `statement:Import`; legacy rc=0 | `coro-module-capture`; legacy rc=0 | (same row) |
| `method:increment` | `x := 5 / x.increment()` | `method:increment`; legacy rc=1 | `method:increment`; legacy rc=1 | `method:increment`; legacy rc=0 | `method:increment`; legacy rc=1 | **row deleted** (emitted; `method:*` covers it) |
| `struct-no-method` | `struct(S) { v := 1 } / s := S() / print(s.nope())` | error: struct has no method: nope; legacy rc=1 | error: import: struct has no method: nope; legacy rc=1 | runs; legacy rc=0 | `struct-no-method`; legacy rc=1 | **restored to PENDING-OWNER, G** |
| `struct-no-method (string name)` | `struct(S) { v := 1 } / s := S() / print(s.trim())` | `struct-no-method`; legacy rc=1 | `struct-no-method`; legacy rc=1 | `struct-no-method`; legacy rc=0 | `struct-no-method`; legacy rc=1 | (same row) |
| `type:element-assignment (variable key)` | `o := {"a": 1} / k := "a" / o[k] = 2 / print(o["a"])` | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | runs; legacy rc=0 | (same row) |
| `type:element-assignment (mixed receiver)` | `fn(f(c)) { if (c) { return [1] } / return {"a": 1} } / x := f(true) / x[0] = 5 / print(x.s...` | `type:element-assignment`; legacy rc=0 | `type:element-assignment`; legacy rc=0 | `type:element-assignment`; legacy rc=0 | `type:element-assignment`; legacy rc=0 | **restored to PENDING-OWNER, G** |
| `type:element-assignment (key kinds)` | `fn(k(c)) { if (c) { return 0 } / return "a" } / a := [1] / a[k(true)] = 5 / print(a[0])` | `index-target`; legacy rc=0 | `index-target`; legacy rc=0 | `index-target`; legacy rc=0 | `index-target`; legacy rc=0 | (same row) |
| `builtin-receiver:stringify` | `f := (v) => v.stringify() / print(f({"a": 1}))` | runs; legacy rc=0 | runs; legacy rc=0 | `builtin-receiver:stringify`; legacy rc=0 | runs; legacy rc=0 | **restored to PENDING-OWNER, G (sessions)** |

A fresh C114 collect sees none of the 4 retired reasons and no `method:increment`.

### 9.4 Fifth context: bodies that are never called (C114)

The confirmation review found `type:for-source` emitted only inside a lambda that is never called. Every retired row and
every unreachable or not-reached claim was re-probed with its program as the body of an uncalled `fn` and an uncalled
lambda (legacy then runs the program, exit 0, since the body never executes).

| Probe | uncalled fn | uncalled lambda |
| --- | --- | --- |
| R type:print | runs | `type:*` |
| R aliasing | runs | runs |
| R reference-guard | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| R coro-module | `coro-module-capture` | `coro-module-capture` |
| R coro-module thread | `coro-module-capture` | `coro-module-capture` |
| R type:for-source | runs | runs |
| U analysis-limit:kinds | `context:assignment-error` | `context:assignment-error` |
| U await-shape | `free-name` | `text-type-name` |
| U binary-operator | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| U compound-operator &= | `compound-operator` | `compound-operator` |
| U compound-operator |= | `compound-operator` | `compound-operator` |
| U compound-operator ^= | `compound-operator` | `compound-operator` |
| U interpolation:internal | runs | runs |
| U lambda-capture | runs | runs |
| U lambda-shape | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| U logical-operator | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| U unary-operator | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| U unknown-effects | `method:sort` | `method:sort` |
| U import-link | `import-nested` | `import-nested` |
| U throw-expression | error: throw requires an Error expression | error: throw requires an Error expression |
| U trailing-comma | error: expression cannot be empty | error: expression cannot be empty |
| NR coro-future-flow:type | runs | runs |
| NR coro-observe:epoch | runs | runs |
| NR coro-task-recursion | runs | runs |
| NR coro-task-native | runs | runs |
| NR coro-task-op | runs | runs |
| NR method:splice | runs | runs |
| NR param-type-unpredicted | runs | runs |
| NR path-in-index | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| NR path-in-call-argument:imported | `path-in-call-argument:imported` | `path-in-call-argument:imported` |
| NR builtin-plain-arg | runs | runs |
| NR import-outside-project | `import-nested` | `import-nested` |
| NR import-parse-error | `import-nested` | `import-nested` |
| NR opaque:deferred-translation | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| NR coro-async-recursion | runs | runs |
| NR coro-capture | runs | runs |
| 92 cff Ternary | `opaque:irregular-legacy-parse` | `opaque:irregular-legacy-parse` |
| 92 expr Spread | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| 92 expr Property | runs | runs |
| 92 expr Coalesce | runs | runs |
| 92 expr Number | runs | runs |
| 92 expr AssignExpr | runs | runs |
| 92 expr IncDecExpr | runs | runs |
| 92 stmt Block | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| 92 stmt Enum | error: enum requires 'Name { members }' | error: enum requires 'Name { members }' |
| 92 stmt StructInIf | `struct-nested` | `struct-nested` |
| 92 stmt ImportInFn | `import-nested` | `import-nested` |
| 92 stmt Pattern | `declare-pattern` | `declare-pattern` |
| 92 stmt Defer | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| 92 opaque deferred-translation | `opaque:legacy-runtime-error` | `opaque:legacy-runtime-error` |
| 92 opaque deferred-translation2 | `opaque:template-text` | `opaque:template-text` |
| 92 cff Unary | runs | runs |
| 92 cff Array | runs | runs |
| 92 cff Declare | runs | runs |
| 92 cff If | runs | runs |
| 92 cff While | runs | runs |
| 92 cff Return | `statement:FnDecl` | `statement:FnDecl` |
| 92 cff Call | runs | runs |
| 92 cff Member | runs | runs |
| 92 cff Index | `type:index` | `type:index` |
| T text-type-name lambda | `free-name` | `text-type-name` |
| T text-type-name called | `text-type-name` | `text-type-name` |

Results: no retired row's reason is emitted (type:print's uncalled lambda gives `type:*`, a different row). The
reachable reasons stay reachable (`compound-operator`, `path-in-call-argument:imported`). One D row is not derivative:
`text-type-name` is emitted by `x := await` inside a lambda body, called or not (now G, outcome (ii): legacy errors when
the body runs). The `92 cff` rows were re-probed with the async declaration at top level (the first pass nested it).

### 9.5 Retirement policy (C114; sixth context added in C116)

A row may be retired, or a reason literal called unreachable or not reached, only after its best program has been run
in all five contexts: a script, a module import (the program as the imported module), a `nift -i` session, a
function called from a loop, the body of a function and of a lambda that are never called, and (C116) classify-all, the
inventory's own mode (`NIFT_VM=classify-all`), on the same programs. The results are
recorded in the row's evidence or in section 9. A reason emitted in any context is not retired; an exact retired row
for an emitted reason is deleted when a family row should classify it (inventory.py matches the exact row first).

### 9.6 Deleted rows (traceability)

`method:increment` was deleted in C113 (it is emitted; `method:*` classifies it). Its last row, kept also as a tsv comment:

- reason `method:increment`; class A; family F13-chain-masking; legacy: copy.f: rc=0 out='1/2/2/' err=''
- disposition: RETIRED by the post-checkpoint-1 rescan (C111; reworded C112, class E2): its own evidence copy.f runs on the VM (1/2/2, legacy-equal). The name is one member of the generic `method:<name>` family (a method not bound for the receiver kind, compiler_call.cpp:204): `x := 5` / `x.increment()` still emits method:increment (legacy 'unknown value or malformed expression: x.increment()'); that family is covered by the `method:*` row (G undef). Was: consequential (chain masking): the receiver's declaration or producer is refused/masked, so its kind is Unknown or the name unresolved; resolves with the root, no independent work (decisions.md CP15B scope correction (1)). Root: copied struct (struct-value)
- approval: pending; evidence: count=1; smoke/v42_struct_private_smoke/013-copy/copy.f; real run NIFT_VM=1 (candidate): fb:struct-value 1; rescan C112 program: `x := 5 / x.increment()` (VM method:increment, exit 1; legacy rc=1 out='' err='error: main.f:2:1: unknown value or malformed expression: x.increment()'); raised at: src/rt/compiler_call.cpp:194 (method not bound for the receiver kind); status: retired

### 9.7 The 32 rows whose reported position differed (C115 per-row assessment)

| Row | legacy position | VM position | C115 class | Assessment |
| --- | --- | --- | --- | --- |
| `builtin-arity:deepcopy` | main.f:1:16 | main.f:1:7 | (ii) | VM more precise: the arity of the call is wrong, so the call (1:7) is the construct; legacy points at the argument list (1:16) |
| `builtin-arity:entries` | main.f:2:1 | main.f:2:7 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `builtin-arity:values` | main.f:2:1 | main.f:2:7 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `builtin-default:get` | main.f:4:7 | main.f:4:16 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `builtin-lazy-args:slice` | main.f:4:1 | main.f:4:18 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `builtin-literal-arg:validate` | main.f:1:7 | main.f:1:16 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `context:modulo-assignment` | main.f:2:1 | main.f:4:17 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `context:number-to-int` | main.f:1:12 | main.f:1:19 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `context:struct-field` | main.f:4:25 | main.f:4:21 | (ii) | fix: take legacy's position (the read of the implicit field, 4:25), not the assignment (4:21) |
| `coro-future-flow:ForBinding` | main.f:3:1 | main.f:3:10 | (ii) | VM more precise: the iterable is the future; legacy reports the statement |
| `coro-future-flow:Throw` | main.f:3:1 | main.f:3:7 | (ii) | VM more precise: the operand is the future; legacy reports the statement |
| `coro-module-before-import` | main.f:1:22 | main.f:2:6 | (ii) | fix: take legacy's position (the failing call inside the task) |
| `coro-module-private-async` | pa.f:2:13 | pa.f:2:26 | (ii) | VM more precise (column only, same statement) |
| `fn-called-before-declaration` | main.f:2:11 | main.f:3:1 | (ii) | fix: take legacy's position (the call, not the declaration) |
| `for-missing-write` | main.f:2:24 | main.f:2:1 | (ii) | fix: take legacy's position (the write `x = 5`, not the `for`) |
| `import-limit` | m32.f:1:1 | m63.f:1:1 | (ii) | fix: take legacy's position (legacy's depth, m32.f); also a class (iv) difference: chains 33-63 deep run on the VM and fail in legacy (row difference:import-depth) |
| `import-opaque:legacy-runtime-error` | m.f:1:7 | m.f:1:1 | (ii) | fix: take legacy's position (opaque nodes report the statement start, not the failing operand) |
| `interpolation:non-scalar` | main.f:3:1 | main.f:3:12 | (ii) | VM more precise: the interpolation part, not the print |
| `method:nope` | main.f:1:16 | main.f:1:23 | (ii) | VM more precise: the method call, not the return |
| `non-transferable` | main.f:3:6 | main.f:3:21 | (ii) | VM more precise: the non-transferable argument, not the thread call |
| `opaque:legacy-runtime-error` | main.f:1:7 | main.f:1:1 | (i): corpus programs complete in legacy | fix: take legacy's position (the failing operand); row moved to (i) |
| `param-shadows-fn` | main.f:3:1 | main.f:2:6 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `prepared-decl-type` | main.f:8:22 | main.f:2:3 | (i): corpus programs complete in legacy | fix: take legacy's position (the failing write, not the declaration); row moved to (i) |
| `static-error:push-arity` | main.f:3:17 | main.f:1:21 | (ii) | VM more precise: the `push` itself, not the loop's call site |
| `string-arg-type` | main.f:2:3 | main.f:6:9 | (ii) | fix: take legacy's position (the parameter binding) |
| `struct-member-clash` | main.f:3:7 | main.f:2:13 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `struct-name` | main.f:2:6 | main.f:1:1 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `struct-nested` | main.f:2:7 | main.f:1:13 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `struct-redeclared` | main.f:3:7 | main.f:2:1 | (i): legacy runs the construct (P2-1) | not a position question: the earlier probe's legacy error was downstream of a construct legacy runs |
| `type:for-source-union` | main.f:2:13 | main.f:3:1 | (ii) | probe replaced by the function-parameter form: same position on both engines (main.f:1:12); now B-same-position |
| `type:member` | main.f:2:24 | error-module.f:1:30 | (ii) | VM more precise: the member read in the lambda body, not the call site |
| `type:script-return` | - | main.f:1:1 | (ii) | VM more precise: legacy prints no position |

Remaining in (ii) with a different position: 17 (7 fix, 10 VM more precise).

### 9.8 Sixth context: classify-all (C116)

Each retired row's probes were run under `NIFT_VM=classify-all` (non-consequential reasons listed; `(c)` = consequential).

| Row | script | module | loop-called fn | uncalled fn | uncalled lambda | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `type:print` | compiled | compiled | compiled | compiled | type:* | stays retired: its own reason is not emitted |
| `aliasing` | compiled | compiled | prepared-decl-type, method:push | compiled | compiled | stays retired: its own reason is not emitted |
| `reference-guard:*` | opaque:legacy-runtime-error | import-opaque:legacy-runtime-error | opaque:legacy-runtime-error | opaque:legacy-runtime-error | opaque:legacy-runtime-error | stays retired: its own reason is not emitted |
| `coro-module` | coro-module-capture | coro-module-init-task, coro-module-private-async | coro-module-capture | coro-module-capture | coro-module-capture | stays retired: its own reason is not emitted |
| `coro-module (thread)` | coro-module-capture, method:join | coro-module-init-task, method:join | coro-module-capture, method:join | coro-module-capture, method:join | coro-module-capture, method:join | stays retired: its own reason is not emitted |
| `fn-shape` | opaque:legacy-runtime-error | import-opaque:legacy-runtime-error | opaque:legacy-runtime-error | opaque:legacy-runtime-error | opaque:legacy-runtime-error | stays retired: its own reason is not emitted |
| `statement:Try` | - | module-error:syntax | - | - | - | stays retired: its own reason is not emitted |
| `session:function-assignment` | assign-free-name | assign-free-name | statement:FnDecl | statement:FnDecl | statement:FnDecl | stays retired: its own reason is not emitted |
| `session:capture-contract` | compiled | compiled | context:assignment-error | compiled | compiled | stays retired: its own reason is not emitted |
| `coro-main-cell` | compiled | coro-module-init-task, coro-module-init-task, coro-module-private-asyn | statement:FnDecl | statement:FnDecl | statement:FnDecl | **un-retired: E2/F12 (consistency)** |
| `coro-collection-callback` | coro-collection-callback, method:map | coro-module-init-task, coro-module-init-task | statement:FnDecl | statement:FnDecl | statement:FnDecl | **un-retired: E2/F12** |
| `free-name:legacy-binding` | free-name:legacy-binding | free-name:legacy-binding | statement:FnDecl | statement:FnDecl | statement:FnDecl | **un-retired: D** |


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
3a. **Reasons that are messages.** `async(callable, ...) was removed; ...` and `await(...) was removed; ...` (compiler_thread.cpp:19-21)
   are refusals whose reason string is the error text. PROPOSAL: name them (e.g. `removed-async-call`, `removed-await-call`)
   and keep the text as the message; a src change with the `type:*` rename (sha256 changes). C119 (position review
   Low): `non-transferable` is the same case; its emitted reason is the long message ("non-transferable resource: cannot
   transfer ..."); name it (e.g. `thread-arg-non-transferable`) with the others.
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

## 11. Host built-ins in programs with imports: root cause and repair (reviewer finding, PROPOSAL)

Recorded from the C114 confirmation review, Part 2. **A reviewer finding marked PROPOSAL, not a ruling.** The rescan did
not investigate this family (owner 00:08); it records the raise site and representatives (section 1.1, rank 1).

**Root cause.** `script_runner.cpp:325-339`: after a linked unit compiles, `first_module_host_op` (module_host_op.h:29)
scans every proto of the linked program; any CallNative of a host built-in (`natives::host_builtin(id)`) refuses the
unit with `module-host-op:<name>`. The refusal inventory uses the same scan (refusal_inventory.cpp:226).

- **What it protects: ruling R1 (owner 2026-10-21) on module read order.** The VM links a program with static imports by
  reading every module's source before execution; legacy reads each module when its `import` executes. A host operation
  legacy runs before or between imports could observe or change different files (`cp`/`remove` of a module, `cd`,
  `setenv`), and the host trace order (`NIFT_TRACE_HOST`) would differ. Pinned by `tests/runtime2/module_host_order.py`;
  `module_host_order_mutant.sh` (`-DNIFT_R1_MUTANT`) is its negative control.
- **The refusal is general:** every proto (entry and modules), uncalled functions included
  (`import("./m.f")` / `fn(never()) { return pwd() }` / `print(k)` is refused; legacy prints 1), read-only and
  side-effecting ops alike, and ops placed after every import (`host-after-import` is pinned as refused although the
  legacy order is then identical).
- **B4b and B5 do not depend on it**: only the ordering of module reads against host side effects and trace lines is at
  stake.

**Proposed bounded repair.** Replace "any host op anywhere" with "no host-op execution can precede the last module read
in legacy order". Admit only when both hold:

1. The entry's imports form a leading block: every top-level statement before the last `import` is an `import` or a
   call-free declaration (`fn`/struct declaration, literal or constant binding).
2. No module's initialisation code can run a host op: module top-level statements contain no host CallNative and no
   call (conservatively: no call other than non-host built-ins), transitively through nested imports.

Everything else stays refused with `module-host-op:<name>` (dynamic and nested imports are already refused:
`import-nested`, D-065). Read-only and side-effecting ops are treated alike (a read-only allowance alone would be unsound
for the trace and for `cd`/env-dependent resolution).

| Aspect | Proposal |
| --- | --- |
| Files and size | new `module_host_order.{h,cpp}` with the two checks (about 120-180 lines); `script_runner.cpp` and `refusal_inventory.cpp` call it instead of the blanket scan (one shared implementation); `module_host_op.h` keeps `first_module_host_op` for the message |
| Tests | extend `module_host_order.py`: admitted, with identical trace, tree and output: a host op after all imports (read-only and mutating), in a function called after the imports, inside an imported module's function called after all imports, and in an uncalled function; still refused: host-before-import (top, function, loop), copy-over-module, remove-module, host-in-module-init with a later sibling import, a nested-module function called during init, and `cd` or `setenv` before an import |
| Mutants | prefix check disabled (caught by host-before-import, copy-over-module); module-init check disabled (host-in-module-init); "calls count as safe" in the prefix (host-before-import-fn); keep the R1 mutant |
| Must not change | the linked-unit model and module-read timing; R1's refusal wherever a host op could precede a module read; sessions, eval and the CP22/CP23 coroutine-module rules; no re-baselining |

## 12. Struct refusals by root cause (reviewer finding, PROPOSAL)

Recorded from the C114 confirmation review, Part 3. **A reviewer finding marked PROPOSAL, not a ruling.**

| Group | Rows | Shared root |
| --- | --- | --- |
| A, declaration model | `struct-field-init`, `struct-after-statement`, `struct-nested`, and the declaration-validation rows (`struct-name`, `struct-redeclared`, `struct-shadows-fn`, `struct-member-clash`: move them to use time) | schemas planned up front with constant defaults (structs.cpp:78-109) |
| B, construction in loop context | `context:struct-new` (former `struct-construction`), `context:struct-field` | legacy's L42 loop behaviour (compiler_structs.cpp:278, compiler_assign.cpp:204) |
| C, struct identity tracking | `struct-assign`, `struct-value`, part of `struct-no-method` | single-`struct_id` binding typing (compiler_assign.cpp:140/209/281/318, compiler_structs.cpp:64/206/210) |
| D, sessions | `presentation-struct-method:*` | session dispatch (compiler_presentation.cpp:40) |
| E, legacy errors | `struct-private`, `struct-no-field` | raise-at-point, under the early-refusal policy |

`type:element-assignment` and `type:for-source` belong with the inference work, not with structs.

**Legacy facts (measured by the reviewer).**
- Defaults are evaluated per construction, in order, with fresh values: `struct(S) { id := next() }` gives 1 then 2;
  `xs := [1]` with `push` on one instance gives `[1,2]` and `[1]`. Defaults that reference earlier bindings or functions
  work only when the struct is declared after them, so non-constant defaults and `struct-after-statement` are one root.
- L42: legacy fails member access on structs inside loop bodies ("value has no member"); only construct-and-store in the
  loop works.
- `S().field` directly on a call result errors in legacy ("current JSON value is not an object"); use `a := S(); a.v`.

**Recommendation (PROPOSAL):** CP2 = Group A + Group B limited to construction and storage in loops (about 300-400 lines
of src, about 500 of tests and mutants; must preserve fresh per-construction defaults (extend the R6 refsem mutant to
expression defaults), no shared mutable defaults, initialiser side-effect order, loop-local captures of instances, B5
semantics, bounded scope and no re-baselining).
CP2b = Group C (about 250-350 lines; B4b struct-id soundness, B5 aliasing of instances). Group D with the B4 session
work; Group E with the early-refusal policy. L42 needs an owner ruling: reproduce legacy's loop-context errors, or
approve one meaning everywhere (D-040 value semantics).


## 13. Proposed rulings for the owner

In the owner's order. Items marked settled restate a ruling; every other item is a PROPOSAL or a question.
Provisional priorities (reference only, not rulings): P0 host built-ins with imports; P1 postfix spelling, nested fns,
`?.`/`?[]`, structs, variadics/spread, const objects; P2 export in entry files, irregular comparisons, indexed postfix
and async forms.

### 13.1 How the early-refusal policy applies

1. **Settled (owner 00:29, D-013):** rejecting before execution a program the VM can determine will fail is the
   established design; earlier timing and absent earlier output are approved; case B stays B, case C is internal
   failures and genuinely different categories only. Governs the 321 class (ii) rows (`policy:early-refusal`), for
   errors legacy raises at the same construct. Valid constructs stay capability gaps; wrong positions are not excused.
2. **Dead code (question).** 316 of the 321 (ii) refusals also fire in code that never runs, in programs legacy
   completes (`fn(f()) { print(1, 2) }` never called; `if (false) { sha256("a", "b") }`; `if (x > 1) { y := "a" - 1 }`
   with x <= 1). Not covered by 00:29, because these programs do not fail. Options, neutrally:
   (a) refusing statically invalid code even when it is unreachable is an approved part of the early-refusal design; or
   (b) only provably reachable errors may refuse; dead-code cases are capability gaps.
   Counts: fires in dead code 316; does not fire 1 (`coro-module-before-import`, flow-sensitive); `import-dynamic` in a dead
   block is refused as `import-nested`; 3 module-initialisation rows always execute. Corpus impact: 0 corpus programs
   for the remaining (ii) rows (the 7 legacy-completing corpus units belonged to rows C115 moved to (i)). Until this is
   ruled, the (ii) rows stay PENDING-OWNER.
3. **The 17 different-position rows (section 9.7):** 7 "fix: take legacy's position" (`context:struct-field`,
   `coro-module-before-import`, `fn-called-before-declaration`, `for-missing-write`, `string-arg-type`,
   `import-opaque:legacy-runtime-error`, `import-limit`); 10 "VM more precise" with a rationale: accept those 10?
   (`prepared-decl-type` and `opaque:legacy-runtime-error` also need legacy's position; they moved to (i).)

### 13.2 The 152 capability gaps (class (i)), by feature (section 1.1)

4. Host built-ins in any program with an import (27 rows): see 13.4.
5. Postfix spellings; nested and redeclared function declarations; `?.`/`?[]`; const objects; variadics/spread/
   destructuring; irregular comparisons; `export` in entry files: require for the first VM release?
6. Structs (incl. the four declaration-validation rows moved to (i)): see 13.5.
7. The owner-excluded checkpoint-1 assignment forms (`a[0]++`, `print(a[0] += 5)`, `hs[0][0] = f(1)`): schedule together.
8. Interpolation of unproven kinds, lazy built-in argument order (incl. `slice`, `get` default), loop-context built-ins
   (`.length()`, `empty()`, `to_int`, `%=`), futures in ordinary expressions, capacity limits, the remaining singles:
   require, or schedule after the first release? Confirm `type:element-assignment`, `type:for-source` and
   `struct-no-method` belong with the inference and struct work.

### 13.3 Session-only rows (class (iii), 3)

9. `builtin-receiver:stringify`, `presentation-struct-method:*`, `session:opaque`: schedule with the B4 session work, or defer?

### 13.4 Host built-ins with imports: diagnosis and repair (section 11, reviewer PROPOSAL)

10. Approve the bounded repair design (leading import block, init code free of host ops and calls; R1 kept wherever a
    host op could precede a module read)?

### 13.5 Checkpoint-2 scope (section 12, reviewer PROPOSAL; section 10 item 4)

11. CP2 = Group A (declaration model) + Group B (construction and storage in loops); CP2b = Group C (identity)?
12. L42: reproduce legacy's loop-context member errors, or approve one meaning everywhere (D-040)?

### 13.6 Other items

13. `fn-flags`: does D-048's rejection-before-execution cover a VM-only refusal, with the legacy change declined?
14. `import-dynamic`: is D-037 its G2 release approval?
15. Ratify the CP23 review qualification `qualified:legacy-await-chain-defect` (async self-recursion runs on the VM where
    legacy errors; deep_chain.f, qualified output `1275` pinned in `qualified` at tests/runtime2/cp23/compatibility.py:9).
16. `difference:import-depth` (class (iv)): import chains 33-63 deep run on the VM and fail in legacy; accept, or make the
    VM fail at legacy's depth (which also fixes `import-limit`'s position)?
17. Family rows (PROPOSAL, confirmation review): family rows for the pure (ii) families `builtin-arity:*`,
    `builtin-receiver:*`, `method:*`, `callable:*`, each stating its contract, exact rows only for members that behave
    differently; only after the `type:*` fix; no rows collapsed until the owner decides.
18. Src-side fixes (each changes the sha256): the F12 inventory-limits fix (then retire the 16 F12 rows); rename `type:*`
    (e.g. `type:mul`); name the two message-named reasons (`async(...)`/`await(...)` "was removed").
19. Deferral candidates: dynamic scope (`free-name`, `free-name:dynamic-caller`); `import-reinitialised` (D-041 #2
    removal condition); CP24 project APIs; comparator-sort portability (Linode); family dispositions in sections 1, 5-9.

## 14. Gates at the tip (C116)

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS (classification self-test, 31 cases) |
| `make test-rt-lint` | PASS (0 problems in 710 files) |
| `make test-test-integrity` | clean (701 files scanned, 0 findings) |
| fresh `inventory.py collect` + `report --check` (C117 tsv) | **exit 3** |
| `python3 -B tests/runtime2/assign_family.py --nift $PWD/nift` | PASS (94 cases) |
| `tests/runtime2/named_deferrals.py` | PASS (11 constructs, 5 new D-067 cases) |
| `make test-runtime2-import-depth` (new) | PASS |

- **Inventory (C117)**: G1 PASS, 155 reasons seen: approved 9, deferred-named 29, implement 27, PENDING-OWNER 90,
  retired 0. Exit 3 because 90 seen reasons are still PENDING-OWNER (the intended BLOCKED state). No retired reason is seen.
- **Production code**: `git diff 0c66cad6 -- src/` is empty; nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.

## 15. Release-gate issue, still outstanding

`tests/v44_root_path_corruption_reproducers.sh` **FAILS** at the tip (re-run in C112): `destruct` (`declare-pattern`),
`structarr` and `mapstruct` (`context:struct-new`). `escaped-ref` passes. The file is not passing; the failures are
recorded on the `declare-pattern` and `context:struct-new` rows.

**Tracked issue (C115 final confirmation, L-2): no gate checks class (iv) differences.** A behaviour where the VM
accepts a program legacy rejects, such as `difference:import-depth` (import chains 33-63 deep), is not an emitted
reason, so `inventory.py --check` cannot see it; only a corpus program in an rt-diff corpus would expose it. Tracked
as a separate issue after the inventory freezes.

## 16. Linode-only items (not run here)

- The execute pass on the release toolchain: `make cp25r-inventory`.
- `method:sort` comparator portability on GCC 15.2 (cp14b-sort's 234 unverified cases).
- Sanitizer runs of the gates in section 4.

## 17. Change logs per review round

### 17.1 C112 changes per review finding

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

### 17.2 C113 changes per confirmation-review finding

| Finding | Change |
| --- | --- |
| P2-A | `builtin-receiver:stringify` restored to PENDING-OWNER, G (sessions); four-way probe rule applied to every retired row (9) and every unreachable or not-reached literal: `struct-no-method` and `type:element-assignment` restored too; `compound-operator`, `limit:atoms`, `limit:too many registers`, `limit:jump distance exceeds 16 bits`, `path-in-call-argument:imported`, `import-opaque:legacy-runtime-error` added |
| P2-B | 8 rows added (`type:/`, `type:<=`, `type:>=`, `type:for-source-union`, `expression:assignment-target`, `coro-future-flow:Assign`, `coro-future-flow:Call`, `context:collection-empty`); every run-time-built family enumerated and probed: 270 rows added, `callable:*` family row added, the rest recorded in section 9.2; `type:*` and `method:*` absorption listed |
| L-1 | `method:increment` row deleted (emitted, so not retirable; `method:*` covers it) |
| L-2 | section 13 item 10 cites `qualified:legacy-await-chain-defect` and asks for ratification |
| L-3 | section 13 items 6 and 8 added |
| L-4 | section 3.3 lists `context:assignment-error`'s filesystem-recipe case |

### 17.3 C114 changes per confirmation-review finding

| Finding | Change |
| --- | --- |
| P2-1 | `type:for-source` restored to PENDING-OWNER, G (infer); uncalled function and lambda bodies added as the fifth probe context; the 4 retired rows and every section 9.1/9.2 claim re-probed (9.4); `text-type-name` D → G; retirement policy written (9.5) |
| P2-2 | `coro-future-flow:Coalesce` row added; every parent re-probed in natural spellings (9.2): `:Binary` has a legacy-running form |
| L-1 | `expression:assignment-target`, `coro-future-flow:Assign` tagged class G in the tsv |
| L-2 | section 13.2 item 9 cites the pinned output `1275` (compatibility.py:9), not a line number |
| L-3 | rename proposal for the two message-named reasons (section 10 item 3a, section 13.2 item 11) |
| L-4 | `statement:FnDecl` covers functions declared in functions, loops and ifs (five-way evidence; section 1.1 rank 3) |
| Item 4 | section 1: outcome classes for all 530 rows, `outcome:` tag in every row, (i) families ranked; evidence fixed for the (i) rows whose program failed in legacy |
| Item 5 | section 13.2 item 10 (PROPOSAL) |
| Owner 00:08 #1 | class (ii) split into ii-A/B/C by a print-before probe of every row: A 0, B 335 (contracts B-same-position 303, B-different-position 32), C 0; tag `outcome:ii-B` with category, position and side effect in every row; the single-mechanism question removed |
| Owner 00:08 #2 | every retired, D, E1 and E2 row carries a TRACE note (reproducer, why it was a refusal, the explanation, why not independent); text dropped by C112/C113 repairs restored as previous-* notes (10 evidence segments, 41 legacy columns, 25 dispositions); the deleted `method:increment` row kept as a tsv comment and in section 9.6 |
| Owner 00:08 #3 | completeness boundary (section 1.3) |
| Owner 00:08 #4 | section 13 arranged under the three headings |

### 17.4 C115 changes (C114 confirmation review and owner rulings 00:08, 00:29)

| Item | Change |
| --- | --- |
| Owner 00:29 | early-refusal policy recorded in sections 1 and 13.1; 321 (ii) rows tagged `policy:early-refusal(D-013, owner 00:29)`; status unchanged pending the dead-code question |
| 1 (P2-1) | 12 rows (ii) → (i) with the reviewer's programs (verified); all (ii) probes re-run with `zz := X`; 2 more rows → (i) from the corpus measurement (`prepared-decl-type`, `opaque:legacy-runtime-error`); split (i) 152, (ii) 321, (iii) 3, (iv) 1, D 38, E 20 |
| 2 (P2-2) | 15-idiom statement-level rescan (section 1.3): `fn-redeclared`, `too-many-functions`, `limit:invocation-arguments`, `session:opaque` added (reachable); 7 unreached rows added as retired at creation with five-context probes |
| 3 | dead-code measurement per (ii) row and the neutral question (13.1 item 2) |
| 4 | section 9.7 per-row table for the 32; `type:for-source-union` probe replaced (same position); `import-limit` (iv) verified: row `difference:import-depth`, item 16 |
| 5 | sections 11 and 12 (reviewer Parts 2 and 3, PROPOSAL); items 10-12 |
| 6 | section 13 in the owner's order |
| 7 | row counts reconciled (528 → 530 → 535); inventory identity recorded |

### 17.5 C116 changes (C115 final confirmation)

| Item | Change |
| --- | --- |
| P2-1 | `free-name:legacy-binding` un-retired: D, PENDING-OWNER (classify-all emits it on its own probe) |
| P2-2 | `coro-collection-callback` un-retired: E2/F12, PENDING-OWNER (classify-all, !parallel); `coro-main-cell` given the same E2/F12 treatment |
| Policy | classify-all added as the sixth context of the retirement rule (9.5); the four remaining retired-at-creation rows and the four older retired rows re-checked (9.8) |
| L-1 | TRACE labels on the retired-at-creation rows |
| L-2 | class (iv) gap recorded as a tracked issue (section 15) |
| L-3 | section 12 must-preserve list completed |
| L-4 | the 14 reclassified rows keep their C114 ii-B tags as "Previous outcome" notes |
| L-5 | section 13.1 option (a) stated without the precedent clause |

## 18. Owner rulings 2026-11-11 02:53 (D-067), applied in C117

decisions.md D-067 records the rulings; the inventory applies them (each row cites "D-067 #n (owner 2026-11-11 02:53)").

| D-067 item | Applied to |
| --- | --- |
| #1 positions | the 7 position fixes set to `implement`; the 10 VM-more-precise positions marked approved-pending-review (still PENDING-OWNER); C119: all 10 verified by the review, 5 approved, 5 on mixed rows kept PENDING-OWNER (section 19) |
| #2 deferrals | 19 rows `deferred-named`: `incdec-target`, `expression:assignment-target`, `coro-future-flow:Assign`; `builtin-receiver:stringify`, `presentation-struct-method:*`, `session:opaque`; `free-name`, `free-name:dynamic-caller`; the CP24 rows (`callable:build`, `build_all`, `build_names`, `inject`, `status`, `track`, `tracked`, `untrack`, `build_repair`, `opaque:template-text`; D-053); `import-dynamic` (D-037 cited, not broadened). known-limitations.md "Further deferrals"; named_deferrals.py gains 5 cases |
| #3 `@fn` | `fn-flags` approved; amendment recorded under D-047 and D-048 |
| #4 import depth | `difference:import-depth` approved; kMaxModuleDepth = 64 (module_graph.h:122) documented as a deliberate resource bound of the recursive graph walk (module_graph.cpp:275, introduced with the loader in CP12b f45f52d3); tests/runtime2/import_depth.py (`make test-runtime2-import-depth`) pins depth 33 running, legacy failing at 33, and depth 64 refused |
| #5 not ratified | async self-recursion and `import-reinitialised` untouched (PENDING-OWNER) |
| #6 | sort-comparator portability stays a mandatory Linode item (section 16) |
| #7 required | postfix, nested fn, `fn-redeclared` (document the later-definition-wins behaviour), `?.`/`?[]`, const objects, `export`, bytes, `warn`/`err`/`read`: `implement` (remaining approved features); variadics/spread/destructuring: CP3; structs: CP2 (declaration model, construction in loops) and CP2b (`struct-assign`, `struct-value`, `struct-no-method`) |
| #8 host-import guard | 27 `module-host-op:*` rows `implement`, sequence slot 2 |
| #9 investigate first | irregular comparisons, interpolation, loop-context built-ins, futures in expressions, capacity limits and the remaining (i) singles: direction recorded per row, PENDING-OWNER |
| #10 family grouping | family rows `builtin-arity:*` (123 members), `builtin-receiver:*` (77), `method:*` (40), `callable:*` (10) state their contracts; members keep their rows and evidence, marked "member of <family>"; exceptions unmarked; nothing deleted |
| #11 dead code | Option A withdrawn; the 321 (ii) rows are reclassified in C118 (docs/runtime-2/checkpoints/dead-code-admission.md); revised in C119 (section 19) |

## 19. C119: review of C117/C118 applied (tsv and docs only)

The review (C117 CONFIRMED; C118 dead-code proposal SOUND WITH CHANGES) is applied; docs/runtime-2/checkpoints/dead-code-admission.md is revised.

| Finding | Change |
| --- | --- |
| P1-1 design | design (b): defer only kind-certain sites, through an allow-list; (a) (a run-time-checked op) would implement the union operation, a class (i) capability, not a deferral (dead-code-admission.md 5.1) |
| P1-2 type rows | 19 rows (ii) → (i) mixed, with union and single-kind programs in each row's evidence: (i) 171, (ii) 302; X (ii) 280, plus 19 single-kind sub-cases |
| P2-1 dynamic scope | `callable:*` (family and 10 members), `assign-free-name`, `assign-free-name:undeclared` and `free-name*` (17 rows) marked deferral-excluded, with the probes `h(1)`, `q = 3` and `h.size()` |
| P2-2 order | per-site table of legacy order and message (dead-code-admission.md 5.2); every (ii) X row is marked ALLOW-LISTED (249) or NOT allow-listed (19) or excluded (12); 15 of the 19 mixed rows' single-kind sub-cases allow-listed |
| P2-3 Bottom | a distinct `kKindBottom` bit, the join rule, the trap for `{Bottom}` operands and a mutant (5.3) |
| P2-4 inventory | `deferred:<reason>` records in classify-all; `reason_key` strips the prefix; `--check` applies the row's status; test (5.4) |
| P2-5 host guard | options (a) (the slot-2 repair covers uncalled and untaken host ops; soundness: no host op runs before the last module read) and (b) (accept the gap), for the owner (5.5) |
| P2-6 S rows | legacy accepts all 10 S constructs in dead code (probes in each row's evidence); `jump-outside-loop` marked OWNER RULING NEEDED; `text-type-name` probe fixed (an uncalled lambda) |
| Must-not-change, mutants | extended (5.7): D-013 early refusal on non-allow-listed sites, exit 1 never 70, heap budget, per-unit sessions and eval, async raise rejects the future (CP22 L1/L4) with a test; 13 mutants |
| Positions (D-067 #1) | all 10 pass; approved: `builtin-arity:deepcopy`, `coro-module-private-async`, `method:nope`, `non-transferable`, `static-error:push-arity`; verified but PENDING-OWNER (mixed rows): `coro-future-flow:ForBinding`, `coro-future-flow:Throw`, `interpolation:non-scalar`, `type:member`, `type:script-return` |
| Lows | `which` in the platform list; the corpus base made reproducible and 318/266/298 reconciled (4.1); `number-literal` implement vs S reconciled; `non-transferable` added to section 10 item 3a; `type:script-return` position note (1:8) |
| Not changed | the slot labels of the D-067 #7 required features (put to the owner by the coordinator) |

Statuses: 609 rows, approved 51 (+5), deferred-named 36, implement 76, PENDING-OWNER 438, retired 8. No row was removed.

Gates at C119 (tsv revision of this commit, pinned binary):

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS (historical control; classification self-test, 31 cases) |
| `make test-rt-lint` | PASS (0 problems in 710 files) |
| `make test-test-integrity` | clean |
| fresh `inventory.py collect` + `report --check` | **exit 3**: G1 PASS, 155 reasons seen: approved 11, deferred-named 29, implement 27, PENDING-OWNER 88, retired 0 (the intended BLOCKED state) |
| `tests/runtime2/named_deferrals.py --nift $PWD/nift` | PASS (11 constructs, 0 failures) |
| `make test-runtime2-import-depth` | PASS |
| `tests/runtime2/assign_family.py --nift $PWD/nift` | PASS (94 cases) |

Production code: `git diff 0c66cad6 -- src/` is empty; nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.

## 20. C120: review of C119 applied (tsv, docs and one pinned test)

The C119 re-review found the plan SOUND WITH CHANGES, with two P1s. The name-certainty option is sound with
conditions, and host guard (a) is sound with small changes. docs/runtime-2/checkpoints/dead-code-admission.md is
revised.

| Finding | Change |
| --- | --- |
| P1-1 sessions | session units never defer a kind- or name-dependent site (argued against routing through the session re-check); session tests (kind and name programs) and the mutant "session deferral frozen"; the C119 "decides independently" wording corrected |
| P1-2 built-in precedence | name certainty for every callee-named site, including the 57 allow-listed builtin-arity free-function members; legacy precedence probed per member in four binding forms: the binding wins for 11 (`cancel`, `channel`, `deepcopy`, `map`, `recv`, `send`, `set`, `sha256`, `sha256_file`, `stack`, `validate`), the built-in wins for 50, and 62 have no free form; recorded per row ("C120 precedence"); the hole is hidden today only by `binding-name-reserved`, `fn-name-reserved` and `param-name-reserved` |
| P2-1 kind certainty | defined per site: the set is non-empty, has no Unknown and no Bottom, and no member is admitted by the operation (Number = Int\|Frac, File/Stream = Handle\|bit) |
| P2-2 Bottom | `kKindBottom = 1u << 24`, distinct from the 12 existing `kinds == 0` uses (audit table); the assertion is scoped to the admission and deferral entry points |
| P2-3 | 5.1(2) qualified by the approved semantic differences (B5 parameter mutation example) |
| Name certainty | extended to `callable:*` (11 rows ALLOW-LISTED under name certainty); scan coverage list (every unit and binding form, nested and conditional fn declarations, struct methods, imports and exports, the embed host registry; sessions excluded); dependency on four whole-program refusals pinned by the new `tests/runtime2/deferral_dependencies.py` (`make test-runtime2-deferral-dependencies`, in `test-rt-vm`); the owner's `if (false) { nope() }` example works under the plan |
| Host guard (a) | restated as the proposed slot-2 design: leading import block; call-free module init with `export`/`import` exempt and non-host, callback-free natives allowed; "call" defined; transitive; a complete host set with a test; tests and mutants; about 150-220 src lines |
| Mutants | five added (session deferral frozen, builtin-arity deferred despite a binding, name scan misses a nested or conditional fn or a struct method, export not exempted, interpolation call missed in init), plus a kind-0 assertion mutant |
| Coverage | X (ii) 260 of 280, X + L-defer 284 of 309; 7 of the 11 refused platform patterns fixed, 8 with host guard (a) |

Statuses unchanged: 609 rows, approved 51, deferred-named 36, implement 76, PENDING-OWNER 438, retired 8.

Gates at C120:

| Gate | Result |
| --- | --- |
| `make test-runtime2-certification-guard` | PASS (31 cases) |
| `make test-rt-lint` | PASS (0 problems in 710 files) |
| `make test-test-integrity` | clean (702 files, 0 findings) |
| fresh `inventory.py collect` + `report --check` | **exit 3**: G1 PASS, 155 reasons seen: approved 11, deferred-named 29, implement 27, PENDING-OWNER 88, retired 0 |
| `tests/runtime2/named_deferrals.py` | PASS (11 constructs) |
| `make test-runtime2-import-depth` | PASS |
| `make test-runtime2-deferral-dependencies` (new) | PASS (5 whole-program refusals) |
| `tests/runtime2/assign_family.py` | PASS (94 cases) |

Production code: `git diff 0c66cad6 -- src/` is empty; nift sha256 `8195f1b34f91d018e336451153aa55bf0e05d590f9acfcd0dffa566ba35834e4`.

