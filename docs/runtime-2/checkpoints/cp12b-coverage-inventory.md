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
