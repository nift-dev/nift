# CLI unification opinion + migration-surface inventory

Answer to the five CLI-design questions (analysis only, no implementation).

## A. Is `nift build [--all|--names|--auto|--repair]` a cleaner grammar?

Yes, clearly. The current surface is five top-level verbs with `build` already
overloaded (bare = incremental, positional names = selected):
`build`, `build-all`, `build-updated`, `build-names`, `build-auto`. The
unified form makes `build` the action and `--all/--names/--auto/--repair` the
execution mode, which resolves the existing `build` overloading rather than
adding to it, and gives `--repair` a coherent conceptual home (action vs
mode). `build-updated` in particular was an awkward spelling for what is
simply the default incremental operation.

## B. Any semantic or parsing reason not to make the modes mutually exclusive?

No semantic reason — the modes are genuinely mutually exclusive (all vs
selected vs continuous vs distrust-repair). Exclusivity is correct and should
error before any project open or side effect.

Parsing notes (not blockers, to be handled in implementation):
- The current option parsing is manual and index-based (`has_option`,
  `options_only`, `valid_options` around argv[2]). `--names` takes a
  variable-length argument list (names until the next `-`), which needs a
  small extension of that parser; no getopt dependency is warranted.
- `-p` (print/explain reasons) is orthogonal and must remain combinable with
  any mode (`nift build --all -p`).
- Decide: positional names under bare `build` (`nift build foo`) are removed
  (error: use `--names`), consistent with "remove old spelling, no aliases".
- `--auto` is a long-running loop; the exclusivity check must run before it
  starts.

## C. Existing build-* commands missed?

The complete current family is `build-all`, `build-updated`, `build`,
`build-names`, `build-auto` (CLI.cpp:723-728 project_commands set, 735
timed_command, help rows 278-281, build_auto_log_path). No hidden build-*
verbs. Related surface: `build-auto` also writes `.nift/build-auto.log` (a
derived bookkeeping file - note it under the marker protocol); `nift commands`
help lists the verbs; the `unknown command` message (CLI.cpp:730) enumerates
them. The non-build mutating commands (`untrack`/`rm`/`del`/`cp`/`mv`) were
already flagged as needing the exclusion protocol in the design review.

## D. Internal migration surface (actual inventory)

### Code
- `src/CLI.cpp` - dispatch (723-800), help rows (278-281), project_commands
  set, timed_command, build_auto_log_path, error/help strings; new
  `--all/--names/--auto/--repair` parsing + exclusivity error + repair
  acquisition.

### Tests/scripts invoking the CLI (must change)
- `build-all` (as "build the project"): ~37 files:
  benchmarks/performance_10k.py, benchmarks/perf_uc_audit.py,
  benchmarks/perf_regression_audit.py, tests/full_build_scaling_benchmark.py,
  tests/memory_10k_benchmark.py, tests/cross_feature_smoke.sh,
  tests/pagination_smoke.sh, tests/crash_recovery_adversarial.py,
  tests/pathto_404_smoke.sh, tests/template_optional_smoke.sh,
  tests/path_security_smoke.sh, tests/json_binding_smoke.sh,
  tests/persistence_concurrency_failure_smoke.sh,
  tests/parser_value_composition_adversarial.py,
  tests/incremental_state_transitions_adversarial.py,
  tests/pagination_sanitizer_smoke.sh, tests/comments_smoke.sh,
  tests/complexity_invariants.py, tests/metadata_safety_smoke.sh,
  tests/incremental_new_features_smoke.sh, tests/config_validation.sh,
  tests/minify_integration_smoke.sh, tests/parser_content_smoke.sh,
  tests/json_schema_integration_smoke.sh, tests/contracts_smoke.sh,
  tests/requirements_smoke.sh, tests/path_safety_smoke.sh,
  tests/control_flow_smoke.sh, tests/filesystem_boundary_adversarial.py,
  tests/pagination_incremental_equivalence.py,
  tests/init_scaffold_functional_truth.py, tests/output_permissions_smoke.sh,
  scripts/checkpoint9_parser_fuzz.py, scripts/checkpoint7_incremental_equivalence.py,
  scripts/checkpoint4_large_project.py, scripts/bh3_guard_mutation.py,
  scripts/checkpoint8_filesystem_transaction.py,
  scripts/checkpoint4_watch_endurance.py, scripts/bh_repair_battery.py,
  scripts/checkpoint6_integration.py, scripts/checkpoint10_cross_platform.py,
  scripts/checkpoint3_core_memory.py.
  Mapping: `build-all` -> `build --all`; incremental fixtures map to bare
  `nift build`.
- `build-updated` (incremental): ~17 files (benchmarks/performance_10k.py,
  tests/cross_feature_smoke.sh, tests/template_optional_smoke.sh,
  tests/persistence_concurrency_failure_smoke.sh, tests/memory_10k_benchmark.py,
  tests/incremental_new_features_smoke.sh, tests/minify_integration_smoke.sh,
  tests/parser_content_smoke.sh, tests/json_schema_integration_smoke.sh,
  tests/contracts_smoke.sh, tests/requirements_smoke.sh,
  scripts/checkpoint7_incremental_equivalence.py,
  scripts/checkpoint4_large_project.py, scripts/checkpoint8_filesystem_transaction.py,
  scripts/checkpoint6_integration.py, scripts/checkpoint10_cross_platform.py,
  scripts/checkpoint3_core_memory.py). Mapping: bare `nift build`.
- `build-names`: tests/requirements_smoke.sh:76 (`build --names /`).
- `build-auto`: scripts/checkpoint8_filesystem_transaction.py,
  scripts/checkpoint4_watch_endurance.py (`build --auto`).

### Conformance / differential
- tests/conformance/run_conformance.py (build-all at :92,:142) and
  gen_golden.py (:41) - `build --all`.
- nift-rs is unaffected at the code level: nr6_differential.sh invokes the
  C++ ENGINE harness (embedding API), not the CLI. Only nift-rs
  docs/semantic-inventory.md and docs/authorities.md mention the verb names as
  semantic references (doc updates).

### CI
- No .github/workflows file invokes build-* directly; all go through the
  scripts above, so CI changes are indirect (update the scripts, workflows
  follow).

### Documentation / generated material
- README.md (:71-83 command examples), PERFORMANCE.md, HANDOVER.md,
  docs/handover/* (ECOSYSTEM-HISTORY, TESTING, CODEX-CHECKPOINT-10,
  PARAMETER-INTERPOLATION-IMPLEMENTATION, PERF-REGRESSION-AUDIT,
  UNFINISHED-MARKER-DESIGN-REVIEW, BATTLE-HARDENING-2, CAMPAIGN-LEDGER, EMBED,
  RELEASES), docs/guarantees/registry.json, docs/evidence/*.json (evidence
  records embedding verb names - decide whether historical evidence is
  rewritten or left as a record).
- src/handover_content.h (init --handover material): already uses only bare
  `nift build` and `nift status` - no change needed for the generated content.

## E. Does bare `nift build` stay incremental?

Yes. Incremental is the normal, most common operation and should be the
zero-friction default; `--all/--names/--auto/--repair` are the explicit modes.
No `--updated` (redundant with the default). This already matches the embedded
handover, which instructs daily `nift build`.

## Migration note

~55-60 files across tests/scripts/benchmarks/docs touch the four verb
spellings. Mechanical mapping: `build-all` -> `build --all`, `build-updated`
-> `build`, `build-names X` -> `build --names X`, `build-auto` -> `build
--auto`, plus help/error/unknown-command text and the new `--repair` mode.
The hidden hazard is scripts that assert on CLI error/output text mentioning
the old verbs; those must be updated in the same pass.

## Final grammar revision (names-as-mode; `--names` removed)

Supersedes the `build --names` form in the section above. Confirmed from the
current code: `info` ALREADY accepts positional tracked names directly
(CLI.cpp:1000-1019: `if (command == "info" && argc > 2)`), so `build` is the
one being normalized to match it.

### Final surface

```
nift build              incremental build of whatever is stale
nift build /            build only index
nift build about blog   build only those named pages
nift build --all
nift build --auto
nift build --repair

nift info
nift info /
nift info about blog
nift info --all
nift info --watching
```

### Mode exclusivity (names are themselves a mode)

build modes: positional names / --all / --auto / --repair
info modes:   positional names / --all / --watching

More than one mode is a hard error, before any project open / side effect:

```
nift build / --all          error: build modes are mutually exclusive
nift build about --repair   error
nift info / --all           error
nift info about --watching  error
nift build --all --repair   error
```

`-p` stays orthogonal (combinable with any mode).

### Factual notes / open decisions to confirm

1. `info` bare currently returns complete metadata for EVERY tracked entry
   (CLI.cpp:1021-1033); `info --all` is therefore an explicit spelling of the
   same all-entries view, not a distinct mode. That is fine for grammar
   symmetry (`build` bare = incremental vs `--all` = full is a real
   distinction; for `info` the two coincide). Document the equivalence rather
   than inventing a different "normal" info view.
2. `info-names` (list of tracked names, CLI.cpp:992-997) is a distinct query,
   not a mode of `info`. Recommendation: keep it as a top-level verb
   (tests/tracking_scaling_benchmark.py:36,:55 depend on it).
3. `info-tracking` (CLI.cpp:1022-1032) and `status` (CLI.cpp:910) are separate
   queries with no in-repo `info-tracking` invocation; `status` stays as
   `nift status` (23 files depend on it). Recommend keeping both as-is; if
   `info-tracking` is ever folded it would be `info --tracking`.
4. `nift build /` is compact and unambiguous: `/` is the tracked index name.

### Updated migration mappings

```
build-all X  ->  build --all        (~37 script/test/benchmark files)
build-updated -> build              (~17 files)
build-names X  ->  build X          (tests/requirements_smoke.sh:76: `build /`)
build-auto  ->  build --auto        (scripts/checkpoint8_filesystem_transaction.py,
                                     scripts/checkpoint4_watch_endurance.py)
info-all    ->  info --all          (scripts/checkpoint3_core_memory.py:61)
info-watching -> info --watching    (no in-repo invocations)
info-names  ->  unchanged           (tests/tracking_scaling_benchmark.py:36,:55)
status      ->  unchanged           (23 files)
```

The rest of the inventory in section D (docs, CI-indirect, conformance
runner/gen_golden using build-all, src/handover_content.h already using only
bare `nift build`, nift-rs unaffected at code level) is unchanged. The hidden
hazard remains scripts asserting on CLI error/output text naming the old
verbs (`unknown command`, `commands` help, `info-all`/`info-names` spellings).

### Answer to the reviewer's symmetry point

Agree: `build /` and `info /` being symmetric is a small, intentional polish
and the cleanest version so far. No `--names` flag. Positional names as a
mutually exclusive mode is correct and mirrors how `info` already behaves.
