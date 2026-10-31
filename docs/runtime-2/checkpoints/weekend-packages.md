# Weekend checkpoint: package imports (cand/packages) — phase 1 contract

Status: **phase 1 (investigation and contract only).** No production source is
changed. Implementation waits for the owner's go-ahead *and* for the `args`/`cmd`
integration (`integ/args-cmd`) to land in `weekend/r2`, because both change
`src/rt/module_link.cpp`.

- Base: `weekend/r2` `8bd59d9f` (strict VM default, diagnostic-position fixes).
- Read for context only: `integ/args-cmd` (`89a4e5dd`; the worktree is at
  `2dbcde8e`, one later test-only commit), which adds D-062 and the
  `import-scope:invocation-binding` refusal.
- Owner scope (fixed): package **resolution and import-time errors only**. No
  `.nift/` creation, no name-clash mangling, no diamond redesign. Packages target
  Runtime 2.0 (D-049). Folded-in obligations: D-062 #1 (`args`/`cmd` in modules,
  temporary refusal) and the 24 importing units coro-module left at `import-package`,
  `import-nested` or `import-missing`.
- Probe binary: `./nift` built from tree `f169e6f1` (= `8bd59d9f`), legacy via
  `NIFT_VM=0`. Probe scripts and raw results are listed in §9.

## 1. Corpus table

These are all the units that leave the VM with a package or nested-import reason in
the latest real-run inventory (`coromod/inv-cand/execute.jsonl`, which matches the
`argscmd` runs). Each was re-run on `8bd59d9f` in a fresh copy, with cwd set to the
script's directory (rt-diff's plan).

- **Legacy (corpus)** is the unit exactly as the inventory sees it. The heredoc
  extractor cannot see `printf`/`mkdir`/`nift add`, so manifests, locks and some
  package files are missing.
- **Legacy (installed)** applies the package setup from the unit's smoke test
  (`realise.py`). It is given only for `import-package` rows.
- **After** is the result this contract specifies. "installed: next X" is the next
  VM refusal once package resolution exists. It was measured by rewriting each package
  import to the installed entry's absolute path, which is an estimate.

In every "legacy error" row, legacy writes nothing to stdout before the error. Under
§4.2 the VM therefore matches stdout, the exit status and the first stderr line
exactly. The only difference is one extra `note:` line.

| # | Unit (smoke/…) | Legacy (corpus) | Legacy (installed) | VM now | After this checkpoint |
|---|---|---|---|---|---|
| 1 | core_language_correctness/005-use | rc 1, `proj/use.f:1:1: @import: package is not installed: cpkg` | rc 0, `42/11/101` | `import-package` | legacy error, pre-execution (§4.2); installed: next `export-struct` |
| 2 | package_callable_closure_smoke/002-test | rc 1, `site/test.f:1:1: @import: file does not exist` | rc 0, `11` | `import-package` | legacy error, pre-execution; installed: next `fn-flags` |
| 3 | package_module_export_smoke/002-t | rc 1, `site/t.f:1:1: @import: package is not installed: vips` | rc 0, `200/100/4` | `import-package` | legacy error, pre-execution; installed: next `fn-flags` |
| 4 | package_module_export_smoke/004-t2 | rc 1, `site/t2.f:1:1: @import: package is not installed: vips` | rc 1, `site/t2.f:2:7: undefined callable: scale_helper` | `import-package` | legacy error, pre-execution; installed: next `import-scope` (the program error legacy reports) |
| 5 | package_sqlite_dogfood/001-test | rc 1, `site/test.f:1:1: @import: package is not installed: sqlite` | rc 0, `false/test.db` | `import-package` | legacy error, pre-execution; installed: next `import-scope` (*) |
| 6 | package_sqlite_dogfood/002-priv | rc 1, `…priv.f:1:1: @import: package is not installed: sqlite` | rc 1, `priv.f:2:7: unknown value or malformed expression: sqlite_cli_query` | `import-package` | legacy error, pre-execution; installed: next `import-scope` |
| 7 | package_sqlite_dogfood/003-full | rc 1, `…full.f:1:1: @import: package is not installed: sqlite` | rc 1, `full.f:9:7: JSON array index 0 is out of range …` | `import-package` | legacy error, pre-execution; installed: next `import-scope` (*) |
| 8 | package_sqlite_dogfood/004-uni | rc 1, `…uni.f:1:1: @import: package is not installed: sqlite` | rc 1, `uni.f:8:7: JSON array index 0 is out of range …` | `import-package` | legacy error, pre-execution; installed: next `import-scope` (*) |
| 9 | script_import_syntax/009-bare-package | rc 1, `site/bare-package.f:1:1: import: file does not exist` | rc 0, `23` | `import-package` | legacy error, pre-execution; installed: **VM runs, equal** |
| 10 | script_import_syntax/010-legacy-package | rc 1, `site/legacy-package.f:1:1: @import: file does not exist` | rc 0, `23` | `import-package` | legacy error, pre-execution; installed: **VM runs, equal** (`@import`; D-048 #1 will reject it in both engines) |
| 11 | v44_relative_import_ownership_smoke/019-main | rc 1, `main.f:4:1: import: package is not installed: owner` | rc 0, `function-owned/…` | `import-package` | legacy error, pre-execution; installed: next `import-nested` |
| 12 | v44_relative_import_ownership_smoke/020-leak | rc 1, `leak.f:1:1: import: package is not installed: owner` | rc 1, `leak.f:3:7: undefined callable: dynamic_value` | `import-package` | legacy error, pre-execution; installed: next `import-nested` |
| 13 | v44_relative_import_ownership_smoke/022-traversal | rc 1, `traversal.f:1:1: import: package is not installed: owner` | rc 1, `…owner/src/main.f:5:27: import: package-relative import escapes package root: …` | `import-package` | legacy error, pre-execution; installed: next `import-nested` (the escaping import is inside a function) |
| 14 | v44_relative_import_ownership_smoke/024-symlink-escape | rc 1, `symlink-escape.f:1:1: import: package is not installed: owner` | rc 1 (the extractor cannot make the symlink) | `import-package` | legacy error, pre-execution; installed: next `import-nested` |
| 15 | v45_integration_dogfood/002-tool | rc 1, `site/tool.f:1:1: @import: package is not installed: demo` | rc 1, `tool.f:8:8: ffi_open: lib/fixture.so: cannot open …` | `import-package` | legacy error, pre-execution; installed: next `coro-module` |
| 16 | v46_import_worker_ownership_smoke/010-module-init | rc 1, `module-init.f:1:1: import: package is not installed: init-worker` | rc 0, `42/43` | `import-package` | legacy error, pre-execution; installed: next `fn-flags` |
| 17 | v46_import_worker_ownership_smoke/011-alias-direct | rc 1, `…: package is not installed: alias-owner` | rc 0, `true/true` | `import-package` | legacy error, pre-execution; installed: next `fn-flags` |
| 18 | v46_import_worker_ownership_smoke/012-alias-workers | rc 1, `…: package is not installed: alias-owner` | rc 0, `true/true/true/true` | `import-package` | legacy error, pre-execution; installed: next `fn-flags` |
| 19 | v46_import_worker_ownership_smoke/013-direct | rc 1, `direct.f:1:1: import: package is not installed: worker` | rc 0, `2` | `import-package` | legacy error, pre-execution; installed: next `import-nested` |
| 20 | v46_import_worker_ownership_smoke/014-one-worker | rc 1, `one-worker.f:1:1: import: package is not installed: worker` | rc 0, `2` | `import-package` | legacy error, pre-execution; installed: next `import-nested` |
| 21 | v46_import_worker_ownership_smoke/015-exact-async-owner | rc 1, `…: package is not installed: async-owner` | rc 1, `…async-owner/src/main.f:3:27: future: return: module-private async callables are unsupported` | `import-package` | legacy error, pre-execution; installed: next `import-collision` |
| 22 | v46_import_worker_ownership_smoke/016-main | rc 1, `main.f:1:1: import: package is not installed: worker` | rc 0, `11/21/left/right/0/31/1` | `import-package` | legacy error, pre-execution; installed: next `import-nested` |
| 23 | v48_callback_parity/001-p | rc 1, `pkg/p.f:1:1: import: package is not installed: a` | rc 0, `15/19/16/20` | `import-package` | legacy error, pre-execution; installed: next `import-name-clash` (no mangling: owner scope) |
| 24 | v44_relative_import_ownership_smoke/004-one | rc 1, `direct/modules/nested/one.f:1:1: @import: script is not readable: …/two.f` | — | `import-missing` | legacy error, pre-execution (§4.2) |
| 25 | v44_relative_import_ownership_smoke/007-missing-main | rc 1, `…missing-main.f:1:1: import: script is not readable: …/missing.f` | — | `import-missing` | legacy error, pre-execution |
| 26 | v44_relative_import_ownership_smoke/008-main-cycle | rc 1, `…main-cycle.f:1:1: import: script is not readable: …/main-cycle-link.f` | — | `import-missing` | legacy error, pre-execution |
| 27 | v45_ffi_package/001-use | rc 1, `use.f:1:1: @import: script is not readable: …/ffi_math.f` | — | `import-missing` | legacy error, pre-execution |
| 28 | v46_b4_cp5b_import_source_recoverable/001-outer | rc 1, `outer.f:1:1: import: script is not readable: …/missing-inner.f` | — | `import-missing` | legacy error, pre-execution |
| 29 | v46_b4_cp5b_import_source_recoverable/003-outer_w | rc 1, `outer_w.f:3:1: import: script is not readable: …` (after starting a worker) | — | `import-missing` | legacy error, pre-execution (the worker never starts) |
| 30 | v46_b4_cp6_import_module_projection/009-mod_se | rc 1, `mod_se.f:1:1: import: script is not readable: …/mod_se_child.f` | — | `import-missing` | legacy error, pre-execution |
| 31 | v46_b4_cp7_worker_hardening/001-mod_w | rc 1, `mod_w.f:3:1: import: script is not readable: …` (after starting a worker) | — | `import-missing` | legacy error, pre-execution |
| 32 | v44_relative_import_ownership_smoke/013-runtime-child | rc 0 | — | `import-nested` (import in a function) | still refused |
| 33 | v44_relative_import_ownership_smoke/015-child | rc 0 | — | `import-nested` (function) | still refused |
| 34 | v44_relative_import_ownership_smoke/018-main | rc 1, `src/main.f:11:1: import: package is not installed: bare` | — | `import-nested` (functions, method, lambda) | still refused (placement is checked before resolution) |
| 35 | v46_b4_cp3_recoverable_errors/002-importer | rc 0, `user.module/2` | — | `import-nested` (`try { import }`) | still refused |
| 36 | v46_b4_cp3_recoverable_errors/004-worker-importer | rc 1, `…: import: failed import created worker resources: unsafe` | — | `import-nested` (try) | still refused |
| 37 | v46_b4_cp3_recoverable_errors/006-fatal-worker-importer | rc 1, `…: failed import created worker resources: division by zero` | — | `import-nested` (try) | still refused |
| 38 | v46_b4_cp5b_import_source_recoverable/002-main_rb | rc 0, `caught:io.import_source_unreadable/false` | — | `import-nested` (try) | still refused |
| 39 | v46_b4_cp5b_import_source_recoverable/004-main_w | rc 1, `…failed import created worker resources: import: script is not readable: …` | — | `import-nested` (try) | still refused |
| 40 | v46_b4_cp6_import_module_projection/004-imp_rb | rc 1, out `caught:user.fail/false`, `imp_rb.f:3:7: undefined callable: made` | — | `import-nested` (try) | still refused |
| 41 | v46_b4_cp6_import_module_projection/006-imp_w | rc 1, `…failed import created worker resources: unsafe` | — | `import-nested` (try) | still refused |
| 42 | v46_b4_cp7_worker_hardening/002-main_w | rc 1, `…failed import created worker resources: import: script is not readable: …` | — | `import-nested` (try) | still refused |
| 43 | v46_import_worker_ownership_smoke/002-main | rc 0 | — | `import-nested` (function, in a package) | still refused |
| 44 | v46_import_worker_ownership_smoke/006-main | rc 0 | — | `import-collision` (in a package) | still refused (out of scope) |

(*) In the sqlite rows, `import-scope` comes from the stale local package snapshot.
`sqlite.f:116:180` is a lambda parameter `db`, and the entry also declares `db`. This
is the linker's conservative name rule, not package resolution. All 7 snapshot
packages also use `run()` (deferred) and `@struct`.

**Counts.** The inventory currently sends 44 units to fallback for these reasons. As
the corpus stands, the 31 `import-package` and `import-missing` units stop falling
back. Each ends in legacy's own error, pre-execution. The 13 `import-nested` and
`import-collision` units stay refused. With packages installed as their smoke tests
install them, 2 of the 23 package units run on the VM with legacy's output. The other
21 move on to these next refusals:

| Next refusal | Units |
|---|---|
| `fn-flags` (`@fn`, the D-048 #4 fixture migration) | 5 |
| `import-nested` | 7 |
| `import-scope` | 5 |
| `export-struct` | 1 |
| `coro-module` | 1 |
| `import-collision` | 1 |
| `import-name-clash` | 1 |

**The coro-module 24.** coro-module reports 11 `import-package`, 8 `import-nested`
and 5 `import-missing` units. Those 24 are a subset of rows 1–44. I could not
reproduce that exact split. In this table, 18 rows use `thread`/`async`/`await`:
9 package rows, 5 nested, 3 missing and 1 collision. The difference does not change
any disposition. After this checkpoint their package and missing-file rows end in
legacy's error, or reach coro-module's rules (row 15) and `fn-flags`. Their nested
rows stay `import-nested`.

Adjacent import rows that this checkpoint does not change:

| Reason | Rows | Basis |
|---|---|---|
| `import-dynamic` | 4 | intentional, D-037 |
| `module-return` | 1 | see Q6 |
| `export-struct` | 4 | B4 |
| `module-host-op:*` | 2 | R1 |

## 2. Legacy resolution semantics (`NIFT_VM=0`, `src/ParserScript.cpp` `execute_import_file`)

**Is the argument a package?** A package is an argument with no `/`, no `\` and an
empty extension (`ParserScript.cpp:482`; the VM uses the same test). So
`import("demo.v2")` is a file import ("script is not readable: …/demo.v2"). Packages
resolve only when `standalone_script_host_` is set: `nift file.f`, `-c`, stdin, the
REPL and workers. Build hooks and embedded scripts treat a bare name as a relative
file. The VM path (`run_script_on_vm`, `CLI.cpp`) is only ever standalone.

**Resolution order.** Each step's error text is given exactly. Every error is
reported at the import statement in the importing file (`file:L:C`), as described
under "Errors and positions" below.

1. `valid_package_name`: 1–64 characters of `[a-z0-9_-]`, starting and ending with
   `[a-z0-9]`, and not `con`/`prn`/`aux`/`nul`/`comN`/`lptN`. Otherwise:
   `invalid package name: <arg>`. This includes `import("")`, which gives
   `invalid package name: ` (the VM says `import-dynamic` today).
2. **Project = `fs::current_path()`**, not the script's directory. The same script
   finds `demo` when run from the project and gets `package is not installed: demo`
   from a subdirectory. Package names imported from inside a package resolve again
   against the same flat project store. There is no nesting.
3. `PackageTransaction::acquire_read`. **Side effect:** it creates `.nift/` and
   `.nift/.packages-transaction.lock` (`O_CREAT`). A failing import into an empty
   directory leaves both behind, and every corpus run created the lock file. It then
   takes a blocking `flock(LOCK_SH)`: legacy waited 3.5 s while a writer held the
   exclusive lock. Errors:
   - `cannot create package lock directory: …`
   - `cannot acquire package read lock`
   - `package read lock identity changed`
   - `unfinished package transaction; run a package command to recover it` (when
     `.nift/package-transaction.json` exists)
4. `.nift/packages` must canonicalise inside the project: `package store must stay
   inside the project` (when the store is a symlink to elsewhere).
   `package path escapes .nift/packages: <n>` is defensive and unreachable with a
   valid name.
5. `manifest.json` absent: `package is not installed: <n>` (`package.not_installed`,
   recoverable). Unreadable or invalid: `invalid package manifest: <n>: <detail>`
   (fatal). One example detail is `manifest.json: expected ',' between object members
   at line 1, column 15`. An uncontained entry gives
   `invalid package manifest: demo: manifest entry must be a contained relative .f path`.
6. entry = `root/manifest.entry`. It must be within the root, symlinks resolved:
   `package entry escapes the package directory: <entry>`.
7. `manifest.name` must equal the import: `installed package name does not match
   import: <n>`.
8. **Version and identity come from the lock, not the manifest version.** The import
   reads `.nift/packages.lock.json` (v1 or v2) through `lock_node_identity`. Errors:
   - The loader's own text, e.g. `file does not exist` when the lock is absent. No
     path is named.
   - `package lock is missing dependency: <n>`.
   - `lock entry '<n>' has an invalid exact commit` (the commit must be `local` or
     an exact 40-hex commit).

   The manifest `version` is never consulted at import. There is one installed copy
   per name per project, so an import cannot be ambiguous. `nift add`/`update` resolve
   versions; import only verifies what is installed.
9. A non-`local` commit requires `root/.git/HEAD` (raw content) to equal it:
   `installed package revision does not match package lock: <n>`.
10. The read itself fails with `script is not readable: <abs entry>`
    (`package.import_source_unreadable`). The read lock is released after the read.

**Relative and nested module imports.**
- A relative path resolves against the importing *module's* directory (the
  `import_base`). The entry's base is the script's directory; for `-c`, stdin and the
  REPL it is cwd. `\` is read as `/`, then the path is made absolute and lexically
  normalised.
- A **relative** import from inside a package must stay within that package's root,
  lexically and after symlinks: `package-relative import escapes package root: <abs>`.
  This was probed for `../` and for a symlink. Each such import re-acquires the read
  lock and re-validates provenance (`stale package ownership: …`).
- An **absolute** import from inside a package is not contained. It works (probe
  `pkg-absolute-import`).
- Cycles: `script import cycle through <abs>`.
- Missing file: `script is not readable: <abs>` (`io.import_source_unreadable`).

**Errors and positions.**
- An import error is reported at the failing import statement in the importer's file.
- The message carries one prefix per active import frame, *including the failing one*,
  outermost first. Each prefix uses that import's own spelling (`import: ` or
  `@import: `). For example, entry `@import("mx.f")` and `mx.f` importing a missing
  file gives `mx.f:1:1: @import: import: script is not readable: …`.
- A runtime error inside a module carries the same prefixes, at the module's own
  position (`dz.f:2:6: @import: division by zero`).
- Everything happens at the statement, after earlier statements ran. `before` is
  printed first, and a worker started earlier has already run.
- An import that is never reached never fails: `return` at the entry's top level
  before a missing import gives rc 0.
- Top-level import errors cannot be caught. A `try { import(...) }` is not a
  top-level import (the VM's `import-nested`).
- Importing the same package twice into one unit, by name twice or by name and path,
  fails with `export collides with existing binding: <x>` (VM: `import-reinitialised`).

**`args`/`cmd` from modules (D-062 #1 obligation).** Probes `ac/e1`–`e14`:

| Case | Legacy |
|---|---|
| module function called from the entry reads `args`/`cmd`/`args[0]` | sees the entry's values (`2`, `e1.f`, `one`) |
| module top level reads `args` (e2) | `import: unknown value or malformed expression: args` at the read, after earlier output |
| module top level *calls* its own function that reads `args` (e3) | the same failure (`m.f:1:15: import: return: …`) |
| module B's top level calls module A's reading function (e14) | the same failure at A's read |
| module function reached through another module's function (e4), a function value or lambda (e7), or a `thread` (e8) | sees the entry's values |
| a lambda stored at module top level, called from the entry (e9) | sees them |
| entry function with a local or parameter `args` calls the module function (e5) | the module still sees the entry's *root* `args`, not the caller's local |
| entry top-level `args := ["mine"]` before the import (e6) | the module function sees `["mine"]` |
| the same replacement between two calls (e10) | `2` then `1` |
| a call inside `if (false)` at module top level (e13) | runs fine (never executed) |
| a module's own top-level `args := …` (e12) | the module's private binding |
| `-c` / stdin | `cmd` is `<command-line>` / `<stdin>` |

So legacy's rule is dynamic. A free name in a module function falls back to the
*current* root scope. During a module's initialisation that root is the module's
fresh scope; after it, the root is the entry's. The VM refuses the general form
(D-038/D-046 #4: `z := 5` read from a module gives `import-scope`). For the two
invocation names the observable result is simple: module code *initialising* cannot
see them, and module code *called later* sees the entry's root bindings.

## 3. Current VM (`8bd59d9f`)

- `module_graph.cpp:122-124` refuses every package-shaped literal (`import-package`)
  before any file access.
- Missing modules (`import-missing`), cycles (`import-cycle`), `export-missing`,
  `export-duplicate` and `module-return` are refused with "… is not supported by the
  Runtime 2.0 VM yet". Each of these is a legacy program error.
- Positions agree with legacy for missing modules and cycles. They do not for
  `module-return`: legacy reports at the import, the VM at the `return`.
- Runtime errors inside module initialisation are always prefixed `import: `
  (`script_runner.cpp:266`). Legacy writes `@import: ` for an `@import` frame
  (`dz.f:2:6: @import: division by zero` against the VM's `import: …`).
- Linking reads every module up front. R1 (`module_host_op.h`) refuses any linked
  unit that can call a host built-in. **Consequence:** nothing a VM-run linked program
  does before an import can create, change or remove a module or package file. Only
  another process can.

## 4. Contract

### 4.1 What resolves

- `import("name")` and `@import("name")` with a package-shaped literal resolve in the
  VM module graph by the steps of §2, in legacy's order and with its texts.
  `import("")` is a package name and gives legacy's `invalid package name: `.
- Project is cwd, as in legacy, for imports from the entry and from any module.
- The resolved entry is an ordinary module: the same graph, linker, identity
  (`module_identity`), initialise-once (D-041), and D-046 #2 exported bindings.
- `Module::package_root` and `Module::package_name` are recorded. A relative import
  from a module with a package root must satisfy `path_within(root, path)`, lexically
  and canonically. Otherwise: `package-relative import escapes package root: <abs>`.
  Absolute imports are not contained, as in legacy.
- **No creation.** The VM never creates `.nift/`, the lock file or anything else.
  - Without `.nift/`, a valid name gives `package is not installed: <n>`. This is the
    text legacy gives, without legacy's directory creation.
  - With `.nift/` but no `.nift/.packages-transaction.lock`, the VM reads without the
    lock, but still checks the journal. Legacy would create the lock file. No `nift
    add` has run in such a store; it is a hand-made store. See Q2.
- **One snapshot.** When the lock file exists, the VM opens it without `O_CREAT`
  (Windows: `OPEN_EXISTING`). It takes the shared lock blocking, like legacy, at the
  first package import of the graph build. It holds it until the graph is built, then
  releases it before compile and run.
  - Legacy's per-import re-validation (`stale package ownership: …`) cannot fail inside
    one held snapshot, so it is not reproduced.
  - The journal check runs at acquisition.
- **Dependencies.** The graph's dependency and trace (`read-source`) list is the module
  files read, as legacy records it (`result_.dependencies`). Manifest and lock reads
  are not dependencies, as in legacy.
- **Identity.** A package imported by name and by path is one module. Within one unit
  that is a second import, which stays `import-reinitialised` (no diamond redesign).
  Legacy fails it as `export collides`.

### 4.2 Import-time errors: rejected before execution as program errors

Every resolution error of §2 is a **program error**:

- every package step;
- `script is not readable`;
- `script import cycle through`;
- `package-relative import escapes package root`.

It is not a missing VM feature. It replaces today's `import-package` and
`import-missing` / `import-cycle` refusals.

**When it happens.** The VM finds the error while building the graph, before any
statement runs. The program is rejected whole and nothing of it runs, including the
statements before the import. Exit status 1. This is the D-062 #2 pattern and an
**approved-break candidate (Q1)**: legacy runs the earlier statements first. Under R1
those statements can only compute, print and start tasks, so the departure is output
and timing.

**Diagnostic.** For an error at an import in file `F` (line L, column C), with import
frames `f1…fn` (outermost first, `fn` the failing import):

```
error: F:L:C: <p1><p2>…<pn><legacy message>
note: the program was not executed: its imports are resolved before it runs, so no part of it ran
```

- `pk` is `@import: ` when frame k is spelled `@import` (`kFlagTemplateImport`),
  otherwise `import: `.
- The message is legacy's text from §2, with paths as `generic_string()` absolute.
- The diagnostic has no "not compiled for the VM", no "yet", no `NIFT_VM=0` hint and
  no request to report it.
- **Positions.** The position is the failing import statement's start in its own
  module, with `positioned = true` set explicitly at each site. Every one of these
  errors has an import statement, so a positionless form should not arise. If it ever
  does, the existing `(the VM gave no exact position)` path is used, never `1:1`.

Corpus check: rows 1–31 match legacy's first stderr line and its rc. All 8 VM
positions for missing modules already equal legacy's.

**Precedence.** These are the first failure in graph order. Placement refusals
(`import-nested`, `export-nested`, `module-return`) for a unit are checked when the
graph visits that unit, as today, so row 34 stays `import-nested`. Compile-time
refusals come later, so an import error wins over them.

**Unreached imports.** If the entry's top level has a `return` outside functions
before the failing import's top-level statement, rc and output could differ (legacy
rc 0). Such a unit keeps a refusal, `import-error-unreached`, worded as "not supported
yet". Modules cannot be affected: any module top-level `return` is already
`module-return`.

**Small text fix in scope.** Runtime errors raised while a module initialises use each
frame's own spelling (`@import: ` / `import: `), outermost first, as in legacy. Today
every prefix is `import: `. If D-048 #1 lands first, the point is moot.

### 4.3 Still refused before any effect (unchanged reasons)

| Reason | Why |
|---|---|
| `import-nested` | imports in functions, lambdas, methods and `try` blocks: runtime import, D-041 failure caching, transactional rollback |
| `import-dynamic` | D-037 |
| `import-reinitialised` | D-041 #2 policy; no diamond redesign |
| `import-name-clash` | no mangling |
| `import-collision`, `import-scope`, `export-*`, `module-return`, `import-opaque:*`, `import-parse-error`, `import-limit` | unchanged |
| `module-host-op:*` | R1 |
| `callable:package_path`, `callable:module_path` | unchanged |
| `statement:Import` in sessions | D-054: no REPL package use on the VM |

### 4.4 `args`/`cmd` in modules (lifts D-062 #1; legacy-compatible)

**I1. Functions see the bindings.** A read of `args` or `cmd` inside a module's
function body sees the entry script's invocation bindings. That covers a lambda inside
a function, and a function reached by any path from the entry: directly, through other
modules, as a value, or in a `thread`/`async`.

The linker splices the module at its import, so such a read compiles exactly as an
entry function at that position reading the name. This gives:
- e6: an entry top-level `args := …` *before* the import is what the module sees.
- e10: a replacement after a module function may already have read the name is refused
  as the entry-function equivalent is today (`free-name`), not mis-run.

**I2. Module initialisation does not.** A read that may execute while any module
initialises is refused before any effect with **`import-scope:invocation-init`**, at
the read, `positioned`. "May execute" means:
- a read at a module's top level outside functions, including a top-level lambda's
  body;
- a read in a function that any module's top-level code may reach. A function is
  reachable when that code mentions it by name, as a call or as a value, directly or
  through the names mentioned in the bodies of the functions it mentions (fixed
  point). Names are disjoint across units because of `import-name-clash`, so the
  closure is exact by name and conservative for values.

A note gives the initialising mention (`note: reached while <module> initialises, at
<file>:L:C`). Legacy fails these if they execute (e2, e3, e14). The refusal is
conservative for code that is never executed (e13). Its wording is a program error
("an imported module reads the entry script's `args`/`cmd` while it initialises; they
are visible to its functions only after the import"), with no "yet". See Q4.

**I3. Unchanged.**
- A module *declaring* `args`/`cmd` at top level stays refused
  (`import-scope:invocation-binding`, the strict policy of D-062). That remains the
  only use of that reason.
- Locals, parameters and functions named `args` stay `binding-name-reserved` /
  `param-name-reserved` / `fn-name-reserved` (D-062 open question 1).
- Struct methods reading `args` stay `free-name`.
- `cmd(...)` stays `callable:cmd`.
- Sessions are unchanged.

**I4. Other entry globals.** I1 applies only to the two invocation names. Other entry
globals read from a module function stay `import-scope` (D-038 lexical; legacy's
dynamic lookup is not reproduced).

### 4.5 Approved departures this needs (for a decision record)

1. Rejection before execution for import resolution errors (Q1).
2. No `.nift/` or lock-file creation on lookup.
3. One read-lock snapshot instead of per-import re-validation.
4. `import-scope:invocation-init` is conservative for never-executed module code.

## 5. Implementation plan

The work starts after `integ/args-cmd` is in `weekend/r2`, and is rebased onto it.
`cand/coro-module` does not touch these files. `cand/collections` changes one line of
`script_runner.cpp` and takes D-063, so this decision is D-064 or the next free number.

| File | Change | Size |
|---|---|---|
| `src/rt/package_resolve.{h,cpp}` (new) | Pure resolver: `resolve_package(name, project) -> {entry, root, error text}`. Uses header-only `PackageMetadata.h`/`PackageGraphLock.h` and `FileSystem` helpers. Takes its own non-creating shared lock (POSIX `open` without `O_CREAT` + `flock`; Windows `OPEN_EXISTING` + `LockFileEx`), plus the journal check. A `PackageSnapshot` RAII object is held by the graph builder. | ~180 |
| `src/rt/module_graph.{h,cpp}` | Package branch in `GraphBuilder::import`; `Module::package_root`/`package_name`; package-relative containment. `ModuleGraph::error {message, frames, where, offset, positioned}`: a program error distinct from `reason`. Missing, cycle and package errors fill it. Entry top-level `return` before → `import-error-unreached`. `import("")` → package path. Split into `module_graph_errors.cpp` if needed for rt-lint's 400 lines (now 289). | ~110 |
| `src/rt/module_link.{h,cpp}` | Propagate the graph error. Per-module frame-spelling chain (replaces `depth` for prefixes). D-062 #1: invocation-name uses in module function bodies are allowed; `import-scope:invocation-init` comes from a new `rt/module_invocation.{h,cpp}` (mention graph, fixed point, first read in text order). | ~40 + ~120 |
| `src/rt/script_runner.cpp`, `refusal_text.cpp` | Render §4.2 diagnostics and prefixes; `invocation-init` wording. `script_runner.cpp` is ~380 lines after integ, so a helper moves out if needed. | ~50 |
| `src/rt/refusal_inventory.cpp` | Builds the same graph (F12 lesson). A program error is listed as `import-error:<kind>`, not as a fallback reason. | ~15 |
| `Makefile` | `src/JsonFile.cpp` into `RT_VALUE_SOURCES` (rt unit binaries link the resolver); `test-runtime2-packages` target. | ~10 |
| `tests/runtime2/packages/packages.py` + `mutants.sh` (new) | §6 | ~500 |
| Docs | this file; decision record; `compatibility.md` approved breaks; `classification.tsv`: `import-package` closed, `import-missing`/`import-cycle` → `import-error:*` (not fallbacks), new `import-scope:invocation-init` and `import-error-unreached` rows; refusal register. | — |

Production total is about 500–550 lines in 8–10 files. The earlier release-plan
estimate of 220–300 did not include the D-062 lift or the error conversion.

**Risks.**
- Lock semantics: blocking like legacy, no timeout. Behaviour without a lock file.
  Windows `LockFileEx` without create.
- Linking `JsonFile.cpp` into the rt test binaries.
- rt-lint size limits in `module_graph.cpp` and `script_runner.cpp`.
- Positions for every error site (each must set `positioned`).
- The precedence of graph errors over compile refusals.
- Windows path case (`module_identity` lowercases) against `path_within` containment.
- Over-promising: only 2 of 23 package units run fully on the VM. The rest need
  D-048 fixture migration, `run()`, `export-struct`, `import-nested`,
  `import-name-clash` and `import-reinitialised`.

## 6. Tests (phase 2)

**Legacy/VM differentials.** `tests/runtime2/packages/packages.py` runs each workspace
in both engines and compares stdout, the first stderr line (text and position) and rc.
The "before" output is the approved difference. Cases:
- every §2 step (§9 `lg/*`);
- cwd vs script directory;
- package in package, missing and present;
- package-relative helper;
- `../` and symlink escapes;
- absolute import from a package;
- runtime error in a package;
- mixed `@import`/`import` frames;
- cycle, self-cycle, missing and nested missing;
- the 9 + 2 corpus shapes;
- the unreached-`return` refusal.

**Negative controls, refused before effect** (no output, no files):
- the cases that stay refused: `import-name-clash` across two packages,
  `import-reinitialised` by name + path, a package using `@fn`, `module_path()`;
- an assertion that a run never creates `.nift/` or the lock.

**Concurrency.** With an exclusive lock held, the VM waits and then runs, like legacy.
A journal present gives the transaction error.

**`args`/`cmd`.** e1, e4–e10, e8 and e9 run equal to legacy. e2, e3, e13 and e14 are
refused `import-scope:invocation-init` at the read. e12 stays
`import-scope:invocation-binding`.

**Mutants.** Each must make a test fail:
- the script directory as project;
- `.nift` creation;
- a missing per-frame prefix or wrong spelling;
- `args` visible at module top level;
- the init closure not following value mentions;
- no package-relative containment;
- no lock taken.

**Inventory re-collect.** Rows 1–31 leave the fallback list. Rows 32–44 are
unchanged.

**nift-regression-suite.** `contract/v44_packages_smoke.sh`,
`v46_import_ownership_smoke.sh`, `v46_package_graph_smoke.sh`,
`v48_package_combined_fixture_smoke.sh` and `v45_integration_smoke.sh`, with the VM
as default.

## 7. Open owner questions

1. **Error timing.** Should import resolution errors be rejected before execution
   (proposed, as D-061/D-062 #2), or raised at the import after earlier statements
   ran (legacy order)?
   - The raise-at-point alternative truncates the linked text at the failing import
     and needs a positioned raise operation.
   - It adds ~150 lines, and compile refusals for code after the import vanish.
   - It also keeps legacy's rc 0 for unreached imports.
2. **Store without a lock file.** For `.nift/` with no `.packages-transaction.lock`,
   should the VM read unlocked (proposed; journal still checked)? The alternative is
   to treat it as an error or as "not installed".
3. **Uninformative legacy texts.** Keep legacy's texts exactly (proposed for parity),
   or improve them? The case is `file does not exist` for an absent lock, which names
   no path. A possible replacement is `package lock not found: .nift/packages.lock.json`.
4. **`import-scope:invocation-init` wording.** Should it be a program error (proposed;
   no "yet") although it is conservative for never-executed module code? The
   alternative is a "not supported yet" refusal.
5. **A module's private top-level `args := …`** (legacy runs it, e12). Keep it refused
   (D-062 strict policy, proposed), or allow it as a module-private name?
6. **Out of scope unless asked.** `module-return`, `export-missing` and
   `export-duplicate` are legacy program errors too. Should they get the §4.2
   treatment? They are unconditional only at top level, and `module-return`'s legacy
   position is the import, not the `return`.

## 8. Not done in phase 1

No production source was changed. No tests were added yet. Nothing was pushed.

## 9. Evidence (scratch, not committed)

Under `/tmp/claude-0/-home-claude/5262fcab-c28a-58db-987c-2c4b624387c1/scratchpad/packages/`:

| Path | Contents |
|---|---|
| `units.txt` | the 44 units |
| `run_units.py`, `realise.py` | the runner and the package installer / pathifier |
| `corpus.json` | §1 corpus columns |
| `real.json` | installed and next-refusal columns |
| `lg/` | §2 resolution probes |
| `ge/` | import-time error and prefix probes |
| `ac/` | `args`/`cmd` probes (e1–e14, s1, s2; VM side run with the `integ-argscmd` binary) |
| `prior_units.json` | reasons from the inventory |
