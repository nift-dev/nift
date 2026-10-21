# CP12c slice 0: the refusal inventory follows compiler linking

**Status:** local slice on `cp12c-base` (`e1f03028`), not integrated.
**Scope:** test tooling only (`NIFT_VM=classify-all`); no compiler, linker,
VM or run-mode change.

## Defect

`inventory_refusals` parsed and compiled the **unlinked** entry source.
`run_unit` (`src/rt/script_runner.cpp`) first calls `link_unit`; a link refusal
is the unit's first refusal, and a successful link is what gets compiled (with
`CompileLimits::module_names`). The inventory therefore reported
`statement:Import` for imports and `callable:<module name>` for functions the
linked unit provides. On `e1f03028` the shared corpus had 80 first-reason
mismatches (69 curated, 11 rt-diff generated). Confirmed before changing
anything with `cp12b_inventory.py collect --only shared` and `analyse`.

## Correction

`inventory_refusals(source, source_path)` now mirrors `run_unit`:

1. parse the entry; an entry parse error or Opaque node stops before linking,
   exactly as the runner does;
2. call `link_unit` for the entry;
3. if linking refuses, item 0 is the link reason at its module offset
   (`module-return` and `export-struct` point into the module); no compile
   follows, so `inventory_complete` is false and `inventory_stop` names the
   reason;
4. if linking succeeds, parse the linked text and inventory that AST with the
   same `CompileLimits` (`module_names`), masking and recompiling as before.
   A linked-text parse/Opaque failure is the runner's `import-link` invariant;
   the inventory reports it as incomplete rather than inventing blockers;
5. map every item offset with `LinkedUnit::locate` to its owning module and
   source offset. Items from imported modules carry a `path` field; entry
   items keep the old shape (empty path). `line`, `column` and `text` are
   computed in the item's own source.

`InventoryItem` gained optional `path`, `line`, `column` and `text`; the stats
JSON keeps every existing key. `cp12b_inventory.py` now reports how many
blocker items came from imported modules.

Later blockers are still inventoried when a linked unit compiles far enough
to refuse. A link refusal cannot be compiled past, so the inventory is marked
incomplete instead of silently dropping the rest.

## Regressions

`tests/runtime2/vm_inventory_links.py` (new target
`test-runtime2-inventory-links`, added as a prerequisite of `test-rt-vm`)
covers one shape per corrected category:

- link refusals: `import-package`, `import-nested`, `import-missing`,
  `import-dynamic`, `import-collision`, `module-return`, `export-struct`;
- module-name reasons: `callable` (the module's `getenv` is now first, not
  the module function name), `call-binding`, `fn-flags`;
- a linked unit with a later blocker in the entry, and a blocker inside an
  imported module;
- module path and source-position attribution, complete inventories where
  the unit compiles, and explicit incomplete inventories for link refusals.

The negative control is a second link of `nift` with
`-DNIFT_INVENTORY_MUTANT=1` on `refusal_inventory.cpp` only. That object
deliberately skips `link_unit`; the same tests must detect it in every case.
Normal builds contain none of the mutant code. A compile or no-statistics
failure is a harness failure, never a detection.

## Evidence

Before: shared 80 first-reason mismatches (69 curated, 11 rt-diff
generated) and projects 8. After: shared 0, curated 0, rt-diff generated 0,
projects 0, generator 0. See
`.claude-linode-runs/deepseek/cp12c-s0-inventory/REPORT.md` for the full
analysis and gate logs.
