# CP12 phase B — modules: legacy semantics (probed) and the VM design

Status: probes recorded, semantics drafted; implementation authorised by the
owner (decision 2) once these semantics are verified — not started. Probes:
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
| Values | an exported variable is the module's binding value at the end of the module (copied: pushing to it in the importer is local) | `export_copy` |
| Module lexical environment | exported functions resolve the module's own top-level bindings and private functions through the module environment as it is **at the end of the module** (a binding declared after `export` is visible); never through the caller (a caller's local of the same name is not seen; an unbound name is an error even if the caller has it) — modules are **lexically scoped** already | `module_private`, `module_private_late`, `module_vs_caller_local`, `module_free_unbound` |
| Collisions | importing a name that already exists in the importer (binding in any active scope, function, struct) is an error, `export collides with existing binding: <name>`, after the module ran (its side effects happened); this makes a repeated import of the same module an error | `export_collision`, `repeated`, `same_file_two_spellings`, `in_function` (second call) |
| Import in a function | runs at each call; the exports go to the global tables, so the second call collides | `in_function` |
| Errors | missing file: `script is not readable: <abs path>` at the import statement (recoverable diagnostic `io.import_source_unreadable`); cycle: `script import cycle through <abs path>` reported inside the innermost module with one `import: ` prefix per active import; an error inside a module is reported at the module's position with an `import: ` prefix per level; `export(x)` naming nothing: `export names no existing binding: x` at the export; `return <value>` in a module: `return with a value is not allowed in import` at the import (bare `return` ends the module normally); missing package: `package is not installed: <name>` | `missing`, `cycle`, `self_cycle`, `module_error`, `export_missing`, `module_return`, `module_bare_return`, `package_missing` |
| Rollback | a failed import rolls back every resource it created (modules, lambdas, structs, handles, files) before the error propagates | source |
| Dependencies | a successfully read module is added to the build's dependencies (`result_.dependencies`); a missing module is not added by the import itself (the failure fails the page) — relevant to templates (CP24), not to standalone scripts | source |

Quirks for the register (not fixed here): exports land in the innermost
scope for variables but the global tables for functions (L42); every import
re-runs the module and repeated imports collide (L43); the `import: `
prefix repeats per level (L44).

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
* **Compile once, initialise per import.** Each distinct module (by canonical
  identity) is parsed and compiled once per program graph; every `import`
  statement is a call of that module's initialiser proto, which runs the
  module top level again in a fresh environment, exactly as legacy's
  per-import execution (`diamond`: twice). Compilation caching never changes
  initialisation.
* **Exports** become stores at the import site: functions bound statically
  (the importer's function table gets the module's protos, closed over that
  initialisation's module environment); variables declared in the importer's
  current scope. A collision that can happen is refused statically
  (`import-collision`: legacy fails after running the module); since a repeated
  import of the same module always collides, it is refused too.
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
