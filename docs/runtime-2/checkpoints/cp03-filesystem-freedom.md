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
  requirement paths. External dependencies are stored relative to the project
  (`../x`, as before); requirements and absences observed by CP02 are stored
  absolute. Both are checked like any other dependency.
- `NIFT_HOOK_*` names are not recorded as environment observations (they are
  per-invocation facts supplied by the build; recording them caused perpetual
  rebuilds in `test-v410-build-pipeline`).

## What is retained
- `--fs-root` (and the embedding API's equivalent) remains an explicit,
  opt-in restriction. It is a policy, not a security sandbox. The parser
  checks the flag before any filesystem call; unrestricted builds pay only a
  boolean test.
- Package and metadata integrity: package entries must not escape their
  package; `.nift` metadata, custom build output and watch-list scoping keep
  their checks.
- Nift-owned state: `.nift/` metadata writes, managed outputs, `rm`/untrack
  cleanup only touch Nift-managed paths (checked in `fs_freedom.sh`).

## Contract change (approved break, D-002)
Tests that encoded the old confinement contract were changed to assert the new
behaviour. Each change keeps a test of the same scenario:

| Test | Old assertion | New assertion |
| --- | --- | --- |
| `path_safety_smoke.sh` | `@json`, schema, `@dep` via symlink to outside refused | build succeeds, external JSON value rendered; `@path` to an external symlink is an ordinary target (CP03 R2) |
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

## CP03 R2 — remove default containment overhead (2026-10-23)

The remaining default checks were removed:

- `ProjectInfo::build_reasons` no longer canonicalises every requirement under
  the output tree (`path_within` twice per requirement). A concrete missing
  requirement still rebuilds; a tracked producer still owns its state.
- `ParserTemplate::path_to` and the `@pathto` target check no longer reject a
  target outside the project. The target is still existence-checked and
  dependency-recorded; an outside target produces the existing
  `path-outside-output` deployment warning, not a build failure.
- Embedded page scripts no longer set `enforce_project_root`; ordinary Nift
  code is unconfined. `NIFT_FS_ROOT`/`--fs-root` sets the explicit filesystem
  root and is enforced for `@path`/`@pathto` and expression-level operations.
- Package escapes, `.nift` metadata, custom build output and watch-list
  scoping are unchanged.

Tests: `fs_freedom.sh` now also covers an outside `@pathto` target and a
project build restricted by `NIFT_FS_ROOT`; `path_safety_smoke.sh` records the
new `@path` target policy. `tests/runtime2/cp03_simplify.sh`
(`test-runtime2-cp03-simplify`, in `make test`) adds the 200×10 and 20×20
syscall guard.

Performance (one thread, `strace -f`):

| Probe | Before | After |
|---|---:|---:|
| 200 pages × 10 links, no-op | 25,944 | **5,944** |
| 20 pages × 20 links, no-op | 9,158 (audit candidate) | **919** |
| readlink calls attributable to containment (200×10 no-op) | 16,000 | **0** |

Residual: `@path`/`@pathto` targets outside `public/` still emit the deployment
warning and may be tracked as external dependencies; they are not refused.
`link/..` is normalised lexically before the OS sees it (pre-existing).
Embedders that relied on default confinement must opt in with
`--fs-root`/the host policy.
