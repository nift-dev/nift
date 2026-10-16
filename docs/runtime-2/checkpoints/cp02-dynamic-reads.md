# CP02 — Dynamic reads become dependencies (review defect D2)

Status: see `../progress.md`.

## Objective
Reads that can change rendered bytes without touching a declared dependency
must invalidate the page in all incremental modes.

## Reproduced on v4.12.0
`exists()`-guarded `@input`, `ls()` globs and directory listings, and
`@getenv`/`getenv` values were not recorded; `nift status` reported "up to
date" while output was stale (24 failing checks in `dynamic_reads.sh`).

## Implementation
- `RenderResult::observed` (`ReadObservations`): `absent` paths, `listings`
  (key → fingerprint), `env` (name → value or unset), `environment_snapshot`.
- Recording (page renders only; standalone scripts record nothing):
  - `Parser::inspect_path` now wraps the raw query (`inspect_path_unobserved`)
    and records presence: an existing path becomes a requirement (`reqs`,
    stale if it disappears), a missing path becomes `absent` (stale if it
    appears). This covers `exists`, `is_file`, `is_dir`, `stat` and every
    prepared or legacy path that inspects metadata, because they all go
    through `inspect_path`.
  - `ls` (no argument, directory, glob) and the prepared glob walker record a
    listing key with a fingerprint computed by `nift::observe::listing_fingerprint`.
  - `getenv` and `@getenv` record the observed value; `env()` records an
    environment-wide fingerprint.
  - Pagination merges per-page observations.
- `src/ReadObservation.h` + `src/ParserReadObservation.cpp`: spelling of paths
  (project-relative inside the root, absolute outside), listing fingerprints
  (sorted names or matched paths, FNV-1a 64), environment helpers. The renderer
  and the build engine call the same functions, so they agree by construction.
- `ProjectInfo::append_read_observations` writes optional `absent`,
  `listings`, `environment`, `environment-snapshot` sections;
  `read_observation_reasons` re-checks them in `build_reasons`.

## Review (same-model adversarial pass; not independent)

| # | Finding | Resolution |
| --- | --- | --- |
| 1 | `env()` fingerprint included `_`, `PWD`, `SHLVL`, `NIFT_HOOK_*` → pages looked stale from another directory or with project hooks | Fixed: `environment_name_is_volatile` names excluded |
| 2 | Listings fingerprinted again for every page and twice per render: 5.3 s no-op on 201 pages × 10k-file `**` glob (base 0.016 s) | Fixed: per-build cache (`ProjectInfo::listing_fingerprint_cached`, via `RenderHost::listing_fingerprint`); now 0.07 s. The one remaining walk is the inherent cost of checking that listing |
| 3 | `open()`, `file().read()`, `file().exists()`, `stat().size`, type changes not recorded | Fixed: `observe_read`, `observe_metadata_read`, `inspect_path` for `file().exists()`, `kinds` section |
| 4 | `@input` source-relative shadowing probe not recorded | Fixed: probe recorded as presence/absence |
| 5 | Reads in per-item pre/post-build scripts dropped | Fixed: script `reqs` and observations merged into the page |
| 6 | Output directory outside the project rebuilt forever via `@pathto` | Fixed: output-tree rule also accepts the resolved output root (CP03) |
| 7 | `touch(x); exists(x); rm(x)` rebuilt forever | Fixed: paths the page or its scripts write/remove (`touch`, `rm`, `mkdir`, `cp`/`mv`, `file().save()`) are dropped from its observations |
| 8 | Reads in `async` functions not recorded | Open; scheduled with Host ops (CP16) |
| 9 | Modified mode misses a symlinked directory retargeted to older files | Documented: mtime limit of modified mode; hash mode detects it |
| 10a | `link/..` resolved lexically, not by the OS | Open; pre-existing path normalisation, scheduled with CP16 path handling |
| 10b | Globs in `cp()`/`rm()` arguments not recorded | Open; low impact, CP16 |
| — | Embedded scripts "still enforce project root" | Not reproduced: all script entry points set `enforce_project_root=false` |

Regression tests: `tests/runtime2/dynamic_reads_review.sh` (26 checks; 19 fail
on the CP01 binary). Wired into `make test` as `test-runtime2-dynamic-reads`.

## Untracked by design (documented)
Process output (`cmd(...).run()`, `run`), FFI results, network and clock reads
are not dependencies; neither is `which()`/`PATH` lookup. Projects that need them must declare inputs with `@dep`
or `.deps.json`. Async worker parsers do not report observations back to the
page (follow-up for CP16/CP24, where Host ops centralise recording).

## Compatibility
New page-info sections are optional. Pages that use these reads rebuild once
after upgrade (no metadata), then behave incrementally. `exists()` on an
existing path adds a requirement, which never makes a page fail — it only
rebuilds the page if the path disappears.

## Tests
- `tests/runtime2/dynamic_reads.sh` (new): 11 checks × 3 modes, including
  "stays up to date" after the initial build and after the last change.
- `tests/randomized_incremental_differential.py`: new `dynamic_absent` and
  `dynamic_glob` operations with the reads placed in the shared template. The
  extended campaign passes on the CP02 binary and fails at step 0 on v4.12.0.

## Performance
Listing fingerprints are recomputed only for pages that listed; presence checks
are one `stat` per recorded path. Gate: no-op build instructions within +5%.

## Commit strategy
1. Tests that fail on v4.12.0. 2. Observation module + recording. 3. Metadata
write/check + docs + evidence.
