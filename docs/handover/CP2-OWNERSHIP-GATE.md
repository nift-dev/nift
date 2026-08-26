# CP2 ownership gate — protocol-correct serialization of marker create+lock

Status: 2026-08-26 (CP15 hardening, from the Checkpoint-10 run 32926885753
investigation).

## The defect

`ProjectOwnership::acquire()` creates `.nift/.unfinished` with
`O_CREAT|O_EXCL` and then takes the advisory flock in a **separate syscall**.
A concurrent process can open and flock the freshly-created-but-not-yet-locked
marker first, classify it **Stale** ("unfinished build detected") and refuse,
while the creator then fails to flock and refuses as **Live** too: BOTH
concurrent builds refuse and the marker remains. This was reproduced locally
(~1-in-20, exact evidence: `codes=[1,1]`, both stderr messages, marker left)
and was the cause of the Checkpoint-10 linux job failure.

## The fix: an ownership gate (no timing heuristics)

Every `acquire()` first takes a **blocking** advisory lock on a project-local
gate file (`.nift/.ownership-gate`), holds it across the entire marker
create+lock critical section, then releases it:

```text
acquire()
  ├─ open + blocking-flock .nift/.ownership-gate   (waits for any creator)
  ├─ critical section (serialized):
  │    ├─ exclusive_create(marker) → created?
  │    │     ├─ yes → non-blocking flock(marker)   (always succeeds; we hold
  │    │     │        the gate, so no one else can observe the fresh marker)
  │    │     └─ no  → open + non-blocking flock(marker)
  │    │               ├─ succeeds → Stale (genuinely stale: previous owner
  │    │               │            crashed or finished)
  │    │               └─ fails    → Live (a live build owns it)
  │    └─ durable_sync(marker) + parent dir
  └─ release gate
```

A freshly created marker is therefore **never observable unlocked by another
process**: the creator locks it before releasing the gate. The correctness
argument is mutual exclusion (the gate serializes the create+lock window), not
a sleep/retry heuristic. The gate is held only across the short marker
create/classify/lock critical section, never across the build (filesystem and
scheduler latency can make any single acquisition take longer than a nominal
figure, so the contractual property is the critical-section scope, not an
elapsed-time bound); the build's long-lived ownership remains the non-blocking
marker flock (unchanged semantics: Clean / Stale / Live / Failed).

- The gate is released before `test_hold_after_acquire()` so the
  NIFT_TEST_OWNERSHIP_HOLD hook still yields Live refusals to concurrent
  commands (they block on the gate for microseconds, then observe the held
  marker).
- `live_owner_exists` (read-only probe) does not take the gate; it remains
  best-effort.
- The gate file is infrastructure in `.nift` and is not touched by repair
  (repair deletes only pagination surplus, orphan `.info.json`, and stale
  `.hash` files).
- If the gate cannot be created/locked the acquire fails (Failed): the marker
  path requires the same directory permissions, so a build would fail anyway.

## Evidence

- `tests/ownership_concurrency.py` step 8 (two-process stress, 12 rounds × 2
  concurrent builds): 25/25 full-suite passes.
- Race-pattern hammer (the exact create-before-flock window, 80 project
  trials × 12 rounds): 0 reproductions (pre-fix ~1/14 trials).
- The prior timing-based repair (creator retry ~100 ms vs stale-acquirer
  watch ~5 ms) was reviewed and replaced because asymmetric sleeps do not
  prove the protocol under arbitrary scheduler delay; the gate serializes the
  window instead.
