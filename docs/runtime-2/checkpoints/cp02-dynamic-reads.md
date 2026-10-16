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

## Untracked by design (documented)
Process output (`cmd(...).run()`, `run`), FFI results, network and clock reads
are not dependencies. Projects that need them must declare inputs with `@dep`
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
