# CP23 — genuine parallel execution, isolates, channels and cancellation

Status: **refinement accepted; implementation in progress.** Foundation
`6cf73bed` (tree `eac80979`). This record incorporates the owner decisions of
2026-10-29. It does not claim checkpoint acceptance.

## 1. Scope

CP23 replaces cooperative-only concurrency for legacy-supported programs with
genuine parallel execution on the VM:

- parallel `thread()` and CPU-bound/blocking `async`;
- capture-only isolates with explicit copy/freeze transfer;
- shared `mutex` and `atomic<int>`/`atomic<bool>`;
- `channel(capacity)`, `send`, `recv`, `close`;
- cooperative, idempotent `cancel(task)` with observable safe points;
- structured completion, exceptions, cancellation and resource cleanup;
- no permanent refusal caused solely by missing parallel execution.

CP22's cooperative scheduler, futures, timers, internal teardown and Host
integration remain CP22-owned. CP22's nonblocking Host integration is not a
prerequisite for CPU-bound CP23 work.

## 2. Owner decisions

1. Channels and cancellation use ordinary function calls; no new syntax.
   Capacity 0 is rendezvous, positive capacity is bounded buffering. A closed
   and drained receive returns a distinct sentinel value, not an ordinary
   value. Invalid operations raise catchable Errors. Cancellation is
   cooperative and never forcibly terminates a thread.
2. Approved scaling minima: 2 workers 1.4x, 4 workers 2.0x, 8 workers 3.0x,
   with lower confidence bound meeting the target on a dedicated >=8-vCPU
   Linode. Sequential VM execution must remain within 3% of the accepted CP22
   baseline. Linode spending needs separate approval.
3. Separate VM isolates own stacks, frames, heaps, accounting and resource
   context. Ordinary values cross by copy/freeze preserving aliases/cycles.
   Task identity/completion lives in thread-safe records outside isolate heaps;
   raw `FutureObject`, frames, heap pointers and HandleBox objects never cross.
4. Direct cross-isolate transfer of live file/stream/process/HandleTable
   resources is initially refused. Per-isolate CP17 lifetime, cancellation,
   unwind and exactly-once cleanup remain mandatory.
5. CP22's nonblocking Host integration stays CP22-owned.

## 3. Architecture

- `IsolateRuntime`: one VM execution domain per worker task, with its own `Vm`,
  stack/frames, atom table and heap accounting view.
- `TransferGraph`: thread-neutral wire representation. Encoded on the source
  thread, decoded on the destination thread; object keys by text; aliases and
  cycles preserved; mutable heap objects are never shared.
- Per-isolate constants: `Proto` code/notes are shared read-only, but constants
  are reconstructed in the isolate heap before execution. `LoadK` must never
  read a parent-thread `Value`.
- `TaskRecord`: shared, mutex/CV-protected completion state. Isolate-local
  Future/Thread values reference it. It carries state, result/error wire,
  cancellation, output and structured-child count.
- Worker pool: bounded lanes; a lane executes one isolate at a time. Tasks with
  suspension (sleep/await/channel waits) must not pin a lane forever; the
  scheduler wakes them through task records.
- Shared objects (`mutex`, `atomic`, `channel`) live in an explicit
  process-wide/shared registry addressed by ids, not in isolate heaps.
- Cancellation: a task record flag, checked at calls, loop back-edges,
  coroutine safepoints and channel waits. Cancellation settles exactly once and
  wakes waiters/channel operations.

## 4. Required implementation milestones

1. Transfer graph and constants materialization; tests for aliases, cycles,
   atom keys, errors and refusal cases.
2. Isolate task entry/result API and wrapper execution; no parent heap access
   from worker threads.
3. Parallel `thread()` and `async`, structured completion, join/await, error
   transfer and drain.
4. Shared task records, cancellation, channel/mutex/atomic built-ins.
5. Remove parallel-only refusals while preserving CP17 resource refusals.
6. Differential, race, mutation, sanitizer and scaling suites.
7. Performance evidence and final traceability.

## 5. Temporary restrictions

Until their milestone lands, development may conservatively refuse
cross-isolate futures/thread handles, live managed resources, mutable captures
not yet representable, and programs requiring unimplemented shared objects.
These are implementation gaps, not acceptance rules, and must be reconciled
before freezing the checkpoint.

## 6. Acceptance

- All legacy-supported `thread()`, `async`, `mutex` and `atomic` operations
  execute on the VM without legacy fallback or parallelism-only refusal.
- No data races under TSan; ASan/UBSan/LSan clean for task/isolate lifecycle.
- Channels and cancellation meet the owner-defined semantics.
- CP22 cooperative regressions and CP17 resource contracts remain green.
- Approved 1/2/4/8 scaling and 3% sequential regression targets are measured
  on the authorised reference environment; local results are diagnostic only.
- Remaining platform, CP19/CP21 integrated-tree and Linode obligations are
  reported explicitly, not waived.

## 7. Known integration obligations

- CP22 nonblocking Host integration remains CP22-owned.
- CP17 §6.1 steps 3/5 (task-frame release, per-task handle contexts) must be
  implemented or explicitly refused at the isolate boundary.
- CP19 attachment interfaces and CP21 JobTable ownership are not modified.
- macOS/Windows worker/isolate execution remains unverified until CI/execution.
