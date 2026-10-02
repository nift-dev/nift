# v4.6 Batch 4 CP7 worker/concurrency hardening and certification

Status: complete. Hardens and certifies the async/thread worker model built on
CP6 semantics. No cancellation, no detached workers, no event-loop redesign,
and no new public concurrency APIs.

## M1: worker Diagnostic fidelity (the deferred fix)

Previously the replayed Error retained the built-in code (e.g.
`ffi.library_load_failed`, `io.*`, `package.*`) but the observer-side internal
Diagnostic was rebuilt as the generic `UserRaised` umbrella. Fixed by carrying
the worker's recoverable Diagnostic through worker completion state (per the
approved approach of carrying the structured diagnostic rather than inferring
codes from message text):

- `WorkerCompletion::recoverable(error_value, diagnostic)` now carries an
  optional recoverable Diagnostic alongside the Error value.
- The async and thread workers capture `active_diagnostic_` when a recoverable
  occurred and store it in the completion.
- `await` and `join` observers install the carried diagnostic when present
  (preserving the exact built-in `DiagnosticCode`, disposition, defining
  origin and any frames) and fall back to `UserRaised` only for arbitrary
  user-created `user.*` errors.

Verified by a `parser_statement_state_unit` assertion: a worker hitting a
built-in recoverable (missing import source) is observed with
`IoImportSourceUnreadable` (not `UserRaised`) through `await`, while a worker
throwing `user.custom` is observed with `UserRaised` internally. Error
`code`/`category`/`message`/`source` were already correct and remain so.

## Repeated observation (immutable replay)

`await`/`join` of an already-completed failure replay the identical Error
(code, category, message, source), with no mutation from the first
observation, no frame loss, and no built-in->`user.raised` conversion. Pinned
for both futures and threads.

## Fatal worker failures stay fatal

Programmer errors, invalid runtime invariants and internal unexpected
exceptions produced in a worker bypass `catch` at the observer; only
recoverable worker failures are catchable. Worker entry boundaries already
wrap evaluation in `catch(const std::exception&)` / `catch(...)` so no C++
exception escapes a worker thread to `std::terminate`; the outcome is a
structured fatal `internal.unexpected_exception` (or the known Nift outcome is
preserved). This is containment of host/runtime exceptions only — no signal/
SEH crash recovery was added.

## Nested pool progress (no starvation)

Verified with time-bounded tests: async parent awaiting async child, thread
starting an async task and waiting, async task joining a thread, several
parents each awaiting children, and a single-worker nested case all complete.
The pool executes queued work while a worker waits on nested work, preventing
fixed-pool starvation for these supported nesting shapes. Worker counts of 1,
2 and the normal count are exercised by the wall (the pool size is not pinned
in the test).

## Unobserved failures

Unobserved async/thread failures (never awaited/joined) tear down cleanly at
top-level finalization: no terminate, no crash, no leak, no lost worker
ownership, and the script result is deterministic. No new unhandled-rejection
policy was invented.

## Failed-import worker lifetime

Re-certified: an import that creates a worker (thread or future) and then
fails remains deterministically fatal `internal.import_lifecycle` regardless
of the underlying recoverable failure. Ownership stays retained through the
existing blocking top-level finalization. No cancellation redesign, no detach.

## Worker-owned resource lifetime

Workers legitimately own streams (open/write/close inside the worker) and the
results persist correctly after `join`; worker-created resources are cleaned
up deterministically and are not double-cleaned or rolled back while the
worker still needs them.

## Transfer restrictions

Re-certified: timers are non-transferable out of a worker
(`thread result contains a non-transferable timer`). Error values and raw
handles are not silently deep-copied or stringified to move them between
workers; the existing authority-sensitive policy is preserved.

## Origin and provenance in workers

A worker spawned from imported/module code retains the defining source
authority: the Error origin is the defining worker source (verified for both
imported functions and package modules), the observer frame describes
await/join propagation, and package/module authority is unchanged (no CWD
fallback).

## Race analysis (TSan) / ASan

The existing TSan instrumented concurrency binaries and focused worker
stress scenarios are run as part of the CP7 review gate; any finding is treated
as a blocker until understood (none pending at commit time). ASan/UBSan worker
scenarios cover worker completion during parser destruction, repeated observer
access after completion, nested worker teardown, failed imports with worker
resources and FFI handle cleanup.

## Performance

Successful-path concurrency (async submit/await, thread create/join, repeated
completed-result observation) is compared against `4368dea`; the only added
cost is on the failure path (carrying the structured diagnostic), which is
deliberately not optimized away.

## Wall

```sh
make test-v46-b4-cp7
```

runs the Batch 4B/CP5a/CP6/CP5b aggregate gate then the focused CP7 wall. The
wall covers: built-in DiagnosticCode preservation through await/join (C++
assertion), `user.*` -> `UserRaised` internally, repeated await/join replay,
fatal worker failure bypassing catch, nested async progress (child/thread/
join/several-parents), unobserved async and thread teardown, imported and
package worker provenance, the failed-import worker-lifecycle fatal rule,
worker-owned stream lifetime, and timer transfer restriction.

The existing v4.5 concurrency walls, the import-worker ownership wall, CP6/CP5b
walls, CP5a/FFI walls, and the Batch 4B aggregate remain green.