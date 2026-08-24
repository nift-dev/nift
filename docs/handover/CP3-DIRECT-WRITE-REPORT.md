# CP3 report: direct-write persistence for regenerable build state

## mutation_started instrumentation (CP2.2 preamble)

- `ProjectInfo` holds one epoch-wide `std::atomic<bool> mutation_started_`
  (monotonic false->true) plus `mark_mutation()` / `mutation_started()`.
- Set BEFORE the recovery-relevant derived mutation, in:
  - `build_one`: immediately before the output / pagination-output write
    (the first page mutation; the ordering guarantees info is written LAST,
    so one mark per page covers the whole epoch for that page);
  - `WatchList::reconcile`: immediately before removing generated output for a
    disappeared watched page.
- Watch-state bookkeeping (`.nift/.watch/<dir>/tracked.json`) deliberately
  does NOT mark mutation (atomic temp+rename, regenerable, never torn).

Decision rule (build_all and build_names):
```
success                              -> finish() (remove marker)
controlled failure + !mutation_started -> finish()   (proven zero mutations)
controlled failure + mutation_started  -> retain     (repair required)
crash / SIGKILL                       -> retain      (finish never runs)
build --repair ANY failure            -> retain      (pre-existing stale
                                                      evidence unresolved)
```

## Zero-mutation failure tests (tests/zero_mutation_smoke.py)

1 all-pages render failure -> marker cleared; fix + ordinary build succeeds
2 schema/parser pre-write failure -> marker cleared
3 one page writes, later page fails -> marker REMAINS; ordinary refuses;
  --repair succeeds
4 stale pagination deletion happened + later failure -> marker REMAINS
5 SIGKILL mid-epoch (deterministic via NIFT_TEST_OWNERSHIP_HOLD) -> marker
  REMAINS; --repair recovers
6 build --repair fails with zero new mutations -> marker STILL remains
7 build --auto pass fails with zero mutation -> marker cleared, watch exits
  non-zero
ALL ZERO-MUTATION FAILURE TESTS PASSED.

## Direct-write files changed

- `src/FileSystem.h/.cpp`: `write_direct_file` / `write_direct_files`
  (in-place truncate with the historical writable-retry; no compare, no
  temp+rename; final mode = source permissions).
- `src/ProjectInfo.cpp`: `build_one` outputs, pagination outputs, and
  `write_page_info` (.info.json) switched from `write_readonly_file(s)` to
  `write_direct_file(s)`. Ordering output(s) -> stale cleanup -> .info.json
  LAST is preserved (the pagination-ordering strace test now observes the
  direct-write order: output write 1 < cleanup 3 < info write 4).

## Remaining atomic-write files

- `write_file` (temp+rename) remains for AUTHORITATIVE state: tracked.json
  (save_tracking), watched.json, init/deploy configs, content files.
- `write_readonly_file` / `write_readonly_files` remain as the retained atomic
  utility (used by tests/recovery_epoch_guard.cpp to exercise the temp/rename
  recovery machinery); no longer on the build-derived hot path.

## Recovery results (direct writes)

- crash_recovery_adversarial: kills mid-build -> marker present -> ordinary
  refuses -> build --repair converges to the clean baseline. PASS.
- Direct-write kill smoke: 9 MB page write killed mid-build -> marker present,
  ordinary refuses, build --repair regenerates the full output. PASS.

## 10k benchmark (established methodology, 20 interleaved samples)

```
              pre-Embed A   atomic B*   direct+marker C
unchanged 10k   85.4 ms      129.8 ms     104.9 ms   (+22.8% vs A)
changed 10k     81.8 ms      134.6 ms     102.3 ms   (+25.0% vs A)
```
*B numbers are the historical CP1/CP2 measurements, not re-run this session.
Outputs byte-identical vs A on both workloads. The ~25-30 ms persistence cost
of per-file atomic replacement is gone; the remaining ~20 ms residual is the
known non-atomicity cost: pagination-history read ~12 ms, render seam ~6 ms,
file_permissions ~2 ms, marker ~0 (per earlier attribution).

## Full regression

46 make targets / 158 PASS lines / exit 0, including conformance 9/9,
pagination equivalence, output-permissions, crash-recovery, the ownership
suite (38 checks), the zero-mutation suite, and the pagination-ordering
invariant under direct writes.

## Commit / hygiene

(committed by the CP3 checkpoint). Clean tree: no binaries, objects, or
worktrees.

## CP3.1 correction (reviewer HOLD): zero-mutation control-flow holes

### All post-acquisition exit paths audited

- `build_names`: duplicate-name rejection and empty-job-set validation moved
  BEFORE ownership acquisition (reads only), so they neither create nor leave
  a marker.
- `build_all` / `build_names` reconcile-watch failure: routed through the
  central completion rule (previously an unconditional `return 1` that left
  the marker even when reconcile failed before any derived deletion).
- Final result handling: unified.

### Exact control-flow fix

Added `ProjectInfo::finish_if_epoch_complete(ownership, result, repair)`:
```
result == success                              -> finish()
result != success && !repair && !mutation_started -> finish()
otherwise                                     -> retain
```
Every controlled exit after acquisition now routes through it; the only
direct `return 1`s after acquisition are the Live/Failed/Stale refusals (no
marker was created by us, and a stale marker correctly stays). `build --repair`
failure always retains (pre-existing stale evidence).

### New zero-mutation cases (tests/zero_mutation_smoke.py, now 11)

- 8 duplicate-name targeted build -> nonzero, no marker
- 9 targeted build with no resolvable jobs -> nonzero, no marker
- 10 reconcile_watch controlled failure before derived deletion (corrupt watch
  state) -> nonzero, no marker, page output intact
- 11 reconcile_watch failure after derived deletion (disappeared page output
  removed, then the watch-state save fails via a read-only state dir) ->
  nonzero, mutation_started true, marker REMAINS, ordinary build refuses,
  --repair recovers after restoring the authoritative project

### Confirmation benchmark (10 samples, cold/error-path-only change)

unchanged: A 81.4 / C 101.6 ms (+24.9%); changed: A 80.6 / C 100.1 ms
(+24.3%) - consistent with the full CP3 run (104.9/102.3); the hot path is
unaffected.

### Full regression

46 targets / 177 PASS lines / exit 0.
