# CP23 — genuine parallel execution, isolates, channels and cancellation

Status: **Codex implementation candidate frozen for independent review; not accepted or integrated.**
Original refinement foundation: `6cf73bed` (tree `eac80979`). This record incorporates the owner decisions of
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
- Per-isolate constants: `Proto` code/notes are cloned and remapped from neutral metadata; constants
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

## 4a. Milestone 4 shared objects (implemented)

Shared `mutex`, `atomic<int>`/`atomic<bool>` and `channel` handles live in a
process-wide registry protected by one mutex/condition variable; a handle is
`(HandleKind, id, generation)`, never an isolate heap pointer. Values stored in
a mutex or carried by a channel are kept as `TransferGraph` wire data and are
encoded on the producer thread and decoded on the consumer thread. The
transfer graph encodes shared handles by id and still refuses live file/stream,
process and other non-shared resource handles. Parallel futures transfer by completion-record identity.

- `mutex()` / `mutex(initial)` supports `lock`, `try_lock`, `unlock`,
  `locked`, `get` and `set(v)`. `lock` waits without pinning the isolate;
  re-locking by the same owner, unlocking by another owner, and `get`/`set`
  without the lock are catchable `runtime.invalid_argument` Errors. A
  cancellation request wakes a waiting `lock`.
- `atomic<int>(v)` / `atomic<bool>(v)` support `load`, `store`, `exchange`,
  `fetch_add`, `fetch_sub` and `compare_exchange`, plus legacy's compound
  (`+=`, `-=`, `%=`, `&=`, `|=`, `^=`) and prefix/postfix increment/decrement
  operators. Signed 64-bit add/sub overflow raises catchable
  `runtime.atomic_overflow` and leaves the value unchanged.
- `channel(capacity)`, `send`, `recv` and `close` are ordinary calls. Capacity
  0 is a rendezvous; a positive capacity is a bounded FIFO. Sending on a closed
  channel raises catchable `runtime.channel_closed`; a closed and drained
  `recv` returns an Error sentinel with code `runtime.channel_closed` (never
  `null`). `close` is idempotent and wakes all waiters.
- `cancel(task)` is idempotent and cooperative: it sets the task record's flag,
  wakes blocked `send`/`recv`/`lock`/`await`/`join`, and never terminates a
  thread. The observed operation raises the catchable `runtime.cancelled`
  Error; catching it consumes that request so cleanup can run. A task settles
  exactly once as done, failed or cancelled.

`type()` reports `mutex`, `atomic<int>`, `atomic<bool>` and `channel`.
Cooperative (`NIFT_VM_COOPERATIVE_THREADS=1`) runs share the same registry;
blocking waits there still occupy the single scheduler thread, so cancellation
of a CPU-bound cooperative loop remains limited to its safepoints.

## 4b. Legacy v45 compatibility (implemented; not checkpoint acceptance)

- `@fn[async]` script declarations are admitted on the VM path. Their deferred
  body is still parsed in deferred mode, so template-only forms that become
  Opaque remain refused rather than acquiring different semantics.
- `thread(callee, args…)` accepts a named function, closure or other function
  value. The VM validates callability and arity at run time with the legacy
  `thread:` diagnostics.
- A `FutureObject`/thread handle crosses isolates as a `TaskHandle` wire node
  carrying the process-unique `ParallelTaskRecord` identity, never a heap or
  `FutureObject` pointer. Decoding creates an isolate-local future that shares
  the record, so `join`, `await`, `done`, `status` and `cancel` address the
  same task from another worker.
- A managed file/stream/process handle in a `thread(...)` argument is refused
  with a specific non-transferable resource error. Shared mutex/atomic/channel
  handles continue to cross by registry identity.

Focused evidence: `test-rt-isolate-transfer-unit`, `test-cp23-isolate-task-unit`,
`test-cp23-parallel-unit`, `test-cp23-shared-unit`, and the v4.5 async/thread/
mutex/atomics scripts under `NIFT_VM=require`. This does not claim the full CP23
acceptance matrix (sanitizers, race, scaling or platform evidence).

## 5. Preserved admission boundaries

Parallel mode now admits captured closures, structs, collection callbacks,
CPU loops/recursion, blocking file/stream operations and scheduling observations
with explicit execution witnesses. Cooperative mode keeps its schedule-safety
checks. `tests/runtime2/cp23/CP22-CLASSIFICATION.tsv` records all 85 historical
refusal fixtures and identifies preserved semantic/type/binding refusals.

Future escape/observation forms and named async callable values still subject
to CP22's unresolved semantic/admission rules remain refused before effects.
Live file/stream/process resources do not cross isolate boundaries. `run()`
is still not a compiled Host-process operation on this foundation. These are
explicit limitations, not successful VM executions through fallback.

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

## 8. Codex completion architecture (2026-10-29)

The development foundation is the local reconciliation with accepted
`475f5192f6ca510d19602e42041d6e10a88efc78` / tree
`339bb7fc60ac3245e7e4d6a4ddc54476a321811b`. DeepSeek's frozen `a5179308` source,
bundle and historical evidence are preserved separately. This record does not
assert independent acceptance or integration.

### Source ownership and symbols

The submitting thread snapshots metadata and all constant Values before a
worker starts. Queue records contain immutable metadata without Values and
thread-neutral graphs; even ignored embedding args/globals are cleared on the
source thread. Only bytecode-referenced atom names are copied; a source table with 70,000
unrelated names does not overflow the worker operand namespace. Worker clones
remap all six atom operands: ObjectSet, NewStruct,
StructAdd, FieldSet, GetMember and FieldGet. Each task has a fresh AtomTable;
reusable workers do not accumulate task symbols indefinitely. Constants,
arguments, globals and callable captures are decoded in the destination heap.

Encoding uses a work list. Decoding validates graph edges with an explicit
stack, allocates all skeletons, then links them. Neither traversal consumes C++
stack proportional to graph depth. Partial construction cuts edges while
nodes are anchored, including allocation failures. Function wire indices also
carry a program domain, preventing an unrelated program from interpreting an
index as its own callable.

### Pool, cancellation and shutdown

A reusable FIFO pool has a CPU-permit limit (default min(8, hardware count),
explicit environment range 1..256). Outstanding-task admission is a separate
resource budget: 16,384 unfinished records by default, independent of CPU
permits (see the sleeping-task correction below). Saturation returns an explicit task-admission
failure rather than growing an unbounded queue. This is refusal backpressure,
not an indefinitely blocking spawn operation.

An isolate stays on its OS thread. Blocking Host calls, resource-context closure, and channel/mutex/await/timer waits
return their CPU permit. The FIFO reader/writer witness requires progress with
one permit; native I/O does not pin it. Reusable compensating workers provide progress for
nested waits and producer/consumer loops while keeping thread count bounded
by `permits + blocking_capacity`, independently of the outstanding-task budget. Resuming workers
use a separate condition variable and take priority over new queued work.
No worker is detached. Shutdown first closes admission and worker creation,
cancels queued/running work, wakes native waits, and joins every thread.

Cancellation remains cooperative and catchable; caught cancellation can run
cleanup. User native code that never reaches a cancellation point can delay
shutdown. Mutexes are not silently unlocked when an owner fails: scripts must
unlock in their cleanup. Ownership tokens are process-unique VM tokens, not
reused record addresses or thread hashes. Blocked lock acquisition is
cancellable. Internal worker failures retain a fatal Internal stop and are
observed at structured completion even without an explicit await.

### Neutral and isolate heap lifetimes

Shared handle boxes own neutral backing references. Registry entries are weak;
queued/running tasks and wire graphs pin actual backing. A non-allocating
collector under the registry mutex counts neutral internal edges and clears
unreachable channel/mutex/task-record cycles. Pending tasks are conservative
roots. Graph decoding and release never hold a completion-record lock while
entering that collector.

Decoded cyclic Values are tracked only within their owning thread. Worker
scope collects unreachable cycles and releases remaining imported graph
cycles after every task-owned Value has died. Root/embedding boundaries
collect unreachable imported cycles after VM members die; embedding-retained
Values survive those boundaries. Embedders that retain then drop cyclic
Values outside a VM boundary can call `collect_transfer_heap()`; the root
arena also collects at later decodes/boundaries and thread exit. Raw cyclic
Values manually constructed by a C++ caller outside this transfer ownership
mechanism remain that caller's responsibility. Script cycle-prevention rules
are unchanged.

### Output and shared operations

Each worker publishes complete print chunks into a mutex-protected neutral
sink. The root drains it before printing, observing an await result, and
finishing teardown. A task's print therefore precedes a completed join/await,
including foreign task handles and channel synchronization. Cross-worker
order remains scheduling-dependent. The old spawn-order-only completion
buffering failed happens-before tests and is replaced; output is emitted once.

Rendezvous sends track consumption of their own queue item. Cancellation
removes only that sender's item; close rejects unconsumed rendezvous sends.
Positive-capacity channels drain accepted buffered messages after close.
Mutex waits re-check the predicate after reacquiring a CPU permit. Atomics
retain linearizable updates and overflow-without-change errors.

### CP17/CP19/CP21 boundary

Every worker VM owns its own accepted CP17 HandleTable/context and settles it
before publishing completion. Live resource transfer is refused. No
ProcessGroup, JobTable, terminal or session-resource owner is duplicated.
Accepted Host/handles/Process/JobControl source is unchanged. CP19 has not been
independently integrated here; combined persistent-session verification stays
an explicit integrator obligation. CP20 F6 is preserved.

Verification commands and qualifications are in
`tests/runtime2/cp23/README.md`; final results and immutable source/binary
identities accompany the candidate handover.

The default atom-table pointer is cached after initialization, and shared-native
metadata is populated only for the shared-operation family. Ordinary native
calls do not repeatedly acquire task metadata. Initial and corrected performance
observations remain in the delivery evidence; no threshold is changed.

### R2 completion/cancellation linearization

Completion claims the cancellation decision under the task mutex before encoding
its terminal payload. `cancel` before that claim converts a normal return to a
catchable `runtime.cancelled`; an already observed script Error retains its
identity and fatal Internal failures remain fatal. Cancellation after the claim
is a no-op, including repeated cancellation of a terminal task. The state stays
Pending until the immutable payload is ready, then publishes exactly once.
Repeated await/join decodes the same terminal outcome; unawaited cancelled tasks
are drained. Native blocking work still observes cancellation cooperatively and
can delay shutdown. One CPU permit is supported, including compensating workers
for blocked waits; simultaneous CPU overlap is asserted only with two or more
permits. Status witnesses use channel synchronization rather than sleep timing.

### Sleeping-task admission correction (after R2)

The old `active_` admission counter counted all unfinished work: queued,
executing, timer-suspended, native/shared-wait blocked, and completion cleanup.
It was not a CPU counter. Completed records retained by future handles no longer
consume this budget once worker cleanup is finished. The CPU counter is
`computing_`; `blocked_` includes parked OS workers and workers awaiting a new
permit, and `resuming_` gives those returning workers priority.

The former `64 * permits` capacity incorrectly made 200 valid sleepers fail on
one/two CPU permits. The corrected default is a fixed 16,384 unfinished neutral
records, covering the accepted 10,000-sleeper workload with bounded headroom.
It is a count of queued/executing/storage obligations, not a promise of 16,384
OS stacks or CPU slots. The C++ embedding constructor may deliberately choose a
smaller task budget (clamped to 1..16,384); the CLI always uses the fixed default.
Budget exhaustion retains the existing task-admission diagnostic. No unbounded
queue or fallback after effects is introduced.

CPU execution is still bounded by the requested permits. A timer suspension
returns its CPU permit. Timers do not count against the separate 1,024 ordinary
blocking-worker budget; physical reusable workers are bounded by `permits + blocking_capacity`
(default `permits + 1,024`). Timer/cleanup compensation is separately capped
at 64 additional workers, so plain sleepers do not create thousands of OS
threads. The ordinary blocking budget retains the former policy's largest
possible capacity rather than reducing supported await chains. Embedders may
explicitly configure a smaller blocking budget (the saturation test uses 64).
Ordinary blocking I/O, shared waits and foreign-future waits reserve that stack
budget only while actually blocked, not while waiting to reacquire a CPU permit.
Exhaustion reports the existing uncatchable `limit.exceeded` VM fault, through
normal VM unwinding and resource finalization. It does not escape as a C++
exception, become Internal or become cancellation. Rendezvous rejection removes
its unconsumed item. Timer and emergency cleanup waits retain progress without
being rejected by the ordinary blocking budget. Uninterruptible native work can
still delay shutdown; no forced thread termination is added.

To multiplex the required plain sleeping tasks without one OS thread per task,
a worker may execute bounded queued work while its timer is suspended. This is
restricted to a conservative finite straight-line opcode whitelist (loads,
moves, returns and non-negative literal sleeps) with no
calls, jumps, handlers, callbacks, shared waits or native I/O. Unknown/new opcodes
are ineligible. In particular an awaiter of the sleeping ancestor is never run
on that ancestor's C++ stack. Small input/constant/atom descriptions (at most 256 KiB), matching arity and a
bounded global window are required. At least 4 MiB of the helper's own heap
budget must remain above the thread's current charged heap before selection;
otherwise it stays queued for ordinary dispatch. This prevents nested thread
accounting from spuriously rejecting an individually valid isolate. Low-budget
isolates are covered by a 32 KiB positive regression. Nesting is bounded at 32; every nested VM retains
its own atom and transfer-heap scopes on the same OS thread. Helpers borrow the
worker's existing CPU permit. Other work continues on the normal bounded pool.
A timer never resumes before its deadline; completion order otherwise remains
unspecified under the existing happens-before contract.

Zero-duration sleep validation and a worker HandleTable which has never created
a resource retain their CPU permit; neither can perform a blocking Host action
or lifecycle settlement. The context still closes and reports normally. Actual
resource cleanup continues to return the permit. Separately, an empty neutral
registry resets its cycle-scan epoch under the registry lock: backing/task
registration precedes edge publication, so no live edge is hidden. This prevents
a previous session's transferred future from imposing full graph scans on later
pure-timer runs while retaining cycle reclamation within every nonempty epoch.

Permanent admission tests cover 200 and 10,000 sleepers at 1/2/4/8 permits,
repeated awaits, suspended cancellation, heap constants, a pending-ancestor
awaiter, CPU-slot parking, sampled executing-slot bounds, physical-worker bounds,
blocking receiver/foreign-await/rendezvous/full-buffer exhaustion, 100-level
await-chain compatibility at every permit count, cancelled shutdown and zero
registry survivors. The ownership suite retains a genuine explicitly configured
64-record admission-limit witness. Allocation mode `timers` injects failures in
four concurrent sleeping tasks, including nested execution. Real topology and
CPU-parking mutants are required to fail their specific completed-suite witnesses.
Local 10,000-sleeper latency remains exploratory; the unchanged <0.2 s gate must
be assessed on the reference host and is not certified by these local runs.

The overload investigation also exposed a missed shutdown cancellation wake:
record cancellation uses the task mutex while shared waits use the registry
mutex. Cancellation notifications now acquire the registry mutex after releasing
the task mutex, preventing a notification from falling between a false wait
predicate and the condition-variable sleep. The same synchronisation applies
to explicit cancellation, emergency task cleanup and pool destruction. This
changes no Error or cancellation-completion precedence. Diagnostic-only rescue
notifications used to identify the race are absent from the regression suite.
