# CP2 report: live lock + .nift/.unfinished productionization

## 1. Lock abstraction per platform

`src/ProjectOwnership.h/.cpp` is the single encoded protocol. Small platform
shim (no PID/liveness/timestamps/UUID/stale-heuristics):

- POSIX (Linux/macOS): `open(O_CREAT|O_EXCL|O_RDWR)` for atomic exclusive
  creation, `flock(fd, LOCK_EX|LOCK_NB)` for the advisory live lock,
  `fsync(fd)` for marker durability, `unlink`+`close` on success.
- Windows: `CreateFileW(CREATE_NEW)` for exclusive creation,
  `LockFileEx(... LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY)` for the
  process-held lock (released on handle close / process death),
  `FlushFileBuffers` for durability.

Observable contract (matches the review): only one live derived-state mutator
owns the project; process death releases the lock automatically; `.unfinished`
survives process death.

## 2. Exact marker lifecycle

```
acquire (build_all / build_names, before the mutation epoch):
  O_EXCL create succeeds            -> Clean   (marker created)
  EEXIST -> open existing + flock:
      flock EWOULDBLOCK             -> Live    (refuse everyone)
      flock succeeds                -> Stale   (previous epoch unfinished)
  fsync(marker) [durability, once per build]
mutation epoch:
  reconcile_watch() -> build_one writes (outputs, pagination cleanup, .info)
  ...
on success (result == 0): finish() = unlink(marker) then close(fd)   [marker LAST]
on failure/crash: destructor closes fd only; the marker REMAINS
```

`finish()` is the only success path that removes the marker; the destructor
never removes it (a failed/interrupted epoch must leave `.unfinished`).

## 3. Exact refusal behaviour

- Ordinary `build` / `build <names>` / `build --all`: refuse on Stale
  ("unfinished build detected ... run `nift build --repair`") and on Live
  ("another build appears to be running"). Proceed only on Clean.
- `build --repair`: proceeds on Clean and Stale (the only mode allowed to take
  over a stale marker); refuses on Live. On success it restores the clean
  marker state (marker removed).
- `build --all` does NOT override unfinished state.
- `untrack`/`rm`/`del`/`cp`/`copy`/`mv`/`move`: refuse only on a LIVE owner
  (lock probe, no full epoch) - a stale marker alone does not make a
  single-page removal unsafe; only a concurrent mutator does. `track` writes
  authoritative state only and is not gated. Read-only commands
  (`info`, `status`, `info --*`) are unaffected.

## 4. Mutation boundary

`ProjectInfo::open()` (CLI) validates authoritative state before any dispatch.
Inside `build_all`/`build_names`, acquisition happens after the non-mutating
`begin_recovery_epoch()` and immediately BEFORE `reconcile_watch()`, which the
audit confirmed is the first derived-state mutation (WatchList.cpp:165-170
removes disappeared pages' outputs; line 84 rewrites watched state). No derived
mutation occurs before ownership; `finish()` (marker removal) occurs strictly
after the final mutation.

## 5. Marker durability / power-loss guarantee

One `fsync` on the marker fd at acquisition (no per-output fsync). Precise
guarantee: the marker is durable before any derived mutation begins, so an
interrupted epoch is detected even across power loss for the build window. A
residual tail instant (the final mutation vs marker removal) matches the same
class of window the atomic-write model has without per-file fsync; database-
grade durability is explicitly not claimed.

## 6. Protected derived-state mutators

`build` (all modes incl. `--repair`, `--auto`), `untrack`/`rm`/`del`,
`cp`/`copy`/`mv`/`move`. `track` (authoritative-only), `init`, `minify`,
read-only queries are not gated. Reasoning is stated in the code and report
section 3.

## 7. build --auto behaviour

`build --auto` acquires per mutation pass (each pass creates the marker and
removes it on success), so an idle watcher does not monopolize the project. A
pass that cannot acquire (stale/live) makes `build_all` refuse and the watch
loop stops with the error rather than inventing hidden recovery.

## 8. Concurrency / lifecycle tests (deterministic)

`tests/ownership_concurrency.py` (Linux/macOS; uses Python `flock` to drive
the exact same C++ flock code path, plus real two-process stress):
1 clean lifecycle; 2 pre-mutation failure leaves no marker; 3 failed
post-mutation build leaves marker; 4 stale+ordinary refuses / stale+repair
recovers; 5 live lock refuses build/all/repair/targeted/untrack/cp/mv while
info/status stay read-only; 6 SIGKILLed holder releases lock, marker survives,
ordinary refuses, repair acquires; 7 concurrent two-process builds: >=1
succeeds, refusals are live-lock, outputs byte-identical, no marker left.
Result: ALL OWNERSHIP/CONCURRENCY TESTS PASSED (29 checks).

`tests/ownership_unit.cpp` (cross-platform C++ unit of ProjectOwnership):
Clean/Stale/Live/two-holder conflict/live_owner_exists + POSIX fork/kill
process-death path. Result: ALL OWNERSHIP UNIT TESTS PASSED (15 checks).

`tests/pagination_ordering_smoke.sh` (Linux, strace): asserts the load-bearing
order output write < stale pagination cleanup < .info.json write for a
shrinking pagination build. Result: PASS (1 < 4 < 5).

## 9. Full regression results

Full make battery: 45 targets, 112 PASS lines, exit 0 - including the new
test-ownership-unit, test-ownership-concurrency, test-pagination-ordering,
test-commands, conformance 9/9, and the pagination lifecycle suite. Six
pre-existing error-path tests (parser_content, control_flow, pagination,
json_schema_integration, path_safety, crash_recovery) plus checkpoint3/6/8/9/10
were updated for the new "failed build leaves .unfinished" semantics (recovery
builds use `build --repair`; expected-failure fixtures clear the marker).

## 10. Cross-platform evidence / pending

- Linux: full evidence above (unit, concurrency, ordering, battery).
- macOS/Windows: the C++ unit test (`test-ownership-unit`) is wired into
  `.github/workflows/checkpoint-10-cross-platform.yml` for all three runners;
  the Python concurrency suite (POSIX `flock`) runs on Linux and macOS. The
  fork/kill process-death path is POSIX-only and is SKIPPED on Windows with an
  explicit note in the unit test. Windows evidence for the killed-process
  lifecycle is PENDING a CI run on the merged commit.

## 11. Performance

Marker/lock overhead is 2-3 syscalls once per build (flock + fsync + unlink),
not per page. Sanity: 10k full build with the protocol ~109 ms median, i.e.
negligible; formal A/B/C performance work remains CP3/CP5.

## 12. Commits / hygiene

(committed by the CP2 checkpoint)
- clean-tree status after commit: no binaries, objects, or temp worktrees.
