# CP12 phase B — coverage inventory (every refusal reason, every unit)

Status: inventory complete (step 1 of phase B). Branch `runtime-2-cp12b-dev`
(local, not pushed), base `runtime-2` @`ed7d64d9`. Raw data and the full
analysis: `../evidence/cp12b/inventory-baseline/` (`analysis.txt` is the
complete printout; `summary.json` the same in JSON; one JSON line per unit in
`shared.jsonl`, `projects.jsonl`, `generator.jsonl`). The phase B plan and
implementation record: `cp12b-resolver.md`.

## Method

**Classify-all.** `NIFT_VM=classify-all` is `NIFT_VM=classify` (parse and
compile only; nothing executes; same `outcome` and first `reason`) plus a
`reasons` list with every refusal the unit would hit
(`src/rt/refusal_inventory.h`). The compiler stops at its first refusal, so the
inventory compiles the unit again and again on a private copy of the AST,
masking the refused construct each time:

* the smallest expression that begins at the refused position becomes `null`;
  a refused call is masked whole, and its arguments are moved into a synthetic
  `if (true) { name := [args] }` before the statement so they are still
  compiled (lambdas, nested calls);
* a chained call refused as `postfix-receiver` (`s.trim().upper()`) is
  replaced by its receiver; the outer method's name is recorded (it would be
  refused next unless it is `length`/`size`);
* a refusal again at the same position masks the next larger expression, then
  the whole statement; `for` becomes `while (true) { body }` and `try` becomes
  `if (true) { try body } else { catch body }`, so their bodies are still
  inventoried; declarations after statements are hoisted;
* Opaque nodes are all recorded first (the runner's order), then masked.

A reason is **consequential** when masking may have caused it: it recurs at a
masked position, names a masked binding or function (free names, calls), or is
a kind/context reason at an expression that contains a masked node or name.
The counts below use the **certain** reasons (non-consequential); "strict"
figures also require the consequential ones. Known blind spots (the inventory
under-counts there): the iterable of a masked `for` header, operands of a
masked expression other than call arguments.

**Self-checks.** On every unit of every corpus the first inventoried reason
equals `classify`'s reason (0 mismatches); every inventory is complete except
one script with a parse error (where legacy fails too). `tests/runtime2/vm_inventory.py`
pins the mode (12 cases: exact certain sets, no execution, no files written,
outcome identical to `classify`). Free names are refined by the CP12b globals
analysis (`src/rt/globals.h`): `free-name:global` when the name provably
resolves to the top-level binding, otherwise why not.

**Corpora** (`tools/rt-vm/cp12b_inventory.py collect`):

| Corpus | Units | What |
| --- | ---: | --- |
| shared | 789 | exactly `test-rt-vm`'s rt-diff set: vm-corpus, semantics, rt-diff-corpus, conformance cases, cp410 probes, scripts extracted from smoke tests, and rt-diff's own generator (`--gen 200 --seed 1`); keyed as `vm_parity.py` keys them (the CP12 record's "786" was measured earlier; the same set gives 789 at `ed7d64d9`, 102 compiling) |
| — shared-curated | 589 | the shared set without rt-diff's generated programs |
| — shared-rtdiff-gen | 200 | rt-diff `gen.py` programs (all refused: each mixes imports, async, threads, `for`...) |
| projects | 86 | the CP13 zero-false-rejection real-project units (Labs and nift-packages/nift-experiments repositories, `scratchpad/cp13/projunits`, scripts and `@script` bodies) |
| generator | 300 | the general generator (`vm_gen.program(1, i)`, as `vm_parity.py`) |

## Headline

| | shared (789) | curated (589) | projects (86) | generator (300) |
| --- | ---: | ---: | ---: | ---: |
| compile for the VM today | 102 (12.9 %) | 102 (17.3 %) | 0 | 72 (24.0 %) |
| refused | 687 | 487 | 86 | 228 |
| … by 1 distinct certain reason | 153 | 153 | 7 | 97 |
| … by 2 | 156 | 155 | 20 | 29 |
| … by 3 | 66 | 62 | 17 | 18 |
| … by 4 or more | 312 | 117 | 42 | 84 |
| **all of CP12b scope supported** (every certain reason in CP12b) | **+77 → 179 (22.7 %)** | **+77 → 179 (30.4 %)** | **+14 → 14 (16.3 %)** | +0 |
| … strict (consequential reasons count too) | +61 | +61 | +14 | 0 |
| CP12b + CP13 + CP14 + CP15 + CP16 | +145 → 247 (31.3 %) | +142 → 244 (41.4 %) | +16 | +2 |
| CP13–CP16 **without** CP12b | — | +40 | +2 | — |

* CP12b is the enabler: with CP12b, CP13–CP16 unlock 142 curated scripts;
  without it, 40.
* Even with every scheduled checkpoint (CP12b–CP24, language cleanup and the
  VM-subset precision work) about 180 curated scripts (≈30 %) stay refused for
  features **no checkpoint owns** (see "Unassigned" below). CP25's zero-fallback
  gate is unreachable on the current roadmap without assigning them.
* The real projects are the hardest: 42 of 86 have four or more distinct
  blockers and no reason is the sole blocker of any project unit except
  legacy-reserved parameter names (7).

## Reasons by scripts blocked (shared, all 789)

Certain / any (incl. consequential); "sole" = the only certain reason of the
script (unlocked if this alone were supported). Top 40; the rest in
`analysis.txt`.

| Certain | Any | Sole | Reason | Checkpoint / scope |
| ---: | ---: | ---: | --- | --- |
| 155 | 321 | 0 | `postfix-receiver` | VM subset: chained postfix on a call result |
| 227 | 227 | 2 | `fn-after-statement` | CP12b resolver (200 of them rt-diff gen) |
| 19 | 209 | 0 | `unresolved-name` | CP12b GETDYN / modules |
| 0 | 188 | 0 | `prepared-unpreparable` | VM subset (D-018) |
| 173 | 173 | 2 | `method:stringify` | CP14 |
| 142 | 142 | 2 | `statement:For` | **unassigned** |
| 141 | 141 | 11 | `statement:Import` | CP12b modules |
| 114 | 114 | 1 | `fn-flags` (async, `@fn`) | CP22 / CP24 |
| 109 | 109 | 9 | `expression:Lambda` | **unassigned** |
| 98 | 98 | 0 | `callable:is_dir` | CP16 |
| 97 | 97 | 0 | `callable:thread` | CP16/CP22 |
| 93 | 93 | 0 | `method:contains` | CP14 |
| 92 | 92 | 1 | `method:split` | CP14 |
| 91 | 91 | 0 | `fn-as-value` | **unassigned** |
| 91 | 91 | 0 | `method:join` | CP14 |
| 85 | 85 | 1 | `statement:Try` | CP13 |
| 85 | 85 | 9 | `statement:StructDecl` | CP12b structs |
| 79 | 79 | 0 | `expression:Await` | CP22 |
| 71 | 71 | 2 | `callable:getenv` | CP16 |
| 78 | 78 | 2 | `free-name:*` (all refinements; 54 are `dynamic-caller` in rt-diff gen) | CP12b globals / GETDYN |
| 54 | 54 | 2 | `statement:Throw` | CP13 |
| 53 | 53 | 9 | `statement:Export` | CP12b modules |
| 52 | 57 | 2 | `static-error:member-assign` | CP13 |

(The rt-diff generator makes up 200 of the 789 and inflates its own
constructs: every gen program declares functions after statements, imports,
uses `for` and threads. The curated table in `analysis.txt` is the better
guide to real code: there `statement:Import` 80, `expression:Lambda` 58,
`fn-flags` 58, `statement:Export` 53, `fn-after-statement` 48,
`statement:StructDecl` 36, `postfix-receiver` 28 certain / 153 any.)

## Features: single and greedy (curated shared corpora)

Each certain reason mapped to its feature (checkpoint | scope). "Sole" =
scripts unlocked if that feature alone were supported.

| Blocked | Sole | Feature |
| ---: | ---: | --- |
| 124 | 22 | CP12b modules/imports (`import`, `export`) |
| 92 | 12 | CP16 host ops |
| 90 | 5 | CP14 built-ins |
| 88 | 10 | unassigned: first-class functions / lambdas (`=>`, functions as values, calling a binding) |
| 66 | 28 | VM subset precision (D-018 context refusals, kinds, aliasing) |
| 60 | 7 | unassigned: legacy built-ins no checkpoint schedules (`map`, `set`, `mutex`, `cmd`, `ffi_*`, `sha256_file`) |
| 58 | 1 | CP22/CP24: `fn[async]`, `@fn` |
| 57 | 1 | unassigned: methods not in CP14/CP15 (`push` as an expression, `add`, `close`, `total`...) |
| 48 | 2 | CP12b resolver: function declarations after statements |
| 38 | 0 | unassigned: lambda-taking collection methods (`map`, `sort_by`, `filter`...) |
| 38 | 15 | CP13 errors, try/catch, static errors |
| 37 | 10 | CP12b structs |
| 33 | 0 | CP12b modules: imported functions (`callable:` in an importing script) |
| 30 | 6 | CP24 template functions (`inject`...) |
| 28 | 0 | VM subset: chained postfix on call results |
| 23 | 0 | CP22 `await` |
| 21 | 2 | unassigned: `for` loops |
| 17 | 7 | language cleanup: names legacy reserves (L6, D-009#3) |
| 17 | 0 | CP12b structs: methods of user structs |
| 16 | 0 | CP12b modules: imported bindings (unresolved names in an importing script) |
| 12 | 7 | language cleanup: legacy run-time-error constructs (`opaque:legacy-runtime-error`) |
| 11 | 2 | CP12b globals (statically resolvable free names) |
| 10 | 6 | CP24 template-only constructs (expression statements, template text) |

Greedy climb (each step adds the feature that unlocks most together with the
previous ones; 102 → …): VM-subset precision +28 → 130 (22.1 %); CP12b modules
+22 → 152; CP22/24 async/`@fn` +23 → 175; CP13 +21 → 196; CP12b imported
functions +18 → 214; CP16 +16 → 230; lambdas +18 → 248; lambda-taking methods
+18 → 266; other methods +24 → 290; unscheduled legacy built-ins +21 → 311;
CP14 +31 → 342 (58.1 %); CP12b function declarations +21 → 363; CP12b structs
+18 → 381; CP24 template functions +18 → 399; CP22 await +18 → 417; chained
postfix +15 → 432; `for` +16 → 448 (76.1 %); … 25 features → 528 (89.6 %).

Within CP12b alone (curated, certain): modules +22, + imported functions → 40,
+ structs → 51, + function declarations → 57, + struct methods → 64, +
globals → 67, + unknown callables → 70, + refused globals → 72, + imported
bindings → 74, + GETDYN → 76, + dynamic-caller → 77. Strict: modules +22 →
38 with imported functions → 44 with function declarations → … 61.

Per checkpoint, cumulatively in roadmap order (curated): CP12b +77 (30.4 %),
CP13 +103, CP14 +110, CP15 +112, CP16 +139 (40.9 %), CP22 +145, CP22/CP24
+188, CP24 +207 (52.5 %), CP19 +210, language cleanup +232, VM subset +271,
D-018/kinds precision +307 (69.4 %), unassigned +486 (99.8 %).

## Reasons mapped to checkpoints

| Reasons | Checkpoint |
| --- | --- |
| `free-name*`, `assign-free-name*` (globals; refinements below), `unresolved-name` (GETDYN: a top-level name nothing declares), `statement:Import`/`Export` and the `callable:`/unresolved names of importing scripts (modules), `statement:StructDecl`, `opaque:struct-body`, methods of scripts declaring structs (structs), `statement:EnumDecl` (enums: **0 units in any corpus**), `fn-after-statement`, `fn-redeclared`, `binding-shadows-fn`, `param-shadows-fn` | **CP12b** |
| `statement:Try`, `statement:Throw`, `static-error:*` | CP13 |
| `method:`/`callable:` names in the CP14 / CP15 built-in lists (`src/rt/natives/builtin_list.inc` on the integration / CP15 branches) | CP14 / CP15 |
| file, process, environment, time and thread names (`read`, `exists`, `is_dir`, `ls`, `run`, `getenv`, `file`, `open`, `thread`, …) | CP16 |
| `statement:Command` | CP19 |
| `fn-flags` (`fn[async]`, `@fn`), `expression:Await` | CP22 (async) / CP24 (`@fn`) |
| `opaque:template-text`, `opaque:deferred-translation`, `expression-statement`, `statement:TemplateStmt`, template functions (`inject`, `content`, …), legacy template/script bindings (`unresolved-name:legacy-binding`: `args`, `page`, …) | CP24 (bindings: CP16/CP24) |
| `opaque:irregular-legacy-parse`, `opaque:legacy-runtime-error`, `jump-not-last` (L5), reserved names (`*-name-reserved`, L6) | language cleanup |
| `type:*`, `context:*`, `aliasing`, `alias-argument`, `path-in-*`, `prepared-*` | VM subset precision (CP12 phase A's refusals; records20k `aliasing`/`push` is the integration owner's) |
| `postfix-receiver`, `interpolation`, `string-literal`, `number-literal`, `declare-const`, `declare-pattern`, `assign-target`, `compound-target`, `index-target`, … | VM subset (expression/statement coverage; no checkpoint names it) |
| `statement:For`, `expression:Lambda`, `fn-as-value`, `call-binding`, lambda-taking and other methods outside CP14/CP15, legacy built-ins outside CP14–CP16 (`map`, `set`, `mutex`, `cmd`, `ffi_*`) | **unassigned** |

Globals refinements (certain; all 789 / curated 589): `free-name:global`
19 / 7 scripts and `assign-free-name:global` 2 / 2 (resolvable by the
analysis; need the compiler hook), `dynamic-caller` 54 / 2 (genuine dynamic
scoping: only a caller's local resolves the name; 52 are rt-diff gen's
deliberate `free3`/`outer4` pattern), `undeclared` 3 / 3,
`not-one-top-level-binding` 1 / 1. Plain `assign-free-name` (2) is an
assignment to an undeclared name at the top level (legacy's error).

## Findings for the owner

1. **CP12b is worth +77 curated scripts (17.3 % → 30.4 %) and +14 project
   units, and it gates CP13–CP16** (142 vs 40 unlocked). Modules are most of
   it (≥ 40 of 77), then structs (with methods, 17), then declaration order,
   globals and GETDYN.
2. **Enums never occur** in the corpora; GETDYN proper (a free name only a
   caller's local resolves) occurs in 2 curated scripts. Both are cheap to
   leave refused (prefer refusal, D-015).
3. **Unassigned work blocks ≈ 180 curated scripts** whatever CP12b–CP24 do:
   `for` loops, lambdas / first-class functions, lambda-taking collection
   methods, other methods, and legacy built-ins outside CP14–CP16. CP25 needs
   owners for these.
4. **Chained postfix** (`a.b().c()`, `f().k`) blocks 153 curated scripts
   (28 certain, the rest hidden behind other reasons): a VM-subset gap, not a
   resolver one.
5. The project units have many blockers each; none is unlocked by one
   feature except reserved parameter names.

## Ownership of every refusal reason (owner decision, D-036)

Every refusal reason seen in any corpus has a named checkpoint or phase
(`owner()` in `tools/rt-vm/cp12b_inventory.py`; script context decides
methods of user structs, members of imported modules and calls to refused
declarations). No corpus was narrowed. New phases: CP12C (functions as
values, closures, lambdas, `for`, and the remaining statement and binding
forms), CP14B (callback-taking collection methods), CP15B (remaining pure
built-ins and methods, literal spellings, interpolation). Process,
concurrency and FFI names go to their natural checkpoints. Counts are
scripts carrying the reason (certain or consequential) over shared,
projects and generator.

| Owner | Scope | Depends on | Reasons (scripts carrying the reason, any, all corpora incl. generator) |
| --- | --- | --- | --- |
| CP12b | B1 declaration order (done) | CP12 phase A | `fn-after-statement` 234 |
| CP12b | chained postfix (this phase) | CP12 phase A | `postfix-receiver` 393 |
| CP12b | globals | CP12 phase A | `free-name:unanalysed` 23, `free-name:global` 19, `assign-free-name:unanalysed` 7, `assign-free-name:global` 4 |
| CP12b | modules | CP12 phase A | `statement:Import` 154, `statement:Export` 82 |
| CP12b | modules (imported binding) | CP12 phase A | `unresolved-name` 71, `assign-free-name` 2, `free-name:unanalysed` 1 |
| CP12b | modules (imported function or module member) | CP12 phase A | `method:total` 11, `callable:mf5` 8, `callable:mf10` 7, `callable:mf3` 6, `callable:mf4` 6, `method:parse` 6, `method:push` 5, `callable:mf2` 5, `callable:mf9` 5, `callable:mf14` 4, `callable:S1` 4, `callable:mf7` 4, `callable:af1` 4, `callable:mf12` 4, `callable:mf11` 4, `callable:mf8` 4, `callable:mf6` 3, `callable:answer` 3, `callable:af11` 2, `callable:S8` 2, `callable:af16` 2, `callable:mf15` 2, `callable:af7` 2, `callable:af5` 2, `callable:af6` 2, `callable:S6` 2, `callable:af10` 2, `callable:mf13` 2, `method:query` 2, `method:exec` 2, `callable:value` 2, `callable:package_value` 2, `callable:load_function` 2, `method:eq` 2, `callable:mutate` 2, `callable:state` 2, `callable:worker_job` 2, `callable:ignore_async` 2, `method:sgr` 2, `method:wrap` 2, `callable:mf16` 1, `callable:af21` 1, `callable:af3` 1, `callable:S15` 1, `callable:mf20` 1, `callable:S4` 1, `callable:af15` 1, `callable:mf23` 1, `callable:S14` 1, `callable:mf18` 1, `callable:af4` 1, `callable:mf17` 1, `callable:mf19` 1, `callable:S9` 1, `callable:S16` 1, `callable:wave_owner` 1, `callable:helper` 1, `callable:twice` 1, `callable:use_own` 1, `method:inc` 1, `callable:direct` 1, `callable:public_add` 1, `method:resize` 1, `callable:scale_helper` 1, `method:available` 1, `method:transaction` 1, `callable:inner_value` 1, `callable:two` 1, `callable:win_value` 1, `callable:loader` 1, `callable:child_value` 1, `method:load` 1, `callable:load_lambda` 1, `callable:bare_value` 1, `callable:load_runtime_child` 1, `callable:dynamic_value` 1, `callable:traversal_escape` 1, `callable:symlink_escape` 1, `method:bare` 1, `method:module_handle_path` 1, `method:index_postfix` 1, `method:handle_path` 1, `method:index_compound_prepared` 1, `callable:legacy_eq` 1, `method:call_sibling` 1, `method:receiver_compound` 1, `method:call_pick` 1, `method:invoke_bound` 1, `callable:exported_async` 1, `callable:recur` 1, `method:private_call` 1, `callable:stateful` 1, `method:receiver_compound_prepared` 1, `callable:prepared_indirect` 1, `method:index_dynamic` 1, `method:member` 1, `callable:callable_collision` 1, `callable:prepared_eq` 1, `method:receiver_direct_prepared` 1, `method:variadic` 1, `method:module_value` 1, `method:call` 1, `method:index_direct_prepared` 1, `method:receiver_direct` 1, `method:same_handle` 1, `callable:local_order` 1, `method:compound` 1, `method:index_compound` 1, `method:index_direct` 1, `callable:point` 1, `method:explicit` 1, `method:private_value` 1, `method:start_private_async` 1, `callable:start_private_async` 1, `callable:add_f64` 1, `callable:pair_sum` 1, `callable:add_i64` 1, `callable:add` 1, `method:lock` 1, `method:unlock` 1, `callable:package_double` 1, `callable:val` 1, `callable:made` 1, `callable:mutate_async` 1, `callable:same_name` 1, `callable:parent_counter` 1, `callable:exported_future_timer` 1, `callable:exported_thread_timer` 1, `callable:loading_imported_timer_async_wrapper` 1, `callable:clean_module_target` 1, `callable:clean_loading_async_wrapper` 1, `callable:async_b` 1, `callable:make_a` 1, `callable:make_b` 1, `callable:async_a` 1, `method:blue` 1, `method:reset` 1, `method:white` 1, `method:green` 1, `method:yellow` 1, `method:dim` 1, `method:italic` 1, `method:underline` 1, `method:red` 1, `method:cyan` 1, `method:black` 1, `method:magenta` 1, `method:bold` 1, `method:validate_spec` 1, `method:stringify_document` 1, `method:options` 1, `method:parse_document` 1, `method:write_document` 1, `method:parse_bytes` 1, `method:chars` 1, `method:lines` 1, `method:unified` 1, `method:words` 1, `callable:http` 1, `method:ordinal` 1, `method:plural` 1, `method:integer` 1, `method:prepare` 1, `method:gte` 1, `method:compare` 1, `method:satisfies` 1, `method:valid_range` 1, `method:valid` 1, `method:lt` 1, `method:lte` 1, `method:gt` 1 |
| CP12b | structs | CP12 phase A | `statement:StructDecl` 111, `opaque:struct-body` 1 |
| CP12b | structs (constructor call) | CP12 phase A | `callable:vault` 12, `callable:S4` 5, `callable:S7` 5, `callable:S3` 5, `callable:S2` 4, `callable:S1` 4, `callable:api` 4, `callable:S12` 3, `callable:S6` 3, `callable:S8` 3, `callable:P` 3, `callable:af1` 2, `callable:af7` 2, `callable:S11` 2, `callable:S10` 2, `callable:af3` 2, `callable:af12` 2, `callable:S14` 2, `callable:S5` 2, `callable:pt` 2, `callable:bare` 2, `callable:box` 2, `callable:mdx` 2, `callable:af5` 1, `callable:af15` 1, `callable:af22` 1, `callable:af17` 1, `callable:S9` 1, `callable:af14` 1, `callable:af4` 1, `callable:S15` 1, `callable:S20` 1, `callable:af9` 1, `callable:s` 1, `callable:counter` 1, `callable:p` 1, `callable:vips_lib` 1, `callable:holder` 1, `callable:global_order` 1, `callable:prepared_count` 1, `callable:private_async` 1, `callable:method_api` 1, `callable:module_point` 1, `callable:named` 1, `callable:callable` 1, `callable:Point` 1, `callable:ansi` 1, `callable:assert` 1, `callable:cli` 1, `callable:csv` 1, `callable:curl` 1, `callable:diff` 1, `callable:dotenv` 1, `callable:duration` 1, `callable:fuzzy` 1, `callable:humanize` 1, `callable:id` 1, `callable:ignore` 1, `callable:magick` 1, `callable:jq` 1, `callable:mysql` 1, `callable:pandoc` 1, `callable:postgres` 1, `callable:redis` 1, `callable:semver` 1, `callable:socket` 1, `callable:sqlite` 1, `callable:url` 1, `callable:vips` 1 |
| CP12b | structs (method of a user struct) | CP12 phase A | `method:total` 38, `method:save` 14, `method:close` 14, `method:write` 14, `method:open` 14, `method:push` 3, `method:read` 3, `method:m` 2, `method:helper` 2, `method:inc` 1, `method:call_helper` 1, `method:public_helper` 1, `method:call_step` 1, `method:secret` 1, `method:read_other` 1, `method:increment` 1, `method:set` 1, `method:q` 1, `method:count` 1, `method:array_value` 1, `method:clear` 1, `method:command` 1, `method:fixed` 1, `method:to_global` 1, `method:self` 1, `method:run` 1, `method:hidden` 1, `method:call` 1, `method:error` 1, `method:catch` 1, `method:throw` 1 |
| CP12C | functions as values, closures, lambdas, for | CP12b | `statement:For` 160, `expression:Lambda` 110, `fn-as-value` 92, `call-binding` 72 |
| CP12C | statement and binding forms (const, destructuring, element targets, spread) | CP12b | `assign-target` 140, `compound-target` 135, `incdec-target` 23, `index-assign-target` 15, `prefix-not-precedence` 12, `text-type-name` 11, `index-target` 7, `string-arg-type` 6, `declare-const` 5, `postfix-spelling` 4, `spread` 3, `fn-param` 2, `jump-outside-loop` 1, `declare-pattern` 1, `expression:IncDec` 1 |
| CP13 | errors (undefined callable, error/warn/throw) | CP12 | `callable:error` 6, `callable:nope` 3, `callable:len` 2, `callable:throw` 2, `callable:catch` 2, `callable:try` 2, `callable:err` 2, `callable:warn` 1, `callable:undefined_fn` 1, `method:nope` 1, `callable:exit` 1, `callable:undefined_thing` 1 |
| CP13 | errors, try/catch, type errors with legacy diagnostics, prevalidation | CP12 | `type:compound-assignment` 119, `type:+` 110, `type:>` 98, `type:<` 97, `type:-` 93, `type:member` 88, `statement:Try` 86, `type:length` 71, `type:*` 66, `statement:Throw` 62, `type:%` 59, `static-error:member-assign` 57, `type:>=` 45, `type:<=` 42, `type:index` 37, `type:print` 31, `type:increment` 22, `type:push` 18, `static-error:arity` 16, `type:unary` 10, `type:/` 6, `static-error:redeclared` 5, `type:element-assignment` 2, `parse-error` 1, `type:script-return` 1 |
| CP13 | undefined name (legacy run-time error) | CP12 | `unresolved-name` 144, `assign-free-name` 14, `free-name:undeclared` 3, `free-name:not-one-top-level-binding` 1, `assign-free-name:undeclared` 1 |
| CP14 | built-ins | CP12 | `method:stringify` 200, `method:split` 103, `method:join` 97, `method:contains` 95, `method:to_upper` 88, `method:starts_with` 79, `method:substr` 23, `method:replace` 21, `method:to_string` 18, `method:get` 10, `callable:type` 10, `method:to_int` 10, `method:keys` 5, `callable:min` 3, `method:trim` 2, `method:has` 1, `callable:range` 1, `method:omit` 1, `method:pick` 1, `method:empty` 1, `method:values` 1, `method:sum` 1, `callable:is_null` 1, `method:ends_with` 1 |
| CP14 | push as an expression (records20k, integration owner) | CP12 | `method:push` 26 |
| CP14B | callback-taking collection methods | CP14, CP12C | `method:sort_by` 21, `method:sort` 14, `method:map` 13, `method:group_by` 2, `method:filter` 1 |
| CP15 | built-ins | CP14 | `callable:bytes` 14, `method:decode` 11, `method:encode` 5, `callable:sha256` 4, `callable:copy` 3, `callable:deepcopy` 2, `method:bytes` 1 |
| CP15B | remaining pure built-ins and methods | CP15 | `callable:map` 22, `method:set` 21, `callable:set` 12, `method:add` 12, `method:clear` 1, `callable:same` 1, `method:pop` 1, `callable:stack` 1 |
| CP15B | string and number literal spellings, interpolation | CP15 | `interpolation` 110, `string-literal` 99, `number-literal` 9 |
| CP16 | host ops (files, environment, time) | CP14, CP03, CP07 | `callable:is_dir` 98, `callable:getenv` 78, `callable:file` 58, `callable:exists` 56, `callable:open` 55, `callable:ls` 55, `callable:is_file` 51, `method:open` 46, `method:close` 44, `method:write` 43, `method:save` 42, `callable:rm` 22, `callable:read` 11, `method:run` 10, `callable:timer` 10, `callable:run` 8, `callable:epoch` 6, `method:cwd` 6, `callable:cat` 5, `callable:sha256_file` 5, `method:elapsed` 4, `callable:sleep` 4, `callable:make_dir` 4, `callable:write` 4, `callable:mkdir` 3, `method:read` 3, `callable:open_bytes` 3, `callable:os` 3, `callable:env` 2, `callable:cp` 2, `callable:mv` 2, `callable:stat` 2, `callable:pwd` 2, `method:status` 2, `callable:platform` 2, `method:input` 2, `callable:touch` 1, `callable:project_root` 1, `callable:status` 1, `callable:setenv` 1, `callable:hardware_concurrency` 1, `callable:remove` 1, `callable:move` 1, `method:read_all_bytes` 1, `callable:ifstream` 1, `callable:which` 1, `method:remove` 1 |
| CP16/CP20 | processes and pipelines | CP16; pipelines CP20 (after CP19) | `callable:cmd` 10, `method:pipe` 1 |
| CP16/CP24 | legacy script/template bindings (args, env, page) | CP16 (script bindings), CP24 (template bindings) | `unresolved-name:legacy-binding` 26 |
| CP18 | FFI | CP13 | `callable:ffi_call` 5, `callable:ffi_open` 5, `callable:ffi_close` 1, `callable:ffi_callback` 1, `callable:ffi_buffer` 1, `callable:ffi_bytes` 1 |
| CP19 | shell | CP12 | `statement:Command` 4 |
| CP22 | await | CP12 | `expression:Await` 86 |
| CP22/CP23 | concurrency (threads, mutexes, atomics) | CP22 (threads, scheduler); CP23 (mutexes, atomics) | `callable:thread` 100, `callable:mutex` 9, `method:lock` 7, `method:unlock` 6, `method:done` 2, `callable:atomic<bool>` 2, `method:load` 2, `callable:atomic<int>` 2, `method:start` 2, `method:try_lock` 1, `method:locked` 1, `method:stop` 1, `method:pause` 1, `method:paused` 1, `method:resume` 1, `method:await` 1, `method:store` 1, `method:fetch_add` 1 |
| CP22/CP24 | fn[async] (CP22), @fn deferred bodies (CP24) | CP22 (async); CP24 (@fn) | `fn-flags` 122, `callable:tiny` 6, `callable:af1` 5, `callable:af13` 3, `callable:af6` 3, `callable:bad` 3, `callable:af8` 2, `callable:af12` 2, `callable:af4` 2, `callable:af3` 2, `callable:af9` 2, `callable:af10` 2, `callable:af5` 2, `callable:outer` 2, `callable:pack` 2, `callable:id` 2, `callable:af14` 1, `callable:af19` 1, `callable:af7` 1, `callable:af11` 1, `callable:af2` 1, `callable:probe` 1, `callable:later` 1, `callable:recur` 1, `callable:_leading` 1, `callable:mk` 1, `callable:mko` 1, `callable:add` 1, `callable:inc_async` 1, `callable:add_async` 1, `callable:init_async_job` 1, `callable:future` 1, `callable:measure` 1, `callable:make_timer` 1, `callable:ignore_async` 1, `callable:work` 1, `callable:good` 1, `callable:held_job` 1, `callable:async_job` 1, `callable:nested_one` 1 |
| CP24 | template functions | CP16, CP02 | `callable:inject` 24, `callable:minify` 4, `callable:tracked` 2, `callable:build` 2, `callable:track` 1, `callable:untrack` 1, `method:html` 1 |
| CP24 | template-only constructs | CP16, CP02 | `expression-statement` 136, `opaque:template-text` 8, `statement:TemplateStmt` 4 |
| language-cleanup | D-018 loop-evaluator differences (L1-L4) | after CP16–CP17 (roadmap); lifts D-018 refusals once legacy is not the oracle | `prepared-unpreparable` 279, `prepared-decl-type` 160, `prepared-fn-unprepared` 77, `context:assignment-error` 58, `context:assignment-type` 4, `context:string-append-type` 3, `context:negative-zero` 2 |
| language-cleanup | L25 lexical scoping (refused; migration diagnostics) | after CP16–CP17 (roadmap); lifts D-018 refusals once legacy is not the oracle | `free-name:dynamic-caller` 54 |
| language-cleanup | L6 names legacy reserves | after CP16–CP17 (roadmap); lifts D-018 refusals once legacy is not the oracle | `binding-name-reserved` 26, `fn-name-reserved` 15, `param-name-reserved` 13 |
| language-cleanup | L7 aliasing / location references (records20k aliasing: CP14 integration) | after CP16–CP17 (roadmap); lifts D-018 refusals once legacy is not the oracle | `aliasing` 56, `path-in-call-argument` 12, `alias-argument` 1 |
| language-cleanup | legacy parse/run-time quirks (D-014, D-022) | after CP16–CP17 (roadmap); lifts D-018 refusals once legacy is not the oracle | `opaque:irregular-legacy-parse` 194, `opaque:legacy-runtime-error` 20, `shebang-statement` 2, `jump-not-last` 1 |

Unowned reasons: none.

Notes. `type:*` (an operand kind legacy rejects may occur) is owned by CP13:
the VM can compile these once it reports legacy's type errors itself.
`free-name:dynamic-caller` and `-shadow` stay refused by design (L25,
decision 3); the two curated scripts become compatibility fixtures.
`postfix-receiver:member` (a member read on a call result or literal;
introduced with the chained-postfix fix) belongs to language cleanup (L43).

## Combinations (owner request)

A script counts when every certain reason belongs to a landed feature;
after the slash, consequential reasons count too. B1 is landed in every row.
Chained = `postfix-receiver`; modules = `import`/`export`, the imported
names and module members of importing scripts; functions as values = CP12C's
callables, closures, lambdas and `for`; CP13–CP16 = the owners CP13, CP14,
CP15, CP16 and CP16/CP24 in the table above.

| Landed (B1 always) | curated 589 | projects 86 | shared 789 |
| --- | ---: | ---: | ---: |
| today (B1 landed) | 104 / 104 | 0 / 0 | 104 / 104 |
| chained | 104 / 104 | 0 / 0 | 104 / 104 |
| modules | 154 / 150 | 0 / 0 | 154 / 150 |
| functions as values | 117 / 109 | 0 / 0 | 118 / 109 |
| chained + modules | 154 / 150 | 0 / 0 | 154 / 150 |
| chained + functions as values | 117 / 109 | 0 / 0 | 118 / 109 |
| modules + functions as values | 168 / 157 | 0 / 0 | 169 / 157 |
| all three | 168 / 157 | 0 / 0 | 169 / 157 |
| all three + CP13–CP16 | 262 / 227 | 10 / 9 | 285 / 231 |
| modules + functions as values + CP13–CP16 (no chained) | 252 / 209 | 5 / 2 | 262 / 209 |
| CP13–CP16 only | 173 / 148 | 3 / 0 | 174 / 148 |

Revised from the first report (241 / 6 / 272 for "all three + CP13–CP16"):
module-member calls in importing project scripts now count as modules (+9
curated, +4 projects) and `type:*` as CP13 (+10 curated). Chained postfix
alone unlocks nothing (every chain also calls a method nobody supports yet);
on top of the others it adds +10 curated, +5 projects, +23 shared. The
chained figures assume all chains become compilable; member reads on call
results stay refused (L43), see `cp12b-resolver.md`.

## Residual after all three + CP13–CP16

Curated: 262 of 589 on the VM, 327 residual (one has a parse error).
Projects: 10 of 86, 76 residual. Features are owner | scope from the table
above; "sole" = residual scripts that feature alone would unlock.

```
== curated: 589; all three + CP13–CP16: 262; residual 327 (incomplete 1)
   distinct remaining features per residual script (4=4+): {1: 253, 2: 53, 3: 13, 4: 7}
   by sole blocker (blocked / sole):
      34 sole /  58  CP22/CP24 | fn[async] (CP22), @fn deferred bodies (CP24)
      30 sole /  38  CP14B | callback-taking collection methods
      28 sole /  44  CP22/CP23 | concurrency (threads, mutexes, atomics)
      23 sole /  30  CP24 | template functions
      19 sole /  29  language-cleanup | L7 aliasing / location references (records20k aliasing: CP14 integration)
      18 sole /  32  CP15B | remaining pure built-ins and methods
      15 sole /  37  CP12b | structs
      15 sole /  20  CP15B | string and number literal spellings, interpolation
      12 sole /  16  language-cleanup | legacy parse/run-time quirks (D-014, D-022)
      11 sole /  23  CP12C | statement and binding forms (const, destructuring, element targets, spread)
      11 sole /  17  language-cleanup | L6 names legacy reserves
      11 sole /  15  language-cleanup | D-018 loop-evaluator differences (L1-L4)
       7 sole /  10  CP24 | template-only constructs
       6 sole /   9  CP12b | globals
       4 sole /   5  CP18 | FFI
       3 sole /  23  CP22 | await
       3 sole /   4  CP19 | shell
       2 sole /   4  CP16/CP20 | processes and pipelines
       1 sole /   2  language-cleanup | L25 lexical scoping (refused; migration diagnostics)
       0 sole /  18  CP12b | structs (method of a user struct)
   greedy from all three + CP13–CP16:
     + 34 -> 296 (50.3 %)  CP22/CP24 | fn[async] (CP22), @fn deferred bodies (CP24)
     + 31 -> 327 (55.5 %)  CP14B | callback-taking collection methods
     + 29 -> 356 (60.4 %)  CP22/CP23 | concurrency (threads, mutexes, atomics)
     + 29 -> 385 (65.4 %)  CP24 | template functions
     + 21 -> 406 (68.9 %)  CP15B | remaining pure built-ins and methods
     + 24 -> 430 (73.0 %)  language-cleanup | L7 aliasing / location references (records20k aliasing: CP14 integration)
     + 20 -> 450 (76.4 %)  CP22 | await
     + 19 -> 469 (79.6 %)  CP12C | statement and binding forms (const, destructuring, element targets, spread)
     + 17 -> 486 (82.5 %)  CP12b | structs
     + 16 -> 502 (85.2 %)  CP15B | string and number literal spellings, interpolation
     + 14 -> 516 (87.6 %)  CP12b | structs (method of a user struct)
     + 14 -> 530 (90.0 %)  language-cleanup | legacy parse/run-time quirks (D-014, D-022)
     + 14 -> 544 (92.4 %)  language-cleanup | L6 names legacy reserves
     + 12 -> 556 (94.4 %)  language-cleanup | D-018 loop-evaluator differences (L1-L4)
     +  9 -> 565 (95.9 %)  CP24 | template-only constructs
     +  8 -> 573 (97.3 %)  CP12b | globals
     +  5 -> 578 (98.1 %)  CP18 | FFI
     +  4 -> 582 (98.8 %)  CP16/CP20 | processes and pipelines
     +  4 -> 586 (99.5 %)  CP19 | shell
     +  2 -> 588 (99.8 %)  language-cleanup | L25 lexical scoping (refused; migration diagnostics)
== projects: 86; all three + CP13–CP16: 10; residual 76 (incomplete 0)
   distinct remaining features per residual script (4=4+): {1: 45, 2: 14, 3: 10, 4: 7}
   by sole blocker (blocked / sole):
      16 sole /  26  CP12b | structs
      10 sole /  18  language-cleanup | L7 aliasing / location references (records20k aliasing: CP14 integration)
      10 sole /  17  language-cleanup | L6 names legacy reserves
       4 sole /   6  CP16/CP20 | processes and pipelines
       1 sole /   9  CP15B | string and number literal spellings, interpolation
       1 sole /   7  CP24 | template-only constructs
       1 sole /   3  CP22/CP23 | concurrency (threads, mutexes, atomics)
       1 sole /   1  CP18 | FFI
       1 sole /   1  CP24 | template functions
       0 sole /  12  language-cleanup | D-018 loop-evaluator differences (L1-L4)
       0 sole /   9  language-cleanup | legacy parse/run-time quirks (D-014, D-022)
       0 sole /   8  CP22/CP24 | fn[async] (CP22), @fn deferred bodies (CP24)
       0 sole /   7  CP22 | await
       0 sole /   6  CP15B | remaining pure built-ins and methods
       0 sole /   3  CP12C | statement and binding forms (const, destructuring, element targets, spread)
       0 sole /   2  CP12b | globals
   greedy from all three + CP13–CP16:
     + 16 -> 26 (30.2 %)  CP12b | structs
     + 10 -> 36 (41.9 %)  language-cleanup | L7 aliasing / location references (records20k aliasing: CP14 integration)
     + 10 -> 46 (53.5 %)  language-cleanup | L6 names legacy reserves
     +  5 -> 51 (59.3 %)  language-cleanup | D-018 loop-evaluator differences (L1-L4)
     +  5 -> 56 (65.1 %)  CP16/CP20 | processes and pipelines
     +  3 -> 59 (68.6 %)  CP15B | string and number literal spellings, interpolation
     +  4 -> 63 (73.3 %)  language-cleanup | legacy parse/run-time quirks (D-014, D-022)
     +  5 -> 68 (79.1 %)  CP24 | template-only constructs
     +  4 -> 72 (83.7 %)  CP15B | remaining pure built-ins and methods
     +  1 -> 73 (84.9 %)  CP12C | statement and binding forms (const, destructuring, element targets, spread)
     +  2 -> 75 (87.2 %)  CP12b | globals
     +  1 -> 76 (88.4 %)  CP22/CP23 | concurrency (threads, mutexes, atomics)
     +  1 -> 77 (89.5 %)  CP22/CP24 | fn[async] (CP22), @fn deferred bodies (CP24)
     +  7 -> 84 (97.7 %)  CP22 | await
     +  1 -> 85 (98.8 %)  CP18 | FFI
     +  1 -> 86 (100.0 %)  CP24 | template functions
   projects greedy from today (B1 only), all features:
     +  7 -> 7 (8.1 %)  language-cleanup | L6 names legacy reserves
     +  3 -> 10 (11.6 %)  CP13–CP16 (as scheduled)
     +  5 -> 15 (17.4 %)  CP12b | modules
     + 14 -> 29 (33.7 %)  CP12b | structs
     +  7 -> 36 (41.9 %)  CP12b | chained postfix
     +  8 -> 44 (51.2 %)  language-cleanup | L7 aliasing / location references (records20k aliasing: CP14 integration)
     +  5 -> 49 (57.0 %)  CP16/CP20 | processes and pipelines
     +  3 -> 52 (60.5 %)  CP15B | remaining pure built-ins and methods
     +  2 -> 54 (62.8 %)  CP12C | functions as values, closures, lambdas, for
     +  5 -> 59 (68.6 %)  language-cleanup | D-018 loop-evaluator differences (L1-L4)
     +  4 -> 63 (73.3 %)  language-cleanup | legacy parse/run-time quirks (D-014, D-022)
     +  4 -> 67 (77.9 %)  CP15B | string and number literal spellings, interpolation
     +  5 -> 72 (83.7 %)  CP24 | template-only constructs
     +  1 -> 73 (84.9 %)  CP12C | statement and binding forms (const, destructuring, element targets, spread)
     +  2 -> 75 (87.2 %)  CP12b | globals
     +  1 -> 76 (88.4 %)  CP22/CP23 | concurrency (threads, mutexes, atomics)
     +  1 -> 77 (89.5 %)  CP22/CP24 | fn[async] (CP22), @fn deferred bodies (CP24)
     +  7 -> 84 (97.7 %)  CP22 | await
     +  1 -> 85 (98.8 %)  CP18 | FFI
     +  1 -> 86 (100.0 %)  CP24 | template functions
```

For real projects the path to 50 % is structs (CP12b) and two language
cleanup items — L7 aliasing and L6 reserved names (D-009#3, already
approved) — before any further built-in work; 100 % needs CP22 `await` and
async, CP16/CP20 processes and the template constructs as well.
