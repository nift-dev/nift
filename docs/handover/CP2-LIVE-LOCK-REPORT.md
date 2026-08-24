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

## CP2.1 correction (reviewer HOLD): mutator TOCTOU + durability + portability

### Exact TOCTOU fix

`untrack`/`rm`/`del` and `cp`/`copy`/`mv`/`move` previously gated mutation on a
non-owning liveness probe (`ProjectOwnership::live_owner_exists`), which is
check-then-act: a build could acquire ownership between the probe and the
mutation, giving two concurrent mutators. Fixed by making these commands run a
**full ownership epoch** (same as builds): all validation first (no mutation on
validation failure), then `acquire()` (Clean -> proceed holding the lock;
Live -> refuse "another build appears to be running"; Stale -> refuse
"unfinished build detected ... run `nift build --repair`"), then mutate while
holding the lock, then `finish()` only on complete success. A crash mid-mutation
now leaves `.unfinished`, so the next build refuses and `build --repair`
restores derived state.

### Mutator ownership semantics

- **untrack** mutates tracked.json (AUTHORITATIVE only - it does NOT call
  remove_page_build_state). It participates for one uniform invariant and the
  reviewer's preferred semantics A, although a principled classification could
  exempt it (see the table).
- **rm/del** mutate tracked.json (authoritative) AND remove output/info/hash
  (derived) - the genuine derived mutator.
- **cp/copy** create destination content + tracked entry (authoritative);
  participates for uniformity (reviewer preference A).
- **mv/move** mutate tracked.json + content (authoritative) AND remove the
  source page's derived state - genuine derived mutator.

### Stale-marker semantics for non-build mutators

Semantics A as the reviewer preferred: a stale `.unfinished` makes untrack/rm/
cp/mv REFUSE until `build --repair` - one invariant ("stale derived state is
untrusted; only `build --repair` may mutate it"). Documented trade-off: an
authoritative-only change (e.g. untrack) is forced to run `--repair` first,
which is conservative but consistent.

### Complete mutation classification

```
command         authoritative   derived   participates   reason
build / build X / --all / --auto / --repair   -   yes   yes (per-pass for --auto)
track           yes (tracked.json, content)   -    no    authoritative-only; atomic write
untrack         yes (tracked.json)            -    yes   uniformity + reviewer preference A
rm / del        yes (tracked.json)            yes  yes   derived mutator
cp / copy       yes (content, tracked.json)   -    yes   uniformity + reviewer preference A
mv / move       yes (tracked.json, content)   yes  yes   derived mutator
watch / unwatch yes (.nift/.watch)            -    no    watch config; atomic; not build-derived
minify          -                              -    no    standalone file op
init            yes (fresh project)           -    no    fresh-project creation
info / status   -                              -    no    read-only
```
Not gated (track/watch/minify/init) never touch outputs/.info/hash, use atomic
authoritative writes, and a concurrent build uses its own opened snapshot.
Caveat documented: `init` over an existing crashed project would leave a stale
marker; the first build then refuses until `--repair`.

### New race tests (deterministic, not timing-based)

A test-only, env-gated sync hook (`NIFT_TEST_OWNERSHIP_HOLD=<dir>` in
ProjectOwnership::acquire) makes a successfully acquired owner write
`<dir>/acquired` and block until `<dir>/release` appears. The concurrency
suite now proves BOTH directions deterministically:
- build-first -> untrack/rm/cp/mv refuse (existing test 5);
- mutator-first -> build --all refuses while untrack/mv/rm hold ownership
  (new test 7, one mutator per held-ownership epoch);
- mutator-vs-mutator -> untrack and cp refuse while rm owns (new test 7b).
Result: 38 checks, ALL OWNERSHIP/CONCURRENCY TESTS PASSED. The reviewer's
example race (mutator probes false, build acquires, mutator mutates) can no
longer happen: the mutator holds the lock for its whole mutation window, and
the tests prove the held lock refuses concurrent builders.

### Windows unit-test portability

`tests/ownership_unit.cpp` now builds its unique temp directory from the
steady clock instead of `::getpid()` (which was only incidentally available on
MSYS2 UCRT via `<unistd.h>`/`<process.h>`), so the test compiles on Windows
with standard headers only. The fork/kill process-death block remains
`#ifndef _WIN32` (SKIP with note on Windows). Pending: a real Windows CI run
of `test-ownership-unit` on the merged commit.

### Exact power-loss durability guarantee (corrected)

`fsync(marker)` flushes the marker file contents + inode but NOT the parent
directory entry that links the name. CP2.1 adds `fsync(parent directory)` at
acquisition on POSIX (one extra fsync per build, not per output), so the
stated guarantee - the marker is durably established before any derived
mutation - now holds for both the file and its directory entry. On Windows,
`FlushFileBuffers` flushes file data/metadata and NTFS journals the creation,
but no directory-handle flush is issued; the documented Windows guarantee is
therefore explicitly NARROWER (journaled creation, no directory-handle fsync).
No per-output fsync; database-grade durability is still not claimed.
