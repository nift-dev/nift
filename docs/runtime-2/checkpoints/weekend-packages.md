# Weekend checkpoint: package imports (cand/packages) — phase 1 contract

Status: **phase 1 (investigation and contract only), owner rulings of 2026-11-01
folded in (§4, §7).** No production source is changed. Implementation waits for the owner's go-ahead *and* for the `args`/`cmd`
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
| 34 | v44_relative_import_ownership_smoke/018-main | rc 1, `src/main.f:11:1: import: package is not installed: bare` | — | `import-nested` (functions, method, lambda) | legacy error, pre-execution: the program error at 11:1 takes precedence over the nested-import refusal (§4.3) |
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
the corpus stands, 32 units stop falling back: the 31 `import-package` and
`import-missing` units and row 34. Each ends in legacy's own error, pre-execution.
The other 11 `import-nested` units and the `import-collision` unit stay refused. With packages installed as their smoke tests
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

## 4. Contract (owner rulings of 2026-11-01 folded in)

Guiding line (owner): **an unsupported VM capability is a feature refusal ("not
supported by the Runtime 2.0 VM yet"); an invalid program is a program error** (legacy
text, stable reason id, exit 1, no "yet", no legacy hint, no request to report it).

Two ideas are kept apart throughout:
- **User-program side effects.** None happen before a program error or refusal: no
  statement of the entry or of any module runs, and none of their output, files,
  processes, tasks or environment changes occur.
- **Package-management housekeeping.** Creating (if absent) and taking the package
  lock file `.nift/.packages-transaction.lock` during resolution is the *single*
  allowed exception (owner, Q2). It is not a user-program effect, it happens whether
  resolution then succeeds or fails, and nothing else is ever written.

### 4.1 What resolves

- `import("name")` and `@import("name")` with a package-shaped literal resolve in the
  VM module graph by the steps of §2, in legacy's order and with its texts.
  `import("")` is a package name and gives legacy's `invalid package name: `.
- Project is cwd, as in legacy, for imports from the entry and from any module.
- The resolved entry is an ordinary module: the same graph, linker, identity
  (`module_identity`), D-041 initialise-once, D-046 #2 exported bindings, and the
  same initialisation order. Successful imports keep their order and run-once rules.
- `Module::package_root` and `Module::package_name` are recorded. A relative import
  from a module with a package root must satisfy `path_within(root, path)` lexically
  and canonically, else `package-relative import escapes package root: <abs>`.
  Absolute imports are not contained, as in legacy.
- **Locking (Q2 ruling):**
  1. Legacy's path and mechanism: `.nift/.packages-transaction.lock`, opened as
     legacy's `open_and_lock` does (POSIX `O_CREAT | O_RDWR | O_NOFOLLOW`, mode 0644,
     regular-file check, `flock(LOCK_SH)`; Windows `OPEN_ALWAYS` + `LockFileEx`),
     then legacy's `lock_identity_valid` and journal check. The lock is on the
     descriptor, never on the file's existence.
  2. `O_CREAT` without `O_TRUNC`/`O_EXCL` is atomic create-or-open: concurrent
     creators get the same file, and an existing file is never truncated, replaced or
     written. Its contents and mode are preserved.
  3. **`.nift/` is never created.** If `<cwd>/.nift` does not exist, no lock is taken
     and a valid name gives `package is not installed: <n>` (legacy's text, without
     legacy's directory creation). Nothing is installed, modified or published, and no
     other `.nift/` artifact is created.
  4. Critical section: the lock is taken at the first package import of the graph
     build and held, as an RAII object of the graph builder, until the graph is
     complete; it is released before compile and run on every path (success, program
     error, feature refusal, C++ exception). On cancellation (signal, kill) the kernel
     releases `flock`/`LockFileEx` with the descriptor.
  5. Acquisition blocks while a writer holds the exclusive lock, as in legacy (no
     timeout; legacy waited 3.5 s in the probe). A failure to open or lock gives
     legacy's `cannot acquire package read lock` (and `package read lock identity
     changed`, `unfinished package transaction; run a package command to recover it`)
     at the import, before anything runs.
     - It is a **package-management error** (owner), with its own stable reason id,
       `import-error:package-lock`.
     - It uses the §4.2 program-error format, never "not compiled for the VM" or
       "not supported yet".
  6. One snapshot: legacy's per-import re-validation (`stale package ownership: …`)
     cannot fail inside one held lock and is not reproduced.
- **Dependencies.** The graph's dependency and `read-source` list is the module files
  read, as legacy records them. Manifest and lock reads are not dependencies.
- **Identity.** A package imported by name and by path is one module. Within one unit
  that is a second import and stays `import-reinitialised` (no diamond redesign);
  legacy fails it as `export collides with existing binding: <x>`.
- **Texts (Q3 ruling).** Every message follows legacy exactly, including the pathless
  `file does not exist` for an absent lock file.

### 4.2 Import-resolution errors: program errors, rejected before running (Q1 ruling)

**Covered:**
- every package step of §2 (name, store, manifest, entry, lock, revision, lock
  acquisition);
- a missing or unreadable imported file (`script is not readable: <abs>`);
- an import cycle (`script import cycle through <abs>`);
- a package-path escape (`package-relative import escapes package root: <abs>`).

**Scope (ruling).** This covers only import-*resolution* errors in whole-program
execution (`nift file.f`, `-c`, stdin).
- Runtime errors inside successfully imported modules keep their runtime-error
  semantics: they happen during execution, after earlier effects.
- Interactive startup files (`~/.niftrc`, `nift -i file`) and persistent sessions
  keep their own incremental contracts (D-054, D-061). Whole-program prevalidation is
  not extended to them; their `statement:Import` refusal is unchanged.

**Behaviour.** The VM finds the error while building the graph. The program is
rejected whole: no user-program side effect, including from statements before the
import and from modules imported before it (none has been initialised). Exit
status 1. Only the §4 lock housekeeping may have happened.

**Diagnostic.** For an error at an import in file `F` (line L, column C), with import
frames `f1…fn` (outermost first, `fn` the failing import):

```
error: F:L:C: <p1><p2>…<pn><legacy message>
note: the program was not executed: its imports are resolved before it runs, so no part of it ran
```

- `pk` is `@import: ` when frame k is spelled `@import` (`kFlagTemplateImport`),
  otherwise `import: `. The message is legacy's text, paths absolute
  (`generic_string()`).
- Position: the failing import statement's start in its own module, the position
  legacy reports. `positioned = true` is set explicitly at every site. All covered
  errors have an import statement; should a positionless form ever arise, the existing
  `(the VM gave no exact position)` path is used. A position is never invented.
- Stable reason ids (for classify-all, the inventory and tests; not shown as a feature
  name): `import-error:package` (package resolution steps), `import-error:package-lock`
  (lock acquisition: `cannot acquire package read lock`, `package read lock identity
  changed`, `unfinished package transaction; run a package command to recover it`),
  `import-error:missing`, `import-error:cycle`, `import-error:package-escape`.

**Approved difference (record in `compatibility.md`, Approved breaks):**

| | Legacy (`NIFT_VM=0`) | VM |
|---|---|---|
| statements before the failing import | run (output, effects, started workers) | none run |
| error text, position, prefixes | legacy | the same |
| exit status | 1 | 1 |
| lock file | `.nift/` and lock created if absent | lock created only if `.nift/` exists; `.nift/` never created |
| unreached import (top-level `return` earlier) | not reached, rc 0 | rejected, rc 1 (below) |

Corpus check: rows 1–31 then match legacy's stdout (empty), first stderr line and rc;
the VM adds one `note:` line.

**Top-level `return` before a failing import (ruling).** The pre-execution rejection
is kept and the difference recorded. The diagnostic gives the real reason and does not
claim the import ran:

```
error: r1.f:3:1: import: script is not readable: /…/missing.f
note: the program was not executed: its imports are resolved before it runs, so no part of it ran
note: a top-level `return` at r1.f:2:1 comes before this import; legacy would end there without reaching it (exit 0)
```

Both positions are recorded offsets (the import statement and the `return`
statement). If the `return`'s offset were unavailable, the note omits the location
rather than inventing one. This replaces the phase-1 `import-error-unreached` feature
refusal, which would have misrepresented an invalid program as a missing VM feature.

### 4.3 Module program errors: `return`, `export-missing`, `export-duplicate` (Q6 ruling)

Program errors refused before running, with exit 1, legacy text, a truthful
position, no "not supported yet" wording and a stable reason id. No side effects,
including from imported modules: none is initialised.

| Reason id | Condition | Legacy text | Position |
|---|---|---|---|
| `module-error:return-value` | `return <value>` outside functions in an imported module | `return with a value is not allowed in import` (with the frame prefixes) | the importing statement, where legacy reports it (`e_ret.f:2:1`), plus `note: the \`return\` is at ret.f:1:1` |
| `module-error:export-missing` | `export(x)` where no earlier top-level binding, function or import of the module provides `x` | `export names no existing binding: x` | the `export` statement (legacy: `exm.f:1:1`, `em2.f:3:1`) |
| `module-error:export-duplicate` | a second `export(x)` | `duplicate export: x` | the second `export` (legacy: `exd.f:3:1`) |
| `module-error:export-name` | `export(<expr>)` whose argument is not a binding name (owner, 2026-11-01) | `export requires a binding name` | the `export` statement (legacy: `nonid.f:1:1`) |

`export(<non-name>)` is parsed today as an Opaque legacy-runtime-error region
(`import-opaque:legacy-runtime-error`). The graph recognises that region as an export
statement with a non-name argument before the generic opaque refusal, and treats it
as an ordinary statement for competing-error order. This applies in modules. In the
entry, `export` is the compiler's existing concern and is unchanged.

Verified legacy agreement for the static rules:
- Legacy does not hoist: `export(f)` before `fn(f())` fails in legacy too.
- A binding declared in a top-level block is not exported (`blk.f`: legacy fails).

Not covered, so these keep feature refusals:
- **Bare `return`** at a module's top level (`module-return`, renamed
  `module-bare-return`). It is legal in legacy: it ends the module, and later exports
  are not made. That is a VM gap, not a program error.

**Approved difference.** A `return <value>` that legacy never executes (in an
untaken branch, `cret.f`: legacy rc 0) is rejected. The rule is static: "a module may
not `return` a value outside its functions". The diagnostic states the rule and the
`return`'s position. It does not claim the `return` ran.

**Competing errors.** The graph reports the first program error in legacy's execution
order: depth first, statement order. A construct nested in a block counts at its
enclosing top-level statement.
- `comp.f`: a missing import before a `return 5` gives the missing import, as in legacy.
- `comp2.f`: a `return 5` before a missing import gives the `return` error, as in
  legacy, because the import is never reached.

**Determinism rule (owner).** The reported error never depends on hash-table
iteration or filesystem traversal order.

The order is legacy's first-executed order, which is well defined for everything the
graph checks:
- The graph visits units depth first, in statement order. An import is entered at its
  statement. The module behind it is checked completely (placement, its own
  statements and imports, in order) before the importer's next statement.
- Within a unit, checks run in top-level statement order. A construct nested in a
  block counts at its enclosing top-level statement. Two candidates in one statement
  are ordered by source offset.
- A module already visited is not re-checked. Its first visit's position in the order
  counts.
- Feature refusals use the same order. A clash or scope refusal inside one unit is
  reported at the smallest source offset among its candidates. This replaces the
  traversal-stack order seen on the `integ/args-cmd` binary: `m_fn.f:3:22` was
  reported where the first `args` is at `1:22`.

Implementation constraints:
- `unordered_map`/`unordered_set` (`by_identity_`, `owner`, `Names`) are used only
  for membership and lookup, never to choose what is reported.
- The graph reads only paths named by import statements and package manifests. It
  never lists a directory.

Program errors take precedence over feature refusals (an invalid program is reported
as invalid). The graph walk records the first feature refusal and continues through
every unit it can parse, to find a program error in that order. It stops at a unit
that does not parse. If no program error is found, the first feature refusal is
reported as today. With this rule, row 34 (`018-main`) reports legacy's
`package is not installed: bare` at `src/main.f:11:1`.

### 4.4 Still refused before any effect (feature refusals, unchanged)

| Reason | Why |
|---|---|
| `import-nested` | imports in functions, lambdas, methods and `try` blocks: runtime import, D-041 failure caching, transactional rollback |
| `import-dynamic` | D-037 |
| `import-reinitialised` | D-041 #2 policy; no diamond redesign |
| `import-name-clash` | no general mangling |
| `module-bare-return` | see §4.3 |
| `import-collision`, `import-scope`, `export-nested`, `export-struct`, `import-opaque:*`, `import-parse-error`, `import-limit` | unchanged |
| `module-host-op:*` | R1 |
| `callable:package_path`, `callable:module_path` | unchanged |
| `statement:Import` in sessions | D-054 |

### 4.5 `args`/`cmd` in modules (Q4/Q5 ruling; supersedes D-062 #1)

Resolution is by binding identity (lexical or module scope), never by spelling alone.
D-062 stays as historical evidence. The superseding decision is recorded as
`D-NEXT-packages` (numbered at integration).

| Case | Contract |
|---|---|
| the entry script reads the injected `args`/`cmd` | supported (D-062) |
| an imported function reads `args`/`cmd`, and static resolution proves the name is the entry binding (the module declares no `args` of its own) | supported: it sees the entry's values. If the entry replaced the binding by a top-level `args := …` before the import, it sees that (e6). It compiles as an entry function at the import's position would, so a replacement that a module function may have read earlier is refused as for entry functions (`free-name`, e10). |
| a module's top level, or code that may run while any module initialises, reads the **injected** binding | refused before any effect: `import-scope:invocation-init`, a program error at the read, with `note: reached while <module> initialises, at F:L:C` (e2, e3, e14) |
| a module declares a private top-level `args := …` / `cmd := …` (e12) | **supported, as in legacy** |
| a module reads or assigns its own private `args` | ordinary binding rules (mutable unless declared otherwise). D-062 #2's read-only rule applies only to the injected bindings. |
| ambiguous or unprovable access | refused before any effect: `import-scope:invocation-ambiguous` |

**"May run while a module initialises":**
- a read in a module's top-level code outside functions, including a top-level
  lambda's body;
- a read in a function that any module's top-level code may reach. A function is
  reachable when that code mentions it by name, as a call or a value, directly or
  through the names mentioned in the bodies of functions it mentions (fixed point).

Names are disjoint across units (`import-name-clash`), so this is exact by name and
conservative for values. It is conservative for code never executed (e13, legacy
rc 0); that is part of the recorded difference, and the wording states what was found
("an imported module reads the entry script's `args`/`cmd` while it initialises; they
are visible to its functions only after the import").

**Ambiguous or unprovable** (each refused at the first such mention, positioned):
- a module whose `args`/`cmd` declaration is not a plain top-level statement (inside a
  block at top level; legacy scopes it to the block);
- a module that both declares its private binding and has reads of the name that may
  run before the declaration does;
- `args`/`cmd` as a local, parameter or function name in a module. These are
  unchanged: `binding-name-reserved` / `param-name-reserved` / `fn-name-reserved`, as
  in the entry (D-062 open question 1).

Also unchanged:
- Struct methods reading `args` stay `free-name`.
- `cmd(...)` stays `callable:cmd`.
- Other entry globals read from a module stay `import-scope` (D-038 lexical; no
  unrestricted module-global access).

**Scope report for the private `args :=` (as requested before doing it).** It needs
no module-binding redesign, but it is not free.
- The linker splices every module into one unit, whose globals are keyed by name. The
  entry's injected `args` and a module's private `args` can therefore only coexist if
  the linker **renames that module's binding**.
- The rename gives every identifier in that module's segments, which resolve to the
  private binding, a fresh name that no unit uses. That is a narrowly scoped instance
  of the name-clash mangling the owner kept out of scope, limited to the two invocation
  names.
- It also needs a display-name hook, so that the VM messages that quote binding names
  still say `args`. Today those come from source text:
  - `compiler_const.cpp:58` (`cannot assign to const binding: <name>`);
  - `vm_array_mutation.cpp:19` (`cannot mutate const array: <name>`);
  - `compiler_call.cpp:98` (`callable argument count mismatch: <name>`);
  - refusal and diagnostic positions, through the linker's segment map (a renamed
    token maps back to its original span).
- Estimate: ~120–160 lines across `module_link.cpp`, a small `link_rename` helper, the
  segment map and those message sites, plus tests. Moderate and contained; no compiler
  scope redesign.
- **Owner ruling (2026-11-01): go ahead with the narrow rename.**
  - It covers only `args`/`cmd` and the declaring module's own binding. It is not
    general name-clash renaming; every other clash stays `import-name-clash`.
  - Messages that quote the name still say `args`/`cmd`.
  - Tested in phase 2 and left to the independent review.

### 4.6 Approved differences to record (`D-NEXT-packages`, `compatibility.md`)

1. Import-resolution errors are rejected before running (§4.2), including an import
   that a top-level `return` would leave unreached.
2. Package-lock housekeeping is the single exception to "no effects before
   execution". The lock file may be created, but only inside an existing `.nift/`;
   `.nift/` itself is never created. One lock snapshot replaces per-import
   re-validation.
3. Module program errors are rejected before running, including a `return <value>` in
   an untaken branch (§4.3).
4. `import-scope:invocation-init` is conservative for module code that is never
   executed (§4.5).

### 4.7 Owner conditions on the follow-up approvals (2026-11-01)

These bind the implementation and its review. All of it goes through this
checkpoint's independent review and Linode certification.

**C1. Narrow `args`/`cmd` rename.**
- **What is renamed.** Only the private `args`/`cmd` bindings that a module declares
  itself (§4.5), and only internally in the linked text.
- **Names.** The original names are kept in every diagnostic, error message and
  source report.
  - Positions map back through the segment map to the original span.
  - The display-name hook supplies `args`/`cmd` to every message that quotes a
    binding name.
- **No collisions.** The generated name must not collide with any declaration or use
  in any unit, or with any other linker-generated name.
  - It is chosen against the union of all units' identifiers and the linker's
    generated names, and checked.
  - A collision that remains is an internal refusal, never a silent rebinding.
- **Shadowing and nested imports.**
  - Lexical shadowing is unchanged. A function parameter or local named `args`
    stays `param-name-reserved` / `binding-name-reserved`, as everywhere, so no
    shadow inside a renamed module can be missed.
  - Each module's rename is decided by that module's own top-level declaration, so
    resolution stays correct across nested imports. A module without its own
    declaration still refers to the entry's binding (§4.5), even if it imports, or is
    imported by, a module that has one.
- **Unchanged:** the entry script's injected `args`/`cmd`; module functions reaching
  the entry's bindings where resolution proves it; the pre-effect refusal of
  unsupported init-time access (`invocation-init`, `invocation-ambiguous`).
- **Size stop.** If the rename work runs well beyond ~200 lines, stop and report
  before continuing.

**C2. `PackageTransaction` extension.**
- **One function.** One narrow, clearly named VM-facing function on
  `PackageTransaction`: `acquire_read_existing_store(std::string& error, bool&
  store_present)`. There is no configuration abstraction.
- **Reuse.** It reuses the existing lock implementation and primitives
  (`open_and_lock`, `lock_identity_valid`, the journal check). No locking algorithm
  is copied into `src/rt`.
- **Locking guarantees.** Atomic create-or-open of the lock file inside an existing
  `.nift/`, correct concurrent access, release on every path (destructor).
- **Legacy unchanged.** Legacy call paths and behaviour stay exactly as they are,
  verified by focused regression tests (§6). D8 rules out changing legacy behaviour,
  not extending shared infrastructure.
- **Independence.** The package code stays independent of VM internals.
  - `PackageTransaction`, `PackageMetadata.h` and `PackageGraphLock.h` include
    nothing from `src/rt`.
  - The dependency runs one way: `src/rt/package_resolve` calls the shared package
    code.
  - This lets the infrastructure survive the eventual removal of legacy without
    duplication. It does not authorise any CP25-L work now.

**C3. `export(<non-name>)`.**
- **Classification.** A program error, `module-error:export-name`, with legacy's text
  `export requires a binding name`.
- **Position.** It is reported at the actual `export(...)` expression (the statement
  start, which is also where legacy reports it: `nonid.f:1:1`), with `positioned`
  set.
- **Effect.** Exit 1, rejected before any effect.
- **Classification in the inventory.** It is classified as a program error in the
  inventory and `classification.tsv`, not as `import-opaque`.
- **Valid exports.** Valid `export(name)` statements are unaffected.

## 5. Implementation plan

The work starts after `integ/args-cmd` is in `weekend/r2`, and is rebased onto it.
`cand/coro-module` does not touch these files. `cand/collections` changes one line of
`script_runner.cpp`.

| File | Change | Size |
|---|---|---|
| `src/PackageTransaction.{h,cpp}` (owner, 2026-11-01) | A separate, **VM-only** read-lock function, `acquire_read_existing_store(error, bool& store_present)`. It reuses `open_and_lock` (lock file created if absent, never truncated), `lock_identity_valid` and the journal check, and never creates `.nift/`. Additive only: legacy's `acquire_read`, `acquire` and every legacy call path stay byte-identical; the diff adds lines and changes none. | ~25 |
| `src/rt/package_resolve.{h,cpp}` (new) | Resolver: `resolve_package(name, project, PackageSnapshot&) -> {entry, root, error text}`. It uses `PackageMetadata.h`/`PackageGraphLock.h`/`FileSystem` and holds the RAII `PackageSnapshot`. | ~170 |
| `src/rt/module_graph.{h,cpp}` (+ `module_graph_errors.cpp` for rt-lint's 400 lines) | Package branch, `package_root`, containment. `ModuleGraph::error {id, message, frames, where, offset, positioned, notes}`. The walk continues past feature refusals; the first program error is found in execution order. Return-value, export-missing and export-duplicate become program errors; bare return becomes `module-bare-return`. The entry-`return` note. | ~150 |
| `src/rt/module_link.{h,cpp}`, `rt/module_invocation.{h,cpp}` (new), `rt/link_rename.{h,cpp}` (new) | Graph-error propagation; per-module frame spelling; D-062 #1 lift (`invocation-init`, `invocation-ambiguous`); the private-`args` rename and segment map. | ~60 + ~120 + ~80 |
| compiler and VM message sites (3) | display names for renamed bindings | ~30 |
| `src/rt/script_runner.cpp`, `refusal_text.cpp` | program-error rendering (§4.2, §4.3) and prefixes. `script_runner.cpp` is ~380 lines after integ, so a helper moves out. | ~60 |
| `src/rt/refusal_inventory.cpp` | Builds the same graph (F12). Program errors are listed by id, not as fallbacks. | ~15 |
| `Makefile` | `src/JsonFile.cpp` and `src/PackageTransaction.cpp` (and its dependencies) into `RT_VALUE_SOURCES`; `test-runtime2-packages`. | ~10 |
| tests | §6 | ~700 |
| docs | this file; `D-NEXT-packages` in `decisions.md`; `compatibility.md`; `classification.tsv` (`import-package` closed; `import-missing`/`import-cycle`/`export-missing`/`export-duplicate`/`module-return` become program-error ids, not fallbacks; new `invocation-init`/`invocation-ambiguous`/`module-bare-return` rows); refusal register. | — |

Production total is about 700–750 lines in ~14 files, including the private-`args`
rename (~200 of it).

**Risks.**
- Linking `PackageTransaction.cpp` into the rt test binaries (its dependencies).
- Lock lifetime across the graph build and its release on every path.
- Walking past feature refusals without exploring an inconsistent graph.
- Competing-error order.
- The rename's display names and segment map.
- rt-lint size limits.
- Windows path case against containment.
- Over-promising: 2 of 23 package units run fully on the VM.

## 6. Tests (phase 2)

**Differentials.** `tests/runtime2/packages/packages.py` runs each workspace in both
engines and compares stdout, the first stderr line (text and `file:L:C`) and rc. The
only approved deltas are §4.6. Cases:
- every §2 step (§9 `lg/*`), each with prefix spelling and nesting depth;
- cwd vs script directory;
- package in package, missing and present;
- package-relative helper;
- `../` and symlink escapes;
- absolute import from a package;
- runtime error in a package (legacy timing kept);
- mixed `@import`/`import` frames;
- cycle and self-cycle;
- missing and nested missing;
- the corpus shapes.

**Pre-effect and position regressions (Q1, Q6).** Each program-error case has an
earlier statement that would print and write a marker file. Before the failing
import or module, the entry also imports a successfully loading module whose
top level prints and writes a marker. The test asserts:
- no stdout, no marker files, rc 1;
- the exact `file:L:C`, and `positioned` (no `no exact position`, never `1:1` unless
  the statement is there).

Cases:
- a missing package, a missing file, a cycle, an escape;
- `return 5`, `export(nope)` and a duplicate export, each at depth 1 and depth 2+;
- competing errors (`comp.f`, `comp2.f`, two missing imports, a program error after
  a feature refusal);
- the entry-`return` note (`r1.f`).

**Required adversarial test 1 (owner): a missing package leaves a valid lock.** A
project has `.nift/` and a lock file holding known bytes. The entry prints and writes
a marker, then imports a package that is not installed. The test asserts:
- the VM fails before the earlier statements: no stdout, no marker, rc 1;
- the lock file still exists with byte-identical contents and the same mode;
- it is unlocked: `flock -n -x` succeeds at once;
- a following legacy run and a following VM run can both take it.

Repeat with no lock file: it is created empty, mode 0644, and the rest holds. Repeat
with no `.nift/`: nothing is created.

**Required adversarial test 2 (owner): concurrent resolution, one protocol.**
- **Readers.** N VM processes and N legacy processes resolve the same package
  concurrently in a loop, from a cold start with no lock file (concurrent creation).
  All succeed. Exactly one regular lock file exists, with its contents preserved.
  There is no deadlock (overall timeout).
- **Writer.** Legacy's writer path (`nift add <local package>` / `nift remove`, which
  takes the exclusive transaction lock in `PackageTransaction::acquire`) runs in a
  loop against VM readers.
  - Each VM run either waits and then sees a consistent installed state (old or new,
    never partial: manifest, entry and lock agree), or reports a §2 error for the
    state it saw.
  - No run hangs past the timeout.
  - `nift` package state is valid afterwards (`nift` package listing and lock agree).
- **Held lock.** An external `flock -x` is held for 3 s while a VM run starts. The VM
  run waits and then succeeds, like legacy.
- **Journal present.** `.nift/package-transaction.json` gives the transaction error,
  before anything runs.

**Lock acquisition failure (owner).** Cases:
- an unopenable lock (a directory named like the lock file);
- a symlinked lock file (`O_NOFOLLOW`);
- the journal present;
- a lock identity change.

Each gives `import-error:package-lock`, legacy's text in the program-error format (no
"not supported"), rc 1, nothing run.

**Deterministic competing failures (owner).** Fixtures have several possible program
errors across the graph, for example:
- module A has `export(1)` and module B's import is missing;
- a cycle, a missing package and an `export-missing` in sibling modules;
- a feature refusal next to a program error.

The test creates the module files in every permutation of creation order (and of
directory-entry order, by re-creating the directory) and permutes the import-name
lengths, which perturbs hash bucketing. Over the repetitions it asserts one identical
diagnostic (text, `file:L:C`, reason id), equal to legacy's first-executed error
where legacy reports one. A mutant that picks the reported error by hash-set
iteration must fail it.

**Lock failure cleanup.**
- After success, after each program error, after a feature refusal, and after a
  killed run (SIGKILL while waiting), the lock is free (`flock -n -x` succeeds).
- No file other than the lock is created anywhere under the project (tree diff).
- The lock file's contents are preserved in every case.

**`args`/`cmd` (§4.5).**
- Equal to legacy: e1, e4–e9, a module with a private `args :=` read at top level and
  in its functions (e12), and a module function next to a private-args module
  (identity, not spelling).
- Refused before effect, with marker controls:
  - e2, e3, e14 (`invocation-init`), including the nested-import variant;
  - e10 (`free-name`);
  - block-scoped and read-before-declare private `args` (`invocation-ambiguous`);
  - a module local or parameter named `args`.
- e13 is recorded as the conservative difference.
- Error texts that quote names say `args`, never the renamed form.

**Mutants.** Each must make a test fail:
- the script directory as project;
- `.nift` creation;
- `O_TRUNC` on the lock;
- the lock not released on the error path;
- no lock taken;
- a missing per-frame prefix or wrong spelling;
- `args` visible at module top level;
- the init closure not following value mentions;
- resolution by spelling (a private `args` treated as the entry's);
- no package-relative containment;
- feature refusal preferred over a program error;
- statement order of competing errors reversed.

**Inventory re-collect.** Rows 1–31 and row 34 leave the fallback list as program
errors; the other import rows are unchanged.

**Legacy package behaviour unchanged (owner, 2026-11-01; C2).**
- Focused regression: legacy's `acquire_read`/`acquire` call paths are unchanged.
  - `.nift/` and the lock are still created by a legacy failing import.
  - The exclusive writer path still blocks readers.
  - The journal error is unchanged.
- An include check: no `src/rt` header is reachable from `PackageTransaction.*`.
- The `PackageTransaction` diff is additive: a review check asserts that no existing
  line changes.
- All of these pass under `NIFT_VM=0`, before and after the change:
  - `make test-v44-packages` (`package_transaction_smoke.py`,
    `package_hardening_smoke.sh`, …);
  - `package_metadata_smoke.sh`, `package_refs_smoke.sh`,
    `package_tools_dogfood.sh`, `package_combined_dogfood.sh`,
    `package_module_export_smoke.sh`, `package_callable_closure_smoke.sh`;
  - the `package_graph_*_unit` tests.
- Legacy outputs for the §9 `lg/` probes are compared before and after.

**Narrow rename (§4.5, C1).** Required tests (owner):
- Two imported modules each declare their own private `args`. Neither sees the
  other's value, and neither picks up the entry's `args`. The entry's `args` is
  unchanged.
- A module declares a private `args` and exports a function that references it.
  After import, the function still reads its own module's binding, when called from
  the entry and from another module.
- Nested imports:
  - a private-`args` module importing a module without one, which reads the entry's
    binding;
  - the reverse;
  - re-export through a middle module.
- Exports of the private `args` binding itself (D-046 #2 location semantics).
- Shadowing:
  - the entry's top-level `args := …` beside a private module `args`;
  - parameter and local forms stay refused.
- Reassignment of the private binding at top level and in its functions. It is
  ordinary and mutable; the injected bindings stay read-only (D-062 #2).
- Generated-name collision: a unit that already uses the would-be generated name
  forces a different choice. A planted collision is refused, not rebound.
- Mutation controls, each of which must fail a test:
  - renaming by spelling (entry-binding reads in a non-declaring module also
    renamed);
  - renaming the entry's binding;
  - leaking the rename into another module;
  - a fixed generated name;
  - missing display names.

- A private `args` is read and assigned at top level and in the module's functions,
  next to a module that reads the entry's `args`.
- Errors that quote the name (const assignment, const-array mutation, arity, refusal
  positions) say `args` and point at the original span.
- A mutant that renames by spelling, or that extends the rename to another clashing
  name, must fail.

**`export(<non-name>)` (C3).**
- Arguments: a literal (`export(1)`, `export("x")`), a call (`export(f())`), a member
  (`export(o.x)`), an arithmetic expression (`export(x + 1)`), and a lambda.
- Each gives legacy's text at legacy's position, depth 1 and 2.
- Pre-effect checks: marker files and output from earlier entry statements and from
  previously imported modules must not appear.
- It competes in statement order with a missing import and with `export-missing`.
- Valid `export(name)` forms (a binding, a function, a re-exported import) still
  pass unchanged.
- It competes in statement order with a missing import.
- Pre-effect markers as above.

**nift-regression-suite.** `contract/v44_packages_smoke.sh`,
`v46_import_ownership_smoke.sh`, `v46_package_graph_smoke.sh`,
`v48_package_combined_fixture_smoke.sh` and `v45_integration_smoke.sh`, with the VM
as default.

## 7. Owner rulings (2026-11-01) and remaining questions

| Question | Ruling |
|---|---|
| Q1 | Import-resolution errors are rejected before running; scope limited to whole-program execution (§4.2) |
| Q2 | Create (inside an existing `.nift/` only) and use legacy's lock file; the single allowed housekeeping exception (§4.1) |
| Q3 | Legacy texts, including `file does not exist` |
| Q4/Q5 | Identity-based `args`/`cmd`; a private `args :=` is supported; ambiguity is refused (§4.5) |
| Q6 | Module `return <value>`, `export-missing` and `export-duplicate` are program errors; the entry-`return` case is kept pre-execution, with the real reason (§4.2, §4.3) |

| Follow-up 1 | Narrow rename for a module's private `args`/`cmd` approved (§4.5) |
| Follow-up 2 | VM-only, non-creating read-lock function in `PackageTransaction`; legacy byte-identical, with legacy package tests run under `NIFT_VM=0` (§5, §6) |
| Follow-up 3 | `export(<non-name>)` is a program error in this checkpoint (§4.3) |
| Conditions | Detailed conditions C1–C3 on the three follow-ups (§4.7); their required tests are in §6 |
| Final points | Deterministic competing-failure rule (§4.3, §6); lock failure is a package-management error with its own id `import-error:package-lock` (§4.1, §4.2). No further scope changes. |

No open questions remain. Implementation waits for the coordinator's go-ahead after
`args`/`cmd` lands in `weekend/r2`.

## 8. Not done in phase 1

No production source was changed. No tests were added yet. Nothing was pushed.
The owner rulings of 2026-11-01 are folded into §4–§7; the decision is recorded as
`D-NEXT-packages` in `decisions.md` (numbered at integration).

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
| `q6/` | module `return`/`export` probes (hoisting, blocks, bare and conditional `return`, competing errors) |
| `ac/` | `args`/`cmd` probes (e1–e14, s1, s2; VM side run with the `integ-argscmd` binary) |
| `prior_units.json` | reasons from the inventory |
