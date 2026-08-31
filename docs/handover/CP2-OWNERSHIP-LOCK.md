# CP2 ownership lock — protocol-correct serialization of marker create+lock

Status: 2026-08-26 (CP15 hardening, from the Checkpoint-10 run 32926885753
investigation); updated 2026-08-31 (renamed `.nift/.ownership-gate` to
`.nift/.lock` with explanatory content and safe legacy migration).

## The defect

`ProjectOwnership::acquire()` creates `.nift/.unfinished` with
`O_CREAT|O_EXCL` and then takes the advisory flock in a **separate syscall**.
A concurrent process can open and flock the freshly-created-but-not-yet-locked
marker first, classify it **Stale** ("unfinished build detected") and refuse,
while the creator then fails to flock and refuses as **Live** too: BOTH
concurrent builds refuse and the marker remains. This was reproduced locally
(~1-in-20, exact evidence: `codes=[1,1]`, both stderr messages, marker left)
and was the cause of the Checkpoint-10 linux job failure.

## The fix: a project-local serialization lock (no timing heuristics)

Every `acquire()` first takes a **blocking** advisory lock on a project-local
serialization file (`.nift/.lock`), holds it across the entire marker
create+lock critical section, then releases it:

```text
acquire()
  ├─ open + blocking-flock .nift/.lock   (waits for any creator)
  ├─ populate .lock with the explanation if it is empty (interrupted creation)
  ├─ migrate an idle legacy .ownership-gate to .lock (removed after .lock is
  │    established and locked); refuse if a live process still holds the legacy
  │    file (concurrent different-version migration is unsupported)
  ├─ critical section (serialized):
  │    ├─ exclusive_create(marker) → created?
  │    │     ├─ yes → non-blocking flock(marker)   (always succeeds; we hold
  │    │     │        the lock, so no one else can observe the fresh marker)
  │    │     └─ no  → open + non-blocking flock(marker)
  │    │               ├─ succeeds → Stale (genuinely stale: previous owner
  │    │               │            crashed or finished)
  │    │               └─ fails    → Live (a live build owns it)
  │    └─ durable_sync(marker) + parent dir
  └─ release .lock
```

A freshly created marker is therefore **never observable unlocked by another
process**: the creator locks it before releasing the serialization lock. The
correctness argument is mutual exclusion (the lock serializes the create+lock
window), not a sleep/retry heuristic. The serialization lock is held only
across the short marker create/classify/lock critical section, never across the
build (filesystem and scheduler latency can make any single acquisition take
longer than a nominal figure, so the contractual property is the
critical-section scope, not an elapsed-time bound); the build's long-lived
ownership remains the non-blocking marker flock (unchanged semantics: Clean /
Stale / Live / Failed).

- The serialization lock is released before `test_hold_after_acquire()` so the
  NIFT_TEST_OWNERSHIP_HOLD hook still yields Live refusals to concurrent
  commands (they block on the lock for microseconds, then observe the held
  marker).
- `live_owner_exists` (read-only probe) does not take the serialization lock;
  it remains best-effort.
- `.nift/.lock` is normal persistent concurrency infrastructure in `.nift` and
  is not touched by repair (repair deletes only pagination surplus, orphan
  `.info.json`, and stale `.hash` files). Its presence does not mean a command
  is running and does not require repair — deliberately distinct from
  `.nift/.unfinished`, which is evidence that a derived-state mutation was not
  proven to finish and requires `nift build --repair`.
- If the serialization lock cannot be created/locked the acquire fails
  (Failed): the marker path requires the same directory permissions, so a build
  would fail anyway.

## The .nift/.lock file

- Newly created `.lock` files contain the exact sentence:

  ```text
  Nift project lock. This persistent file is normal and does not indicate an active or failed build.
  ```

- The explanation is written only while the serialization lock is held, and
  only when the file is empty (brand-new, or an interrupted earlier creation).
  An existing non-empty `.lock` is never truncated or rewritten, so the file
  keeps a stable filesystem identity across every acquisition. Locking does not
  depend on the contents.
- `.lock` is never deleted by a successful command or by `finish()`.

## Legacy .ownership-gate migration

Before 4.0.8 the serialization file was named `.nift/.ownership-gate`. On first
use with a legacy project:

1. If `.nift/.ownership-gate` exists, open it and take its advisory lock
   non-blocking.
2. If the legacy lock cannot be acquired, a live process still holds it (a
   concurrently running older Nift). The acquire refuses (Failed) and the
   legacy file is left untouched. **Concurrently running different Nift
   versions during migration is unsupported.**
3. If the legacy file is idle, open/create `.nift/.lock`, take its
   serialization lock, populate the explanation if empty, then remove the
   unlocked legacy file and release its handle. The legacy file is removed only
   after `.lock` is established and locked.
4. The marker create/classify/lock critical section then proceeds normally.

## Evidence

- `tests/ownership_concurrency.py` steps 1/3/4/9 assert `.lock` presence,
  exact explanatory content, retention across failure/repair, stable identity
  across repeated commands, fresh projects never creating `.ownership-gate`,
  idle legacy migration, and locked-legacy refusal (the gate is never removed).
- `tests/ownership_unit.cpp` (cross-platform) asserts `.lock` creation,
  exact content, persistence after `finish()`, idle migration, and
  interrupted-empty-file repair while the lock is held.
- Two-process stress (step 8, 12 rounds × 2 concurrent builds): 25/25
  full-suite passes.
- Race-pattern hammer (the exact create-before-flock window, 80 project
  trials × 12 rounds): 0 reproductions (pre-fix ~1/14 trials).
- The prior timing-based repair (creator retry ~100 ms vs stale-acquirer
  watch ~5 ms) was reviewed and replaced because asymmetric sleeps do not
  prove the protocol under arbitrary scheduler delay; the serialization lock
  serializes the window instead.