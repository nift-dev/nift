# .unfinished marker — revised design review (analysis only, no implementation)

Date: review of the "refuse ordinary builds + explicit `build-repair`" revision.
No production code changed; this is a design/opinion deliverable.

## 0. The core finding up front

Ordinary-build vs ordinary-build IS fully solved by exclusive marker creation.
The reviewer's instinct about the remaining hole is correct: **ordinary build vs
`build-repair` is NOT solved by the bare marker**, because `build-repair` must
legitimately proceed on a stale marker yet cannot distinguish "stale" from
"held by a live build" by file existence alone. Closing that hole needs exactly
the reviewer's intuition — a process-held OS lock on the marker file — and it
is minimal, kernel-released machinery (not PID parsing / liveness polling /
timestamps). Verdict: **the revised design is sound and better than both
alternatives, with three conditions** (flock shim, fsync the marker, ordering
invariant + command audit).

## 1. Overall verdict

ADOPT the revised design, subject to:

1. Marker acquisition uses exclusive create + an advisory exclusive lock
   (`flock`/`LockFileEx`) on the marker file, held for the whole build. This is
   what makes `build-repair` safe against a genuinely live build.
2. `fsync(marker)` once after acquisition, before the first mutation (power-loss
   window; 1 fsync per build).
3. The per-page ordering invariant is preserved and treated as a tested
   contract, and the command audit below is honoured (untrack/cp/mv must also
   refuse while the marker exists).

The refusal + explicit `build-repair` UX is better than automatic recovery and
keeps the design simpler overall than per-file atomic replacement while
recovering ~24-31 ms / 10k (prototype measured).

## 2. Does refusing ordinary builds fix the concurrent-direct-write weakness?

**Partially. It fully fixes ordinary-build vs ordinary-build; it does NOT fix
`build-repair` vs a live build.**

- Two ordinary builds: exclusive create is atomic. Exactly one creates the
  marker; the other observes EEXIST and refuses before mutating anything.
  SOLVED.
- Ordinary build vs a live `build-repair` (or a live ordinary build vs
  `build-repair`): NOT solved by existence. `build-repair` must proceed when the
  marker is stale; it cannot tell stale from live by the marker's existence
  alone. If it always proceeds, it writes concurrently with a live build
  (torn outputs). If it refuses unless forced, the force flag becomes the
  liveness override and re-opens the hole under user error.

Conclusion: the reviewer is right that the concurrency claim is incomplete.

## 3. Remaining races (enumerated)

All resolved by the O_EXCL + flock protocol below. The relevant interleavings:

- ordinary vs ordinary → EEXIST or EWOULDBLOCK loser refuses.
- ordinary (live, holds lock) vs build-repair → build-repair flock fails → refuses.
- build-repair (live) vs ordinary → ordinary EEXIST, flock fails → refuses.
- build-repair vs stale marker (no live holder) → flock succeeds → repairs.
- crash/kill/error-exit during build → kernel releases the lock; the marker
  FILE persists → next ordinary build sees it, flocks it, and refuses; repair
  proceeds.
- crash between `unlink(marker)` and `close(fd)` → unlink happens strictly
  after the final mutation; a new acquirer creates a fresh inode and proceeds,
  which is safe because the prior builder performs no further writes.
- two concurrent build-repair → one flock wins, the other refuses.
- power loss → fsync(marker) at acquisition closes the build window; the
  residual tail instant (final mutation vs marker removal) is the same class
  of window per-file atomic has without per-file fsync.

Two discipline notes:
- The marker fd must have a single owner and be closed only at the end (RAII);
  a stray close elsewhere would release an advisory lock (a discipline bug, not
  a protocol hole).
- NFS: `flock` (and legacy `O_EXCL`) are historically unreliable over NFS. For a
  local-build tool this is acceptable; document it.

## 4. Simplest reliable cross-platform exclusive marker creation

Two primitives, one small platform shim, no PID/liveness/timestamp machinery:

- **Exclusive create**: `open(path, O_CREAT|O_EXCL|O_RDWR)` (POSIX);
  `CreateFileW(CREATE_NEW)` (Windows). Answers "did I create it?" atomically and
  gives the stale-vs-clean signal.
- **Advisory exclusive lock on the marker fd**: `flock(fd, LOCK_EX|LOCK_NB)`
  (POSIX); `LockFileEx(...)` on the handle (Windows). Answers "is a live process
  holding this marker right now?" The kernel releases it when the process dies
  or closes the fd — this is the liveness answer WITHOUT PID parsing.

Use `flock`, not `fcntl(F_SETLK)`: fcntl record locks are released when ANY fd
to the file closes, which is a footgun; flock is per-open-file-description.
Windows `LockFileEx` is released on process termination via handle close.

The marker file itself is zero-byte and persists across process death; that is
what lets ordinary builds refuse on a STALE marker even though the flock is
gone.

### Acquisition protocol

Ordinary build:

```
1. fd = open(marker, O_CREAT|O_EXCL)
     success  -> clean start (we created it); flock(fd, EX|NB); build.
     EEXIST   -> fd = open(marker, O_RDWR); flock(fd, EX|NB):
                   EWOULDBLOCK -> LIVE build -> REFUSE ("another build is running")
                   success     -> STALE marker -> REFUSE ("previous build did not
                                  complete; run `nift build-repair`")
2. hold flock for the entire build; fsync(fd) before the first mutation.
3. success: unlink(marker) [AFTER final mutation], close(fd).
   failure/crash: close(fd) (lock released; file remains -> stale).
```

build-repair:

```
1. fd = open(marker, O_CREAT|O_RDWR); flock(fd, EX|NB):
     EWOULDBLOCK -> LIVE build -> REFUSE ("another build is running")
     success     -> no live build -> proceed to repair (stale or fresh marker)
2. hold flock; repair; fsync(fd) before the first mutation.
3. success: unlink(marker), close(fd). failure: close(fd) (file remains).
```

## 5. The build-repair-vs-live-build problem and preferred solution

Preferred solution: **build-repair acquires the marker the same way and
refuses when the flock is held by a live process.** The flock answers "stale vs
live" via the kernel. This is the minimal machinery that closes the hole; the
alternative answers are all worse:

- "build-repair ignores the marker and repairs": concurrent writers (torn).
- "build-repair requires `--force`": force becomes a liveness override; a
  forced repair during a live build corrupts; and every legitimate repair
  (stale marker) needs the flag. Rejected unless documented as user-override.
- "distinguish stale from live by the marker file being open/held": not
  portable.
- "cannot be made safe without additional machinery": the flock IS the minimal
  additional machinery; it is justified and small.

Note: Nift already has a `process_is_alive` shim for temp-file ownership in the
current atomic design. The direct-write model removes temp files, so that shim
goes away; the flock for the marker is the only liveness primitive left. Net
liveness machinery does not increase.

## 6. Exact mutation boundary in current Nift

Audited from the code:

- `ProjectInfo::open` validates config.json + tracked.json and loads the
  project — reads only; failures here must NOT leave a marker.
- `build_all`/`build_names` then call `begin_recovery_epoch()` — a
  non-mutating epoch counter (FileSystem.cpp:112), then `reconcile_watch()`.
- **`reconcile_watch()` (WatchList::reconcile, WatchList.cpp:108) is the first
  derived-state mutation**: it removes output/info/hash files for pages that
  disappeared from watched directories (WatchList.cpp:165-170) and later saves
  `.nift/.watch/watched.json` (WatchList.cpp:84).
- The lazy stale-temp sweep (`remove_stale_temporaries`) runs on the first
  generated-file write inside `build_many`, after the marker — covered.

Therefore: acquire the marker after `begin_recovery_epoch()` and immediately
BEFORE `reconcile_watch()`, in both `build_all` and `build_names`. A failure of
`reconcile_watch()` after it has started mutating correctly leaves the marker
(repair required); the only cosmetic cost is a marker left when it failed
before mutating.

### Command audit (mutating derived state, must respect the marker)

- In scope (mutate build-derived state): `build-all`, `build-updated`,
  `build`, `build-names`, `build-auto`/watch, and — the ones the proposal's
  list missed — `untrack`/`rm`/`del` and `cp`/`mv`, which call
  `remove_page_build_state` (CLI.cpp:333-341) and delete output/info/hash
  files. These must also refuse while the marker exists, or they can mutate
  derived state concurrently with a live build or a recovery.
- Watch/`build-auto`: acquire the marker per mutation pass, not for the whole
  session, so the marker does not linger during idle watching. A refusal inside
  the watch loop should stop the watch with the repair message rather than
  spam it.
- Out of scope (authoritative inputs or standalone): `track` (writes
  tracked.json), `init`/`init-html` (creates authoritative inputs + fresh
  derived state), `minify` (standalone), `info`/`status` (read-only). `init`
  into a directory already carrying a marker should clear it (it establishes
  authoritative state; there is no prior derived state to repair) or refuse if
  `.nift` exists.

## 7. Exact build-repair semantics

Conceptually `build-repair` distrusts all derived state and reconstructs it:

```
acquire marker + flock (refuse if a live build holds it)
distrust incremental freshness
force every tracked page through build_one
    -> regenerate output(s)
    -> pagination outputs
    -> stale pagination cleanup
    -> .info.json LAST
re-run the disappeared-page sweep for the authoritative tracked set
    -> remove outputs/info/hash for pages no longer tracked
regenerate hash/deps state (hash/hybrid incremental modes)
verify successful completion
unlink(marker) LAST; only on success (failure leaves the marker)
```

Internally it can share `build_one` and the job pipeline with
`build_all(force=true)`. The "stronger than build-all" property is achieved by
the distrust posture (force every page, no incremental selection) plus the
explicit acquisition/refusal and marker lifecycle. The prototype verified that
forcing every page through the existing `build_one` ordering recovers every
tested interruption state; formalizing `build-repair` as "forced build-all with
the marker contract and the disappeared-page sweep" is sufficient.

## 8. Should build-repair work without a marker?

Yes — make it always available. Without a marker it degenerates to exactly the
same operation (forced full rebuild + distrust + sweeps + marker lifecycle),
which is a genuinely useful manual recovery command for corrupt `.info.json`,
damaged generated output, or filesystem accidents. The marker only records
whether a prior build was interrupted; it adds nothing to the reconstruction
algorithm. A marker-gated variant would add an error branch ("nothing to
repair") for no benefit. Document that on a clean project it is simply a forced
full rebuild.

## 9. Pagination / stale-output implications

Unchanged from the prototype and still load-bearing: the per-page ordering
`output(s) -> stale pagination cleanup -> .info.json LAST` makes every reachable
pagination interruption state recover correctly, including the dangerous ones
(pagination removed/shrunk with `.info.json` torn — a torn info implies that
page's cleanup already ran). The "torn info AND stale page-2..N present" state
is unreachable while the ordering holds; `build-repair` must preserve it. The
disappeared-page sweep (section 7) covers stale outputs for pages removed from
the authoritative config. `build-repair` never reports success while stale or
corrupt generated state remains — verified by the 13-state prototype test.

## 10. Is the revised design better than the alternatives?

- vs (a) automatic marker recovery: **yes**. Refusal + explicit `build-repair`
  makes expensive reconstruction intentional and visible instead of silently
  turning `build-updated` into a 10,000-page rebuild; it also lets the error
  distinguish "another build is running" (transient) from "previous build
  unfinished" (needs repair). Cost: an unattended/CI crash makes the next build
  refuse until `build-repair` runs — for CI, pipelines should call
  `build-repair` (idempotent, one line). Document this.
- vs (b) per-file atomic replacement: **yes, with the conditions**. The marker
  + direct-write + repair recovers ~24-31 ms / 10k (prototype: A 84.5-85.9 /
  B 129.8-134.6 / C 104.0-106.1 ms), and removes the temp/rename/backup/getpid
  machinery. The price is the flock shim, the fsync, the ordering invariant,
  and the new repair UX. Per-file atomic remains the right choice only if
  "outputs are never torn even before the next build" or user-less automatic
  recovery are required guarantees.

## 11. Any reason to abandon the proposal?

No — it survives the attack, with the three conditions. The concurrency hole
the reviewer suspected (build-repair vs live build) is real and is closed by
the flock, which is minimal and not the PID/liveness machinery the proposal
wanted to avoid. The remaining caveats are bounded: power loss needs 1 fsync;
NFS locking is a documented caveat; `untrack`/`cp`/`mv` must join the refusal
set; a `--force` escape hatch would re-open the live-build hole (omit it, or
document it as a user override that may corrupt a concurrent build).

## Deliverables mapped

```
1. Overall verdict                          ADOPT with 3 conditions (section 1)
2. Refusal fixes concurrent writes?         ordinary-vs-ordinary yes; repair-vs-live no (section 2)
3. Remaining races                          enumerated, all resolved by O_EXCL+flock (section 3)
4. Simplest cross-platform mechanism        O_EXCL create + flock/LockFileEx (section 4)
5. repair-vs-live problem                   flock-based refusal; alternatives rejected (section 5)
6. Exact mutation boundary                  after begin_recovery_epoch, before reconcile_watch (section 6)
7. Exact build-repair semantics             forced full rebuild + distrust + sweeps + marker last (section 7)
8. build-repair without marker              always available (section 8)
9. Pagination/stale-output                  preserved ordering; verified states (section 9)
10. vs auto-recovery / vs atomic            better than both, with conditions (section 10)
11. Abandon?                                no (section 11)
```
