# CP03 — Filesystem freedom (decision D-002)

Status: see `../progress.md`.

## Objective
Nift programs (page renders, `@script`, build hooks, standalone scripts) use
ordinary OS filesystem semantics: absolute paths, `..`, symlinks and
neighbouring repositories work. External files are tracked as dependencies.
Nift-owned state stays protected. Restrictions are opt-in only.

## What changed
- `ResourcePathAuthority::enforce_project_root` defaults to `false`. The nine
  expression-level checks (`ParserExpression.cpp`) and the script import check
  (`ParserScript.cpp`) test that flag instead of "project build and non-empty
  root". Project builds no longer set it.
- Template directives no longer refuse paths outside the project: `@markup`
  path and includes, `@json` data and schema, `@dep`, pagination template and
  separator (`ParserTemplate.cpp`).
- Project configuration: contract files, schemas and taxonomies may live
  outside the project (`ProjectRead.cpp`, `Parser.cpp`).
- Page-info: `metadata_path_is_safe` no longer rejects external dependency or
  requirement paths. External paths are spelled absolute (CP02
  `nift::observe::spell`) and checked like any other dependency.
- `NIFT_HOOK_*` names are not recorded as environment observations (they are
  per-invocation facts supplied by the build; recording them caused perpetual
  rebuilds in `test-v410-build-pipeline`).

## What is retained
- `--fs-root` (and the embedding API's equivalent) remains an explicit,
  opt-in restriction. It is a policy, not a security sandbox.
- `@path`/`@pathto` URL containment: generated URLs must name files inside
  the project. Because those targets are recorded as requirements under the
  output tree, `build_reasons` now flags a requirement under the output tree
  that resolves outside the project ("required output path now resolves
  outside the project: …"); the rebuild then reports the `@path` error. This
  replaces the old blanket "invalid requirement" rejection for that case.
- Nift-owned state: `.nift/` metadata writes, managed outputs, `rm`/untrack
  cleanup only touch Nift-managed paths (checked in `fs_freedom.sh`).

## Contract change (approved break, D-002)
Tests that encoded the old confinement contract were changed to assert the new
behaviour. Each change keeps a test of the same scenario:

| Test | Old assertion | New assertion |
| --- | --- | --- |
| `path_safety_smoke.sh` | `@json`, schema, `@dep` via symlink to outside refused | build succeeds, external JSON value rendered; `@path` symlink check unchanged |
| `json_schema_integration_smoke.sh` | `../outside.json` data/schema refused | both build |
| `markup_json_directives_smoke.sh` | `@markup "../outside.md"` refused | external file renders; missing external file still fails |
| `contracts_smoke.sh` | contract via `..`/symlink refused | project opens and builds (fixture gained a template) |
| `requirements_smoke.sh` | external req/dep in page-info "invalid" | evaluated: up to date, then "required path missing" / "dependency changed" |
| `metadata_safety_smoke.sh` | external paths "invalid"; retargeted `@path` symlink "invalid requirement" | external paths checked normally; retarget reports "required output path now resolves outside the project" and the rebuild fails with the `@path` error |

## Tests
- `tests/runtime2/fs_freedom.sh` (new, 16 checks): absolute, `..` and symlinked
  reads; external `@json`/`@markup`; `exists`/`ls` outside; script writes
  outside; no perpetual rebuild; external edit and external glob change
  rebuild; standalone absolute read; `--fs-root` still restricts; `rm` leaves
  external files alone.
- Full `make -k test`: see Evidence in `../progress.md`.

## Residual risk
- `exists()` on a symlink under the output tree that points outside the
  project now triggers a rebuild on every status check. This is the price of
  keeping the `@path` invariant without a separate record of `@path` targets;
  CP16 (Host ops) will record `@path` targets distinctly and remove it.
- Embedders that relied on the default confinement must opt in with
  `--fs-root`/the host policy. Release notes must say so.

## Performance
No new filesystem calls on the no-op path except one `lexically_relative` per
requirement and one `path_within` for requirements under the output tree.
