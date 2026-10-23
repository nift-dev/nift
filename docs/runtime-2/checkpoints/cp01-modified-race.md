# CP01 — modified mode restored to v4.12 semantics (simplification)

**Status:** implemented locally; pending independent review. Owner decision
2026-10-23: remove the CP01 clock machinery, keep only the kernel-clock
page-info refresh.

## Contract

Modified mode (the default):

- rebuild when a dependency is missing, or when its mtime is `>=` the
  page-info mtime;
- page-info is stamped from the filesystem's own clock
  (`refresh_modified_time_now`, `utimensat(path, NULL)` on POSIX);
- catches ordinary changes made after a build completes, including immediate
  edits and atomic (rename-over) saves;
- does **not** promise to catch: edits during a running build (including a
  shared template saved mid-build), metadata-preserving replacements, several
  writes in one timestamp tick, backdated mtimes, or a backwards clock;
- equal timestamps rebuild conservatively;
- no hashing, clock records, global render locks or repeated checks.

Hash mode is unchanged and remains the opt-in for reliable content-change
detection.

## Removed

`src/ProjectInfo.{cpp,h}`: `tick_hash_value`, `tick_hash_matches`,
`stamp_render_clock`, `classify_render_dependencies`, `render_clock_path`,
`prune_render_clock_records`, `write_render_clock_record`, the render-clock
mutex/warned flag/observation struct, the `.nift/clock` read in
`build_reasons`, "missing timing record ⇒ rebuild", and all render-clock call
sites. `write_page_info` now ends after the page-info write and its crash
point. Clock-record removals were deleted from `CLI.cpp`, `Automation.cpp` and
`WatchList.cpp`. `tests/runtime2/clock_lifecycle.sh` and its make and
certification-guard entries are gone.

## Kept

- `refresh_modified_time_now` (POSIX/Windows): v4.12's user-space clock missed
  999/1,000 immediate edits permanently; the kernel clock passes 0/1,000.
- `platform()` invalidation (bundled with CP01 in `804ad627`; no `git revert`).
- D-020 crash/interrupted-build recovery (the failed-page list).
- CP02, CP03, shared-source cache, dependency fingerprints/snapshots,
  temp+rename page-info writes and all hash-mode code.

## Tests

- `tests/runtime2/modified_race.sh`: immediate plain edits and atomic saves
  (1,000 cycles each, 0 missed), no concurrent-edit promise, paced pre-build
  hook follow-up, prerequisite-output stability.
- `tests/runtime2/cp01_simplify.sh` (new): ordinary dependency edits/add/
  remove/delete, no-ops, targeted/full rebuilds, mode switches, crash points,
  settle, hash/hybrid shared-template repair, and the deterministic syscall
  guard. Run by `test-runtime2-cp01-simplify`, which is part of `make test`.
- `tests/runtime2/dynamic_reads_review.sh`: the platform-independent follow-up
  is paced past a timestamp tick.
- `tests/build_recovery_random.py`: in modified mode, page-info comparisons
  ignore `dependency-hashes`; outputs are still byte-identical to a clean
  build.

## Syscall result (1,000 pages, one thread)

| Phase | CP01 candidate | This simplification | v4.12 |
|---|---:|---:|---:|
| no-op | 31,146 | **19,146** | 21,161 |
| full | 52,226 | **41,211** | 41,212 |
| one-page edit | 31,279 | **19,243** | 21,258 |
| shared-template edit | 94,277 | **71,251** | 73,267 |

`.nift`: 1,003 files (was 2,003). No `.nift/clock` access; `.nift/.lock` is
one ownership access, as in v4.12. Counts are deterministic `strace -f -c`
on the local kernel.

## Residual v4.12 behaviour (documented)

- A build immediately after an edit in the same timestamp tick may rebuild
  once; on coarse (1–2 s) filesystems back-to-back builds rebuild until the
  next tick. This is v4.12 semantics, not missed output.
- A shared template saved during a build can leave pages stale until the next
  edit (modified mode); hash mode repairs it on the next build.
