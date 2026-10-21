# CP12 phase B — modules: legacy semantics (probed) and the VM design

Status: semantics probed; **decided by D-041** (initialise once per execution
context, shared instance with an explicit lifecycle) and **D-046** (exports
are bindings — shared access to the module's storage location, as nift.dev
documents and legacy does; D-041 #4's copy rule is reversed; module
functions resolve free names lexically, D-038). VM side in progress: module graph and loader (`src/rt/module_graph.*`,
`src/rt/module_loader.*`, below); the D-041 fixtures
(`tests/runtime2/modules-d041`, below). The legacy side waits for
feature-host-deps to integrate (coordinator, 2026-10-20). Probes:
`tests/runtime2/semantics-modules/<probe>/` (program in `p/`, expected
stdout/stderr/exit recorded from legacy, D-015; each probe is also an
rt-diff workspace), run by `tests/runtime2/semantics_modules.py --nift
./nift` (25 probes, legacy: PASS). Legacy source:
`Parser::execute_import_file`, `src/ParserScript.cpp:468-700`.

## Legacy semantics (verified by the probes)

| Area | Legacy behaviour | Probe(s) |
| --- | --- | --- |
| Form | `import("path")` / `@import("path")` statement; the argument is an expression evaluated at run time (`import(p)` with `p := "m.nft"` works) | `dynamic_path`, `template_import` |
| Path resolution | a name with no `/`, `\` and no extension is a **package** (`.nift/packages/<name>`, manifest entry, lock and revision checks); otherwise `\` → `/`, a relative path is resolved against the **importing module's directory** (`import_base`), or the entry script's directory; then absolute + lexically normalised; the project-root confinement applies when enforced (builds) | `nested_relative`, `package_missing` |
| Identity | `stable_import_identity(path)` (canonical file identity); used **only** for cycle detection | `same_file_two_spellings`, `self_cycle` |
| Caching | **none**: every `import` executes the module again in a fresh environment; a diamond runs the shared module twice (`c runs` twice) | `diamond`, `repeated` |
| Execution | the module runs immediately, at the import statement, in isolated scopes (no access to the importer's bindings); its output appears in program order | `side_effect_order`, `module_free_unbound` |
| Exports | `export(name)` lists a binding, function or struct of the module's top level; after the module finished, exactly the listed names are installed in the importer: variables into the importer's **current innermost scope** (block-scoped: gone after the block), functions and structs into the importer's **global** function/struct tables; an importer re-exports names it imported (`export(cf)` in `b`) | `diamond`, `not_reexported`, `in_block`, `struct_export` |
| Values | `export(name)` exports the **binding**: the importer's name is the module's storage location, so the importer's writes (`n += 10`, `xs.push(9)`) change the module's state and the module's functions' later writes are seen by the importer — as nift.dev documents ("exports the actual binding, not a value snapshot") and as decided by D-046 #2 (the earlier "copied" reading here was wrong; the `export_copy` probe does not distinguish) | `export_copy`, `modules-d041/stateful_*_binding` |
| Module lexical environment | exported functions resolve the module's own top-level bindings and private functions through the module environment as it is **at the end of the module** (a binding declared after `export` is visible). A name the module does not bind is looked up in the **caller's top-level bindings** (a caller global `zz := 5` is seen) but not in the caller's function locals (an error even if the calling function has it). So modules are **not** fully lexically scoped (correction, D-045 review below; the earlier reading missed the caller-global case); nift.dev documents "named fn declarations resolve free names in the calling scope" | `module_private`, `module_private_late`, `module_vs_caller_local`, `module_free_unbound`; D-045 review |
| Collisions | importing a name that already exists in the importer (binding in any active scope, function, struct) is an error, `export collides with existing binding: <name>`, after the module ran (its side effects happened); this makes a repeated import of the same module an error | `export_collision`, `repeated`, `same_file_two_spellings`, `in_function` (second call) |
| Import in a function | runs at each call; the exports go to the global tables, so the second call collides | `in_function` |
| Errors | missing file: `script is not readable: <abs path>` at the import statement (recoverable diagnostic `io.import_source_unreadable`); cycle: `script import cycle through <abs path>` reported inside the innermost module with one `import: ` prefix per active import; an error inside a module is reported at the module's position with an `import: ` prefix per level; `export(x)` naming nothing: `export names no existing binding: x` at the export; `return <value>` in a module: `return with a value is not allowed in import` at the import (bare `return` ends the module normally); missing package: `package is not installed: <name>` | `missing`, `cycle`, `self_cycle`, `module_error`, `export_missing`, `module_return`, `module_bare_return`, `package_missing` |
| Rollback | a failed import rolls back every resource it created (modules, lambdas, structs, handles, files) before the error propagates | source |
| Dependencies | a successfully read module is added to the build's dependencies (`result_.dependencies`); a missing module is not added by the import itself (the failure fails the page) — relevant to templates (CP24), not to standalone scripts | source |

Quirks for the register (not fixed here): exports land in the innermost
scope for variables but the global tables for functions (L44); every import
re-runs the module and repeated imports collide (L45, resolved by D-041); the
`import: ` prefix repeats per level (L46); exported variables are live
bindings (L50: documented and decided, D-046 #2 — not an inconsistency).

## VM design (proposed; owner decision 2)

* **Static imports only.** A unit is compiled with its modules when every
  `import` argument is a string literal (no interpolation); a dynamic path
  (`dynamic_path`) is refused before execution (`import-dynamic`). Packages
  (no `/`, no extension) are refused at first (`import-package`) until the
  package store is read through the host interface.
* **Resolution at compile time**, with legacy's rules above, relative to the
  importing module's directory; canonical identity for cycles and for the
  compilation cache. All file access goes through one function
  (`load_module_source(path)`, new file `src/rt/module_loader.*`), to be
  swapped for CP16's host read when it lands. A missing file, a cycle, a
  non-literal path: the unit is refused before execution (legacy then reports
  its own error) — no partial execution.
* **Compile once, initialise once per context (D-041).** Each distinct
  module (by canonical identity) is parsed and compiled once per program
  graph; the first `import` in a context initialises the instance
  (uninitialised → initialising → initialised | failed); later imports bind
  the same instance's exports; importing a module that is `initialising` is
  the cycle error; one that `failed` reports the same failure without running
  it again. Compilation caching never changes initialisation.
  *Superseded draft:* initialise per import (legacy's re-run).
* **Exports** become bindings at the import site: functions bound statically
  (the importer's function table gets the module's protos, closed over the
  instance's module environment); variables bound in the importer's current
  scope **to the module's storage location** (D-046 #2; values assigned into
  the location follow D-040). Binding the same declaration again (a repeated
  import, two spellings, a re-export of it) is a rebind, not an error; a
  clash with a *different* binding is refused statically (`import-collision`:
  legacy fails after running the module).
* **Module environment**: a module's top-level bindings live in that
  initialisation's environment (heap object); its functions read them as
  module globals (the H1 mechanism, with the environment instead of main's
  frame), resolved lexically — consistent with legacy (`module_vs_caller_local`)
  and with L25.
* **Errors and diagnostics**: VM run-time errors inside a module carry
  legacy's `import: ` prefixes by import depth (the proto knows its depth);
  `export` of a missing name and `return <value>` are static refusals.
* **Dependencies**: CP24 records the module paths (and absent modules, where
  legacy's host records absence) through the same loader; standalone scripts
  have none.

Out of scope for the first step: structs exported from modules (needs B4),
imports inside functions (refused: the second call collides), packages.

## Initialisation decision: data and options (for D-037)

### (a) Who imports a module more than once

`tools/rt-vm/cp12b_imports.py` expands each program's import graph statically
the way legacy executes it (every `import` re-runs the module) and reports
modules initialised more than once, their top-level effects (output, host
operations, module state mutated by its functions, exported variables) and
whether initialising once per program would change behaviour. Nothing executes.

| Corpus | Programs | With imports | Module initialised ≥ 2 times | Behaviour would change under "once" |
| --- | ---: | ---: | ---: | ---: |
| shared corpora on disk (vm-corpus, semantics, rt-diff-corpus, conformance, probes) | all `.f`/`.nft` | 3 | 0 | 0 |
| scripts extracted from the smoke tests (as rt-diff extracts them) | 81 with imports | 81 | 0 | 0 |
| rt-diff generator (seed 1, 200) | 200 | 61 | 0 | 0 |
| CP13 project units (86) | 86 | 13 (each imports one module or package) | 0 | 0 |
| project checkouts: every page/template/script file with an import (38 repositories) | 2,100+ files | — | 0 in executed programs | 0 |

The only double imports found are **documentation examples**, not executed
programs: the package READMEs (assert, id, ignore, imagemagick, mdx, mysql,
postgres, redis, vips) show `@import("pkg")` in two separate examples,
nift-dev.github.io `docs/lambdas-closures.html` shows `counter.f` twice in
prose, and JavaScript `import(...)` samples in tanstack/deno/capgo markdown
match the pattern. Our own probes (`diamond`, `repeated`,
`same_file_two_spellings`, `in_function`) are deliberate. Imports inside
functions or blocks occur only in the import-machinery smoke tests (12
scripts: try/catch rollback, workers); none of them imports a module twice.
Dynamic (non-literal) imports: 6 smoke scripts (the import-syntax tests) and
markdown JavaScript samples.

So: **no project or corpus program initialises a module more than once**;
"once per context" changes no observed output today. Modules that would be
affected if it happened are the stateful package modules (`id`, `assert`,
`mysql`, `postgres`, `redis`, `vips`, `imagemagick` keep module-level state
that their functions mutate).

### (b) Options

| | A: once per execution context, shared instance | B: legacy re-run on every import | C: once per context, refusing programs where A and B differ |
| --- | --- | --- | --- |
| Semantics | first import in a context initialises; later imports rebind the same exports (a repeated import of the same module is not an error; a name clash with a *different* binding still is) | every import runs the module again in a fresh environment; a repeated import collides after running | VM implements A; a unit in which any module would initialise twice (static graph: diamond, repeated or nested import of a module with exports or effects) is refused before execution (`import-reinitialised`) |
| VM effort | M: module instance table per context, init proto run on first import, export binding from the instance | M: init proto per import site, fresh instance each time, collision check | M (A + the graph check, which the loader needs anyway) |
| Legacy change (D-018) | M: cache module instances per context in `execute_import_file`, re-install exports from the cache; diamond output and collision behaviour change in **both** engines with old/new tests | none | none now; the behaviour difference is excluded instead of changed |
| D-040 (values) | exported variables are copied into the importer (value semantics) — a later mutation inside the module is not seen by importers' copies; exported functions share the instance's private state (closure over the module environment) | as legacy: copies per import, separate state per import | as A |
| D-038 (lexical) | module functions resolve module names lexically (legacy does already, probes `module_vs_caller_local`, `module_free_unbound`) | same | same |
| Cost when it matters | shared mutable module state across importers (e.g. `id.f`'s counter) — correct for "module = singleton" designs | re-execution cost per import; diamonds duplicate output; repeated imports fail | none: affected programs (0 today) run on legacy |

*Decided by D-041: A.* Option C survives only as a temporary switch (below).
Original recommendation: **C now, A as the target**. C is A's implementation with the
cases where A and B differ refused before execution (D-015: refuse rather
than reproduce or silently change); it needs no legacy change and no
compatibility machinery, and it costs no coverage on any corpus. When the
owner confirms A, the refusal is lifted and legacy gets the same per-context
instance cache under D-018 with old/new tests (`diamond`, `repeated`,
`in_function`).

### Context boundaries (aligned with CP17's context kinds)

Three separate things: the **compiled-module cache** (immutable; keyed by
canonical path, content hash and compiler version; shareable across the whole
process and across isolates), the **module instance** (initialisation and
mutable state), and the **context that owns the instance**:

| Context (CP17 kind) | Module instances |
| --- | --- |
| `Script` (`nift script.f`, `nift eval`, shell runs) | one set per run; nested imports share them; threads/async of the run share them (CP22 rules apply to shared state) |
| `PageRender` | one set **per page render**, never inherited from the enclosing build: a page's output must not depend on which pages rendered before it (incremental builds and parallel rendering) |
| `Build` | owns no instances of its own for pages; it owns the compiled cache for its duration and the dependency records |
| `BuildHook` | one set per hook execution |
| `AutoRebuild` | each pass opens a new `Build`; instances never survive a pass; the compiled cache survives, invalidated by content hash |
| isolates (CP23) | one set per isolate; compiled modules shared read-only |

A page render inside a build therefore gets fresh instances from its own
`PageRender` context; build-wide shared state, if ever wanted, has to be an
explicit build-level facility, not module state. Dependencies (module paths,
and absent modules where the host records absence) are recorded per
`PageRender`/`BuildHook`, so each page rebuilds exactly when one of its
modules changes.

## Implementation (VM side, D-041)

`src/rt/module_loader.{h,cpp}` — the only module file access in `src/rt`
(`load_module_source`, legacy's "script is not readable" wording; to be
replaced by CP16's host read) and `module_identity` (legacy's
`stable_import_identity`: weakly canonical, case-folded on Windows).

`src/rt/module_graph.{h,cpp}` — `build_module_graph(entry ast, root,
entry path, options)`, run before anything executes:

* resolution with legacy's rules (relative to the importing module's
  directory, the entry script's for the unit, the current directory without
  one; `\` read as `/`; absolute and lexically normalised; optional project
  root confinement for builds: `import-outside-project`);
* each module read and parsed once per program by identity
  (`Module::legacy_initialisations` counts how often today's legacy would run
  it — the migration fixture data);
* `dependencies` (every module read, first-read order) and `absent` (looked
  for, unreadable) for CP24 / the page dependency record (to hook into
  feature-host-deps' record once it integrates, not a parallel mechanism);
* explicit refusals before execution: `import-dynamic`, `import-package`,
  `import-nested` (imports in functions, blocks, `try`), `import-missing`,
  `import-cycle`, `import-parse-error`, `import-opaque:<class>`,
  `import-collision` (a different binding; the same declaration again is a
  rebind, tracked per export origin through re-exports), `export-missing`
  (no earlier top-level binding, function or import of that name),
  `export-duplicate`, `export-nested`, `export-struct` (structs: B4),
  `module-return`, `import-limit`.

Unit tests (hermetic: every graph is planned from a copy in a fresh
temporary root; absent targets are checked absent first; the loader's
not-found regression covers a missing file, a directory, a dangling symlink,
a FIFO and an unreadable file): `tests/rt/vm_modules_unit.cpp` plans every legacy probe
(`semantics-modules`, 25) under both policies, the D-041 script fixtures,
resolution details (relative chains, two spellings, absent files, project
root) and rebinding.

### Compilation: source-level linking (2026-10-21)

Implemented (`src/rt/module_link.{h,cpp}`, `script_runner.cpp`). A unit with
static imports compiles as its **linked text**: every `import(...)`
statement is replaced by the imported module's own linked text and every
module `export(...)` is blanked, so the module initialises in place, once,
at its import, and every analysis (globals, kinds, D-018 contexts, aliasing,
the records20k lift) spans modules with no cross-unit machinery. This is
exact under the following refusals (legacy then runs the unit):

| Refusal | Why it is needed |
| --- | --- |
| the module graph's (`import-dynamic`, `-package`, `-nested`, `-missing`, `-cycle`, `-parse-error`, `-opaque`, `export-*`, `import-collision`) | static, literal, top-level imports only |
| `import-reinitialised` (InitPolicy::RefuseReinitialisation) | each module imported once: legacy runs it once too (D-041 #2 removal item) |
| `module-return` (any top-level `return` in a module) | legacy ends only the module |
| `import-name-clash` | names declared outside functions and function names are disjoint across units, so one linked unit names each binding once |
| `import-scope` | a unit names another unit's binding or function only where it imports it (legacy isolates module scopes; module functions resolve names lexically, D-046 #4) |
| `path-in-call-argument:imported` | records20k: a user-call argument path rooted at a module's binding |

Exported bindings are the module's bindings themselves (D-046 #2). A refusal
inside a module is located in that module's file. Run-time errors map back
to the module file and position; while a module initialises, legacy
prefixes `import: ` once per active import (the VM reports where main's
active call is, `VmResult::main_offset`, `vm_frames.cpp`); legacy's
`return: ` prefixes stay under D-018c (vm_policies.py checks the reported
module file and compares the `import: ` prefixes).

Evidence (development, not certification): `tests/runtime2/vm_modules.py`
(in test-rt-vm): 60 pinned programs (shapes in every context, the 25 legacy
probes, the D-041 script fixtures that agree with today's legacy) and
random import trees (`--gen`; seeds 1-3 x 300 all on the VM and identical
to legacy). Imports inside `try` stay refused (`import-nested`, D-046 M4).

### Requirement from records20k: path arguments rooted at imported bindings

When CP12B compiles imports, stage 4's path-argument lift (a call argument
that is a path such as `o.k`, lifted to a location) must **refuse paths whose
root is an imported binding**. Under D-046 #2 an exported name is the
module's storage location, so `f(o.k)` with `s.push(9)` in `f` writes into
the module's state, and the module's own `setk()` can replace `o` between
the import and the call. Probe (scratchpad `integ/mod/`, records20k):

```
// m.f
o := {"k": 1}
fn(setk()) {
  o = {"k": [1]}
  return 0
}
export(o)
export(setk)
// p2.f
fn(f(s)) {
  s.push(9)
  return 0
}
import("m.f")
setk()
f(o.k)
print(o.k.stringify())    // legacy: [1,9]
```

Legacy (runtime-2 @217ac21b): `p1` (`f(o.k)` read-only) prints `1`, `p2`
prints `[1,9]`, `p3` (`print(o.k)`) prints `1`. The refusal reason will be
`path-in-call-argument:imported` until a module-location model lifts it; the
probes are fixtures `import_path_read`, `import_path_write`,
`import_path_print` (modules-d041; legacy agrees with all three).

### Temporary scaffolding: `import-reinitialised` (removal item)

`InitPolicy` is the single initialisation switch. The default is
`OncePerContext` (D-041). `RefuseReinitialisation` refuses units where
today's legacy would initialise a module again (`import-reinitialised`); it
exists only in case the VM's initialise-once lands before legacy's (D-041
#2) and is never part of the module design.
**Removal condition: legacy and the VM both implement and verify
initialise-once (the D-041 fixtures pass on both engines). It must not
survive into CP25.** Tracked in the delivery forecast's scaffolding list.

## D-041 fixtures (independently specified)

`tests/runtime2/modules-d041/<fixture>/` — a program or project in `p/` and
`spec.json` with the expected behaviour **written from D-041, not recorded
from an engine**; run by `tests/runtime2/modules_d041.py --nift <binary>`.
`--legacy-today` checks that the fixtures marked `agrees` pass on today's
legacy and those marked `differs` (with the register entry) still fail, so
the specification stays honest until legacy implements initialise-once.
Today: legacy-today PASS (22 fixtures: 11 agree, 11 differ as recorded).

| Fixture | D-041 expectation | Today's legacy |
| --- | --- | --- |
| `repeated` | one initialisation; the second import rebinds (`m runs`, `2`, `[1]`) | runs twice, collision (L45) |
| `repeated_two_spellings` | `m.nft` and `./sub/../m.nft` are one module | runs twice, collision (L45) |
| `export_clash` | a different binding of the same name: collision error after the module ran | agrees |
| `diamond` | the shared module runs once | twice (L45) |
| `diamond_stateful` | state shared through two importers (1, 2, 3) | separate instances (1, 1, 2) |
| `nested_chain` | relative paths per importer; names reach main only through re-export | agrees |
| `nested_shared` | direct and indirect imports share one instance | separate (L45) |
| `circular`, `self_cycle` | explicit cycle error, nothing runs twice | agrees |
| `stateful_export_binding` | exported `n` is the module's location: `2`, `12`, then the module's counter continues `13` (D-046 #2) | agrees |
| `stateful_array_binding` | exported array is the module's location (`[1,2]`, `[1,2,9]`, `[1,2,9]`) | agrees |
| `repeated_binding` | a repeated import keeps the same locations: state before it survives (`1`, `1`, `7`) | runs again, collision (L45) |
| `reexport_binding` | a re-export is the original location: reaching `n` directly and through `b` rebinds (`1`, `1`) | runs `c` twice, collision |
| `module_fn_lexical` | a module function does not see a caller-only name (`zz`): error (D-046 #4, D-038) | prints `5` (caller's global) |
| `failed_init` | a failed module is not run again; the second import reports the same failure | runs again |
| `page_isolation` (build) | pages `a`, `b` each get a fresh instance (`1 2`); within page `c` one instance (`x1 y2`); imported modules in each page's dependencies | page `c`: `x1 y1` |
| `doc_math` | nift.dev's import example (a lambda closing over a module binding) | agrees |
| `doc_try_import` | nift.dev's recoverable missing import under `try` (`io.import_source_unreadable`) | agrees (without the page's trailing `//` comment, D-045 M3) |
| `rebuild` (build) | editing `m.nft` rebuilds `a`, `b`, `c` with fresh instances; editing `y.nft` rebuilds only `c`; no change rebuilds nothing | page `c` differs |

Interpretations of D-041 (recorded; accepted by the coordinator as
consistent with D-041, reported to the owner):

* **Repeated import rebinds.** Importing a module again (same importer, another
  spelling, or through a re-export) binds the same exports of the same
  instance again; it is not a collision (option A as proposed and adopted).
  Fixtures `repeated`, `repeated_two_spellings`.
* **A failed initialisation is final in its context.** A sequential re-import
  after a failed initialisation reports the same failure without running the
  module again (D-041 #1 states it for concurrent imports). Fixture
  `failed_init`.

## D-045 review: syntax and documentation (nift.dev, legacy, VM)

Checked during normal review against nift.dev `docs/scripting-imports.html`
and `docs/structs.html` (the live pages are byte-identical to
`labs-src/nift-site` @7400024) and legacy (runtime-2 binaries).

Syntax the graph and the fixtures use, all documented: script-land
`import("path")` and template `@import("path")` (script land also accepts
`@import`), `export(name)`, `fn(name(args)) { … }`, `try { … } catch(err)
{ … }`, `throw error("…")`, `err.code` / `err.message`, `+=`, `.push(…)`,
`.stringify()`, `$[…]` in pages; lambdas only in the documentation-example
fixture. Unsupported forms are refused before execution by the graph
(`import-dynamic`, `import-nested`, …), never reinterpreted. Two
documentation examples are now fixtures (`doc_math`, `doc_try_import`); the
page's counter example (`count`/`increment`) is what `stateful_export_binding`
pins (D-046 #2).

Discrepancies, **decided by D-046** (owner, 2026-10-20): M1 — exports are
bindings (D-041 #4 reversed; fixtures flipped to `stateful_*_binding`, plus
`repeated_binding`, `reexport_binding`); M2 — lexical scoping kept, a
deliberate compatibility change (`module_fn_lexical`; documentation text
below, not published); M3 — a legacy parser defect, to be accepted in both
engines (assigned elsewhere); M4 — imports inside `try` lifted later as a
coverage item. The table as reviewed:

| # | nift.dev | Legacy | Runtime 2.0 / decisions | Fixtures |
| --- | --- | --- | --- | --- |
| M1 | "`export(name)` exports the actual binding, not a value snapshot, so exported closures retain private state" (counter example: `count` reads 2 after two `increment()`) | live alias, both directions (L50) — agrees with the page | **D-041 #4: exported values are copies** — contradicts the page and legacy. D-041 was decided on this document's earlier, wrong "copied" reading | `stateful_export_copy`, `stateful_array_copy` encode D-041; change them if the owner keeps the documented binding semantics |
| M2 | "named fn declarations resolve free names in the calling scope, matching template `@fn`" | caller's **top-level** bindings are seen, the calling function's locals are not (`return: unknown value … zz`) — matches the page only partly | D-038 (lexical; caller-dependent lookups refused, `free-name:dynamic-caller`) | the module probes; a module function reading a caller global would be refused by the VM |
| M3 | script examples end statements with trailing `// …` comments (66 lines on 12 pages, e.g. `print(err.code)   // io.import_source_unreadable` on the imports page) | **rejected**: the statement runs, then `expression cannot be empty` (exit 1); a whole-line `//` comment works | the CP10 parser matches legacy (Opaque, legacy-runtime-error) | `doc_try_import` leaves the comment out; not module-specific (reported to the coordinator) |
| M4 | a missing import inside `try` is a recoverable failure | agrees | the VM graph refuses imports inside `try` (`import-nested`) before execution — a coverage gap, not a semantic change; to lift with the lifecycle work | `doc_try_import`, `failed_init` |

Not a discrepancy: the probes and fixtures name modules `*.nft`, the
documentation `*.f`; the extension is not syntax (legacy reads any file).

### Prepared nift.dev change (M2, D-046 #4) — not published

`docs/scripting-imports.html`, the paragraph after the first example.
Current text:

> The imported script cannot implicitly read bindings from its caller.
> export(name) exports the actual binding, not a value snapshot, so exported
> closures retain private state correctly. Closures capture script locals (use
> a lambda such as double := x => x * factor); named fn declarations resolve
> free names in the calling scope, matching ordinary template @fn semantics.

Proposed text:

> The imported script cannot read bindings from its caller. `export(name)`
> exports the actual binding, not a value snapshot: the importer and the
> module share the binding's storage, so writes on either side are seen on
> the other, and importing the module again in the same run gives the same
> bindings. Functions and lambdas defined in a module resolve free names where
> they are defined — the module's own bindings and private helpers, which
> stay reachable after export — never in the calling scope: a name bound only
> by the caller is an error inside the module.

Reproducer of the behaviour that changes (legacy prints `5`; after the
change it is an error naming `zz`) — fixture `module_fn_lexical`:

```
// m.nft
fn(f()) { return zz }
export(f)
// main.f
zz := 5
import("m.nft")
print(f())
```

