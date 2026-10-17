# CP01 — Modified-mode timestamp races (review defect D1)

Status: see `../progress.md`.

## Objective
An edit must never be silently missed by a `modified`-mode incremental build,
whether it happens just after a build or while the page that reads it renders.

## Root causes (reproduced on v4.12.0)
1. `write_readonly_file` skips rewriting byte-identical page metadata and
   refreshes its mtime with `fs::last_write_time(path, clock::now())`. That
   user-space clock runs up to one kernel tick ahead of the coarse clock the
   kernel uses to stamp ordinary writes, so a source edited within that tick
   gets an *older* mtime than the metadata and `mtime >= page-info mtime` is
   false. Reproduced: 98/200 rapid edit→build cycles missed.
2. A dependency edited after the page read it but before page-info was written
   is older than page-info, so the next build trusts stale output (classic
   make race).

## Implementation
- `filesystem::refresh_modified_time_now` stamps with `utimensat(…, UTIME_NOW)`
  on POSIX, i.e. the same clock and granularity as ordinary writes. On Windows
  it stamps with `GetSystemTimeAsFileTime` + `SetFileTime` (the coarse clock
  NTFS uses for writes) instead of the precise `file_clock::now()`; this path
  is not compiled here (D-004: pending Windows CI).
- `ProjectInfo::stamp_render_clock` (modified mode only) touches `.nift/.lock`
  and reads its mtime back, per page, immediately before the page renders:
  after that page's pre-build hooks and after every prerequisite (`depends`)
  page has been built. A failed stamp warns once and disables only this
  check.
- `classify_render_dependencies`, called after rendering and before outputs
  and post-build hooks are written, sorts each dependency:
  - stamped strictly later than the render clock → `"racy-dependencies"`;
    the next build re-renders ("dependency was modified while the page was
    being built: …");
  - stamped in the render clock's own tick → content hash recorded under
    `"tick-hashes"`. When such a dependency later looks "changed" by the
    `mtime >= page-info mtime` rule, the hash decides. This removes the
    rebuild loops caused when Nift itself wrote a dependency (hook output,
    prerequisite output) in the same tick as the page-info write.

## Compatibility
- Page-info gains one optional field, written only when non-empty. Older
  metadata is read unchanged.
- No behaviour change for hash/hybrid modes (content snapshots already cover
  the race).
- `tests/dependency_status.py`'s intentional "older mtime is not detected in
  modified mode" contract is unchanged.

## Tests
- `tests/runtime2/modified_race.sh` (new; `make test-runtime2-modified-race`):
  (a) immediate edit→build cycles (1,000 standalone, 300 in `make test`),
  (b) a dependency edited while its page sleeps in `@script`, (c) a pre-build
  hook regenerating an input causes no follow-up rebuilds, (d) prerequisite
  outputs are never flagged racy and the dependant is never stale.
- Previously failing on this host and now passing:
  `test-incremental-modified-immediate`, `test-consumer-snapshots`.
- Full `make -k test` compared with the CP00 run (see Evidence).

## Performance
One `utimensat` + `stat` per rebuilt page (under a mutex); one `stat` per
dependency per rebuilt page, plus a hash of dependencies stamped in the render
clock's tick. Nothing extra on no-op checks except a hash for a tick-hashed
dependency whose mtime is not older than page-info. Gate: no-op build instructions within +5% of baseline.

## Review (same-model adversarial pass; not independent)
Findings and resolution:

| # | Finding | Resolution |
| --- | --- | --- |
| F1 High | Build-wide clock flagged pre-build hook output as racy → endless rebuilds | Fixed: per-page clock after pre-build hooks; same-tick writes resolved by tick hashes. Hook scenario: 1/10 follow-up builds rebuild (baseline 1–2/10); test (c) |
| F2 Medium | `depends` prerequisite outputs flagged racy → no-op builds not no-ops | Fixed: per-page clock taken after prerequisites build. 0/45 spurious (baseline 0/45); test (d) |
| F3 Medium | Windows still used the precise clock | Fixed in code (coarse clock); unverified until Windows CI |
| F4 Low | Edit within the render clock's tick after the read is missed; comment said "at or after" | Documented residual; comments corrected |
| F5 Low | Build-wide stamp over-flags long builds | Fixed by per-page clock |
| F6 Low | `const` method writing a `mutable` member | Removed: the clock is a local per page |
| F7 Low | Failed touch disabled detection silently | Warns once |
| — | Race test not wired into `make test` | `test-runtime2-modified-race` (300 cycles by default) |

## Residual risk (documented, not fixed)
An edit made after the page read a file but within the same timestamp tick as
the page's render start (one kernel tick, 4 ms at HZ=250; up to 2 s on
FAT/HFS+/ext3) is not detected. Hash mode has no such window. Hash mode has no such window. A
strict `>=` comparison was tried first with a 2-second backwards margin; it made
`nift status` report stale pages immediately after every build of recently
written files (broke `test-dependency-path-identity`), so it was rejected.

## Commit strategy
1. Failing test. 2. Fix + this record. 3. Evidence/progress update.

## Post-certification fix (CI @084f09f): reproducible page-info
`racy-dependencies` and `tick-hashes` depend on timing, and writing them into
page-info made its bytes nondeterministic: BH6 (`test-init-functional-truth`,
"clean rebuild does not reproduce init'd page metadata") failed 4/5 locally
and in CI on Linux (and plausibly the Windows v4.9/v4.10 contracts). They now
live in `.nift/clock/<page-info path>` (written only when non-empty, removed
otherwise); page-info is byte-reproducible again (BH6 6/6). The miss was not
caught locally because BH6 is not in `make test-all`; D-010's certification
target is now `make certify` (test-all plus every suite the CI workflows run),
kept complete by `tests/runtime2/certification_guard.sh`.

## Owner review follow-up: timing-record lifecycle and touch semantics
- The timing record is correctness state (`docs/runtime-2/incremental-state.md`):
  always written in modified mode; missing/malformed → conservative rebuild;
  removed with its page; pruned for untracked pages on non-no-op builds;
  removed in hash/hybrid mode. `tests/runtime2/clock_lifecycle.sh` (22 checks:
  clean/unchanged/rebuild, missing, truncated, stale racy entry, SIGKILL
  mid-build + repair, rm, untrack pruning, bounded count, mode switches,
  external symlinked dependency).
- Tick hashes are bound to the dependency's exact mtime
  (`"<sec>.<nsec>:<hash>"`): any later write — including rewriting identical
  bytes (a touch) — counts as a change again, as in v4.12.0. Found while
  investigating `v45_target_smoke`, which tick-hashing made fail more often.

