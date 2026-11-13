# Async recursion memory safety (C140; production checkpoint)

Owner authority:
- D-077 #2 (2026-11-12): "Insert a bounded async task-memory safety checkpoint immediately after the output-order
  repair. The demonstrated 100 MB budget growing to 1.56 GB and terminating with exit 70 is unacceptable. [...] A depth
  limit must not be presented as comprehensive memory accounting unless the evidence establishes that guarantee. Stop
  if the repair requires broad scheduler or coroutine restructuring. No performance re-baselining."
- D-077 #5: item 2 of the pre-certification production order.
- The coordinator's approval of the design (DESIGN-ASYNC-RECURSION.md, scratch): (a), (b), and the nested-snapshot
  fix (c) "include (c) in C140", with the tests and mutants listed in section 4.
- D-079 #3 (2026-11-12): the seven questions answered in section 3. The coordinator's correction (Option C), confirmed
  by D-080: the new limit errors are the same uncatchable form as every other `limit.exceeded` (the host table keeps
  the code fatal; legacy's thread exhaustion is not catchable either); the existing admission and 1024 blocked-wait
  contracts are unchanged; a budget or thread-creation failure in an un-awaited, detached or abandoned task still ends
  the run (exit 1); an ordinary failure of an un-awaited task keeps the established semantics.

## 1. The defect

`fn[async](f(n)) { if (n == 0) { return 0 }; x := await f(n - 1); return x + n }` / `print(await f(D))`. Every
level is a parallel task: a new isolate on its own worker thread, its parent blocked in `wait_parallel`.

- **Per-level growth (fixed by (c)).** An isolate's program is a clone whose proto 0 is its `<isolate-task>` wrapper
  (constants = globals + arguments + the function; registers = globals + arguments + 1) and whose proto operands
  (`Call`, `Spawn`, `LoadFunction`, `Closure`) are shifted by one. A nested spawn snapshotted that clone, wrapper
  included, and seeded its whole register window as shared variables, so every level added a wrapper, its constants
  and two global slots: protos grew k + 2, constants k^2 + 2. Summed copied constants: 6.1 MB at depth 30, 45.7 MB at
  depth 60. Peak RSS grew cubically (275 MB at depth 100 before (c); "quadratic" until C140a, F6, section 10).
- **No runtime-wide bound (fixed by (b)).** Heap accounting is per thread (heap_account.cpp), so each isolate had
  its own budget and nothing bounded their sum.
- **Exit 70 (fixed by (a)).** A worker thread that could not be created threw `std::system_error`, mapped to Internal
  in `start_parallel_task` ("cannot enqueue parallel task: ...") and in `run_parallel_task` ("parallel task execution
  failed: ..."): "NIFT_VM internal failure", exit 70.

## 2. What is implemented

- **(c) Nested snapshot** (isolate_snapshot.cpp, vm_coro_parallel.cpp, isolate_task.{h,cpp}). A spawn inside an
  isolate detects its wrapper (proto 0 has no transfer wire) and snapshots the program from proto 1 on, undoing the
  wrapper's operand shift (`proto_operand`, shared with `remap_code`); the entry index is shifted back the same way.
  Only the original shared variables are seeded: the first `nregs` registers of the original main (proto 1), never
  the wrapper's argument and function registers. The image a child receives is the original program at every depth.
- **(b) Runtime-wide accounting** (parallel_task.{h,cpp}, parallel_task_start.cpp). At admission (the single
  choke point, `start_parallel_task` -> `ParallelRuntime::submit`, under the runtime mutex) each task is charged its
  bootstrap bytes (seed graph + constants graph storage + program image: protos, code and offsets) plus
  `kIsolateOverhead` = 64 KiB. The sum of every unfinished task's charge, in the whole runtime (all nesting levels,
  async and thread(), awaited or not), may not exceed the root heap budget (`NIFT_VM_HEAP_BUDGET`, default 1 GiB). A
  refused task is never charged (the charge is added only after it is queued). The charge is released when the task
  completes (`execute_record`, also for a timer helper's nested run) or when an un-awaited task is cancelled before it
  runs (`cancel_queued`, the teardown of its parent).
- **(a) Controlled failure** (parallel_task_start.{h,cpp}, parallel_task.{h,cpp}, vm_coro_parallel.cpp,
  vm_builtin.cpp). `std::system_error` from creating a worker thread is `limit.exceeded` with the text `parallel task
  resources exhausted`, never Internal: at the spawn (`submit`), at a blocking wait (`ParallelBlock`, refused like the
  blocking capacity, at the waiting instruction), and at a timer or cleanup wait (`run_parallel_task`, the task fails).
  A spawn refused by the budget or for want of a thread raises the fatal error **at the spawn**, awaited or not (so an
  un-awaited refused spawn is never silently dropped). Any other exception keeps its Internal mapping. The existing
  admission refusal (`parallel task admission limit exceeded`, a failed future) is unchanged.
- **(d) Un-awaited limit failures end the run** (parallel_task.h, parallel_task_start.{h,cpp}, parallel_task.cpp,
  vm_coro_run.cpp). When any task, at any depth, fails with the budget refusal or `parallel task resources exhausted`
  (`note_parallel_limit`, at the task's completion), its runtime records the first such failure; the root, after its
  teardown has waited for every task (`wait_idle`), ends the run with it if the run had otherwise succeeded: exit 1,
  `limit.exceeded`, the failing task's message and position. An awaited failure already reaches the root through the
  await; this covers a task nobody awaits (an abandoned `async` call, a detached `thread()`, a parent that returned
  without awaiting). An ordinary error of an un-awaited task keeps the established semantics (ignored).

## 3. The owner's questions (D-079 #3), with evidence

1. **Why the old snapshot retained so much memory; why the corrected lifetime is sound.**
   - Retention (section 1, measured with instrumentation in the design investigation): each nested spawn snapshotted
     its own clone, wrapper included, and seeded the wrapper's registers, so the image and the seed grew every level
     (protos k + 2, constants k^2 + 2, two global slots per level; 6.1 MB of copied constants at depth 30, 45.7 MB at
     60). Every level's image stayed alive for as long as that level was pending, so the total was cubic, O(D^3)
     (corrected in C140a, F6).
   - Now: the per-isolate charge is exactly (D + 1) x c at depths 1, 2, 20 and 21 (`charge-constant-*`): the image and
     the seed are the same at every depth; RSS is linear (9 MB at depth 10, 22 MB at 200, 80 MB at 1000).
   - **Keepalive.** A child's image is a new `Program` built by `snapshot_isolate_program` on the spawning thread:
     code, offsets, notes, struct descriptors and names copied by value, constants and seeds encoded as TransferGraph
     wire data, atoms by text; `start_parallel_task` refuses a snapshot whose metadata holds any Value. The record
     owns the image (`program`, `options.program_keepalive`); the child's own clone is owned by the child's Vm
     (`execute_isolate_task`, `options.program_keepalive = clone`). Nothing in the image or the wire data points
     into the parent's clone or heap.
   - **Renumbering.** A clone shifts proto operands by +1 for its wrapper (`remap_code`); a nested snapshot shifts them
     back by `first` (= 1) and the entry by the same amount, so a child's image is numbered exactly like the root
     program. Function values cross as wire indices: every isolate's `CloneTransferMap` maps its clone's protos to the
     root's wire indices (`proto_wires`, recorded at the snapshot), the wrapper having none, so a function passed at
     any depth decodes to the same function. Pinned by exact values against legacy (`functions-*`, `exact-*`) and by
     M4 (an off-by-one gives another value with exit 0).
   - **No dangling references after the parent ends.** A parent isolate may finish (and free its clone and heap)
     while its child runs (an un-awaited child; or the child is cancelled at the parent's teardown). The child holds
     only its record's image, its decoded copies in its own heap and its own clone; the result travels back as wire
     data in the record, decoded by whoever awaits it. `release-unawaited` ends parents with children queued; the
     ASan/TSan gates of test-rt-vm-mutants and the cp23 ownership tests run over the same paths.
2. **Real OS threads.** Yes: each pending await level blocks a worker thread (`wait_parallel` -> `ParallelBlock`),
   and the pool starts a compensating worker for every blocked one (`ensure_worker_locked`; at most 8 CPU permits +
   1024 blocked). gdb at depth 1100: 1027 threads (543 in `resume_worker`, 266 in `worker_loop`, 217 in
   `wait_parallel`). Each thread reserves an 8 MiB stack (ulimit -s 8192): address space, little RSS.
3. **What is exhausted at depth 1000.** Under the default settings, nothing: depth 1000 prints 500500 (about 60 s,
   79 MB RSS; also under `NIFT_VM_HEAP_BUDGET=100000000`, 64 s, 79 MB), and depth 1024 prints 524800 (53 s, 80 MB).
   Depth 1025 needs 1025 blocked waits and is refused by the existing pool cap, `parallel blocking wait limit
   exceeded` (75 s, exit 1). Under an address-space limit the thread stacks run out first. Confirmed with gdb
   `catch throw` in scratch under prlimit:
   - depth 600 under 2 GB: `Vm::Coro::wait_parallel` -> `ParallelBlock::ParallelBlock` ->
     `ParallelRuntime::block_worker` -> `ensure_worker_locked` -> `std::thread` -> `std::__throw_system_error`
     (EAGAIN), on a worker in `execute_isolate_task` / `run_parallel_task`. Before C140 this reached the
     `run_parallel_task` catch: `NIFT_VM internal failure: parallel task execution failed: Resource temporarily
     unavailable`, exit 70. Now `ParallelBlock` refuses the wait: exit 1 (`threads-*`).
   - a single `thread()` under 20 MB: `Vm::Coro::spawn_parallel` -> `start_parallel_task` -> `ParallelRuntime::submit`
     -> `ensure_worker_locked` -> `std::thread`: before C140 `cannot enqueue parallel task: ...` (Internal); now exit
     1 at the spawn (`enqueue-*`).
4. **How the limit is enforced before that exhaustion.** No depth cap. Two bounds, both from existing mechanisms:
   the heap budget, now charged runtime-wide by (b), and the pool's existing blocking capacity (1024 nested blocked
   waits, `kBlockedCapacity`), which bounds the threads. The fixed charge, 64 KiB, is measured: after (c), RSS grows
   about 65 KB per nested level (9 MB at depth 10, 22 MB at 200: (22 - 9) MB / 190 = 68 KB); the measured per-isolate
   charge including the bootstrap bytes is c = 73,164 bytes (async) and 71,553 (thread()). **Thread stack address
   space is not reserved or charged**: exhaustion under an address-space limit is not prevented; (a) turns it into a
   controlled exit 1.
5. **The error.** An ordinary `limit.exceeded` error, exit 1, an exact documented message, never exit 70, on every
   path: the budget refusal and a failed thread at the spawn (enqueue), at a blocking wait (worker start), and at a
   timer/cleanup wait (execution). Like every other `limit.exceeded` in this VM (the heap budget, the admission and
   blocking capacities; host table `{"limit.exceeded", Limit, false}`) and like legacy's thread exhaustion, it is
   **not catchable** (Option C). Exact forms (section 4, `pin-*`):
   - awaited, async: `error: main.f:5:14: future: (x levels) parallel task memory budget exceeded` (the spawn site in
     the deepest isolate; one `future: ` per awaiting level);
   - awaited, thread(): `error: main.f:5:8: thread: (x levels) parallel task memory budget exceeded`;
   - un-awaited, async or thread(): `error: main.f:8:3: parallel task memory budget exceeded` (at the spawn);
   - thread exhaustion: `error: main.f:5:8: future: ... parallel task resources exhausted` (the await that could not
     block) or `main.f:5:14: future: ...` (the spawn that could not start a worker; which comes first depends on
     scheduling), and for an un-awaited spawn `error: main.f:5:1: parallel task resources exhausted`;
   - `try`/`catch` around the await does not catch it (`pin-not-catchable`).
   - un-awaited: a refused spawn is raised at the spawn (`pin-unawaited-*`); a task nobody awaits whose own spawn or
     wait fails ends the run after the rest of the program: `main done` then `error: main.f:8:5: parallel task memory
     budget exceeded`, exit 1 (`abandoned-async`, `abandoned-thread`), or `main done` then `error: main.f:5:8:
     ... parallel task resources exhausted`, exit 1 (`abandoned-deep-*`). Never exit 0 with the failure dropped
     (M8 drops it and is caught).
   - An ordinary failure of an un-awaited task (`1 / 0`) is ignored, exit 0, by the established rule
     (cp22-coroutines.md L7, "Unawaited failure | Ignored; exit status unaffected"), on both engines
     (`ordinary-unawaited-async`, `ordinary-unawaited-thread`, compared with legacy).
   - Not covered: under an address-space limit a `malloc` can fail before a thread start does (glibc reserves a 64
     MiB arena of address space per thread); that is `std::bad_alloc`, an internal failure by the existing policy
     (exit 70; observed 4 times in 6 runs of an abandoned thread() recursion of depth 400 under 1 GB, gdb:
     `spawn_parallel` -> `parallel_live.push_back`). C140 does not change it; the thread-exhaustion tests run with
     `MALLOC_ARENA_MAX=1` so that the thread start is what fails.
6. **Release on failure and unwinding.** A refused spawn is never charged (the charge is added only after the record
   is queued and its worker started), never queued (`submit` pops it and restores `active_` when the worker start
   throws, so no partially published task remains: the record's weak entry in `records_` expires with it), and its
   record and future are dropped with the raising frame; the error is fatal, so nothing continues with a leaked task. Charges are released at completion and at
   cancellation before the run ends (`wait_idle` at the root's teardown); M2 and M6 show both releases are needed
   (`release-completion`, `release-unawaited`). A failed thread start restores the pool counters (`block_worker`'s
   catch) and creates no thread. Nothing hangs: every test runs under a timeout. A parent cannot continue after a
   refused spawn (the refusal is fatal to that isolate, and its awaiting parent fails in turn), so the case "a
   refused spawn inside a parent that keeps running" cannot occur.
7. **The budget is enforced independently of the thread bound.** `derived-depth-*` (budget 50.5 c: depth 49 runs,
   depth 50 is refused by the budget, 51 isolates, far below 1024 threads), `width-*`, `pin-*` and `abandoned-*`
   (10.5 c: the 11th live task is refused, awaited or not), and `capacity` (sleeping tasks hold no thread: the budget
   alone bounds them, section 6). Un-awaited tasks are charged exactly like awaited ones, so they cannot exceed the
   budget either.

## 4. Diagnostics (stable; known-limitations.md)

| Situation | Message | Code | Exit |
| --- | --- | --- | --- |
| A spawn would exceed the root heap budget | `parallel task memory budget exceeded` | `limit.exceeded` | 1 |
| A worker thread cannot be created | `parallel task resources exhausted` | `limit.exceeded` | 1 |
| More than 1024 nested blocked waits (existing, unchanged) | `parallel blocking wait limit exceeded` | `limit.exceeded` | 1 |
| More than 16,384 unfinished tasks (existing, unchanged; a failed future) | `parallel task admission limit exceeded` | | 1 |

Not catchable. Raised at the refused spawn (or the wait that could not block), with one `future: ` or `thread: `
prefix per awaiting level above it.

**Not comprehensive.** Each isolate's own live heap is still bounded by its own copy of the budget (thread-local
accounting), not pooled. The worst case is (live isolates) x (per-isolate budget) + the charged bytes, where (b)
bounds the number of live isolates by budget / charge. Thread stack address space is not charged. Pooling per-isolate
heaps would make heap accounting shared across threads, the restructuring D-077 #2 says to stop for.

## 5. Tests (`make test-runtime2-async-memory`, in `test-rt-vm`; tests/runtime2/async_memory.py)

Every run: scratch directory, `NIFT_VM=require`, a hard timeout and RLIMIT_AS (8 GiB; 1 GiB for the thread cases;
12-40 MB for the enqueue sweep). Async results are compared with legacy running the same program made synchronous
(legacy refuses an awaited async self-call: rescan-post-cp1.md section 13); thread() results with legacy directly.
31 cases:

- **Function values across nesting** (depth 4 and 5): `functions-async`, `functions-thread` (a top-level function, a
  function defined after the recursive one, one passed as an argument and called in the deepest task, one held in a
  shared variable, and an imported module function, at every level); `exact-async`, `exact-thread` (neighbouring
  functions of equal arity, so an index off by one gives another value with exit 0).
- **Shared variables**: `shared-async`, `shared-thread`: the deepest task sees exactly the original shared variables
  (integer, string, array, float, and one assigned after the function), and its own arguments.
- **Constant snapshot**: `charge-constant-async`, `charge-constant-thread`. B(D), the least `NIFT_VM_HEAP_BUDGET`
  under which depth D (D + 1 live isolates) succeeds, is found by bisection (every failing probe must be the budget
  refusal). It must equal (D + 1) x c exactly at D = 1, 2, 20 and 21, with c = B(2) - B(1): a snapshot that grows by
  one byte per level (a wrapper image, a wrapper register) fails. This uses only the existing budget variable; no new
  instrumentation. Measured on this host: c = 73,164 bytes (async), 71,553 (thread()).
- **Derived depth**: `derived-depth-async`, `derived-depth-thread`: under the budget 50.5 c, the derived maximum
  depth floor(budget / c) - 1 = 49 gives the exact sum and depth 50 is refused with the documented message.
- **Default budget**: `default-depth`: depth 300 still gives 45150 (unchanged). Derived maximum under the default
  budget: the accounting allows floor(2^30 / c) - 1 = 14,674; the existing blocking capacity (1024 nested blocked
  waits; depth D blocks levels D..1) bounds it first, so the maximum depth is 1024 (verified once, outside the gate:
  section 3 item 3).
- **Release**: `release-completion` (200 sequential awaited tasks under a budget of 3 charges);
  `release-unawaited` (one CPU permit; 5 parents each spawn 4 children without awaiting them and are cancelled while
  the children are still queued, so the children are cancelled un-run; then 8 tasks must fit at once under 12
  charges).
- **Exact refusals** under 10.5 charges: `pin-awaited-async`, `pin-awaited-thread` (depth 40: exactly 10 prefixes,
  the spawn site), `width-async`, `width-thread` (40 awaited sleeping tasks, at the 11th spawn),
  `pin-unawaited-async`, `pin-unawaited-thread` (40 un-awaited sleeping tasks: at the 11th spawn, nothing printed
  after it), `pin-not-catchable` (the same inside `try`/`catch`).
- **Thread exhaustion** (a): `threads-async`, `threads-thread`: depth 400 under a 1 GiB address space: exit 1 at the
  await that could not block or at the spawn that could not start a worker (either, by scheduling), only prefixes
  before `parallel task resources exhausted`. `enqueue-async`,
  `enqueue-thread` (with `MALLOC_ARENA_MAX=1`, see section 3 item 5): an un-awaited spawn under 12-40 MB of address space: each run either completes or is refused at
  the spawn with the exact message (exit 1), and the refusal is reached at least once; no exit 70.
  Not exercised by a test: a timer or cleanup wait without a thread (sleeping tasks did not start extra workers in
  probes up to 60 tasks); the code maps it the same way and M3 covers its mapping together with the others.
- **Un-awaited**: `abandoned-async`, `abandoned-thread` (budget), `abandoned-deep-async`, `abandoned-deep-thread`
  (threads), `ordinary-unawaited-async`, `ordinary-unawaited-thread` (L7, against legacy).
- **Capacity**: `capacity`: for the pinned sleeper program, B(200) = 200 x B(1) exactly; it prints c and
  floor(2^30 / c).

Mutants (`--mutants`, `NIFT_ASYNC_MEMORY_MUTANT`, built by sanctioned_mutant.py), each must fail the named cases:

| Mutant | Change | Caught by |
| --- | --- | --- |
| M1 | no charge | derived-depth-async, width-async, width-thread, pin-unawaited-async, pin-unawaited-thread |
| M2 | completion keeps the charge | release-completion |
| M3 | thread-creation failure mapped to Internal again (all three sites) | threads-async, threads-thread, enqueue-async, enqueue-thread (exit 70) |
| M4 | nested proto operand off by one | exact-async, exact-thread (by value: exit 0 with 703267, not 23577), functions-async, functions-thread |
| M5 | the wrapped image snapshotted again | charge-constant-async, charge-constant-thread |
| M6 | an un-awaited task cancelled before it runs keeps its charge | release-unawaited |
| M7 | wrapper registers seeded as shared variables | charge-constant-async, charge-constant-thread |
| M8 | an un-awaited task's budget or thread failure not recorded | abandoned-async, abandoned-thread, abandoned-deep-async, abandoned-deep-thread |

## 6. Behaviour changes

- Exit 70 (`NIFT_VM internal failure: parallel task execution failed: Resource temporarily unavailable`, or
  `cannot enqueue parallel task: ...`) becomes exit 1, `limit.exceeded`, `parallel task resources exhausted`. Legacy
  reports its thread exhaustion as an ordinary uncatchable error, exit 1 (depth 400 under 1 GB: `thread: ... Resource
  temporarily unavailable`): towards legacy.
- **Capacity.** A new refusal: the unfinished tasks of one run may hold at most the root heap budget in charges.
  The number is exact and deterministic: floor(budget / c), where c = 65,536 + the task's bootstrap bytes (its seed
  graph, and the whole program's constants graph and image), so it depends on the size of the program. The pinned
  workload (`SLEEPERS` in async_memory.py): c = 73,287 bytes, B(k) = k x c exactly (k = 1, 200, 1000, 2000
  measured), so floor(2^30 / 73,287) = **14,651** tasks under the default budget; verified once at the default
  budget (9 s sleeps): 14,651 tasks run (exit 0, 72 s, 261 MB RSS), the 14,652nd spawn is refused at its spawn
  (`capd.f:8:11: parallel task memory budget exceeded`, exit 1). A larger program charges more per task: the
  17,000-sleeper probe adm.f (a try/catch loop after the spawns) charges about 83.8 KB, and 12,810 of its tasks were
  admitted (counted with an intermediate build of the same accounting that counted the refusals); with this build
  its 12,811th spawn is refused, `adm.f:8:11`, exit 1 (21 s, 377 MB RSS). Spawning is slow at that width (most of
  the 72 s is the spawn loop); not changed here.
  Before (C139c binary): the admission capacity, 16,384 unfinished tasks, then `error: adm.f:1:1: future: parallel
  task admission limit exceeded` at the first await of a refused future, exit 1. The CP22 10,000-sleeper workload
  still fits (tests/runtime2/cp23/admission.py). Under a small `NIFT_VM_HEAP_BUDGET` correspondingly fewer tasks run.
  Legacy on the 17,000-task program grew to about 6 GB and was killed by signal 9 after 9.4 s (one observation, not
  repeated: the shared container's OOM killer).
- **Un-awaited tasks.** A budget or thread-creation failure in a task nobody awaits now ends the run with exit 1 after
  the rest of the program (section 2 (d)); before C140 such a task could only fail by thread exhaustion, as an
  internal failure (exit 70) or, for an un-awaited task, ignored under L7. An ordinary un-awaited failure stays
  ignored (L7).
- **Positions.** The new refusals are reported at the refused spawn (or at the wait), with one prefix per awaiting
  level, instead of the `1:1` that a failed future without an offset gives (the admission refusal keeps `1:1`).
- The earlier claim "depth 300 -> 45150" is unchanged under the default budget (and now costs 29 MB, not cubic
  memory). Under `NIFT_VM_HEAP_BUDGET=100000000` (the owner's demonstration) depth 1000 completes with 500500 at 79 MB
  RSS in 64 s (before: growth past 1.5 GB, then exit 70 or SIGKILL); the accounting allows about 1,365 levels there,
  so the blocking capacity (1024) is again the first bound.
- No existing test expectation changes; the admission and blocking-capacity refusals are untouched (their messages,
  codes, catchability and positions).

## 7. Not changed (scheduler; reported, not repaired)

Wall time is superlinear in depth: 1.7 s at depth 300, 7.9 s at 500, 20 s at 700, about 60 s at 1000 (1024: 53 s;
1025 refused after 75 s; 1100 after 87 s). All isolates of a run share the root's `ParallelWait`
(`run_parallel_task` gives a child the parent's wait state), so every completion wakes every blocked ancestor, each
of which then competes in `resume_worker` for a CPU permit. Repairing that changes the scheduler's wait structure,
outside this checkpoint (D-077 #2 stop condition). Memory is bounded meanwhile: RSS is linear, about 80 MB at depth
1000.

## 8. Size

Production source (`git diff --numstat`, split by part):

| Part | Files | Added | Removed | Of the added: test-only mutant switches |
| --- | --- | --- | --- | --- |
| (a) controlled failure | parallel_task_start.{h,cpp} (catch), parallel_task.{h,cpp} (ParallelBlock, run_parallel_task), vm_coro_parallel.cpp (raise at the spawn, the wait), vm_builtin.cpp | 34 | 4 | 5 (M3) |
| (b) accounting | parallel_task.h, parallel_task.cpp (submit, execute_record, cancel_queued), parallel_task_start.cpp (charge) | 30 | 1 | 7 (M1, M2, M6) |
| (c) nested snapshot | isolate_snapshot.cpp, isolate_task.h, isolate_task.cpp, vm_coro_parallel.cpp | 47 | 21 | 11 (M4, M5, M7) |
| (d) un-awaited limit failures end the run (D-080) | parallel_task.h (note_limit, take_limit), parallel_task_start.{h,cpp} (note_parallel_limit), parallel_task.cpp, vm_coro_run.cpp | 38 | 1 | 2 (M8) |
| Total | 11 files | 149 | 27 | 25 |

149 added, net 122: an overrun against the 40-80 line plan (the design note's own estimate was 60-95, the
coordinator's threshold about 120), reported to the owner; (d) is the D-080 requirement. The D-079 records are a
separate commit (4bcb2ef6).

## 9. Gates (run from scratch after the second container restart, in five groups; nift sha256
`e642c5d24986aea8e395be29112a35f1aa801e07096663730b86d37d46c0267f`)

- Group 1: test-rt-lint PASS; test-test-integrity PASS; test-runtime2-async-memory PASS (31/31, mutants 8 of 8
  caught); test-runtime2-output-order PASS; test-runtime2-safety PASS; test-runtime2-safety-mutants PASS;
  test-runtime2-host-deps **FAILED** (exit 2, the same 24 FAILs as the C129 baseline after normalizing scratch paths
  and timings); test-rt-builtins-guards PASS (callgrind, 35 workloads, 0 guard failures, no re-baselining); CP25-R
  inventory collect PASS, `--check` BLOCKED (exit 3: the 59 PENDING-OWNER reasons and the
  `defect:print-cat-output-order` row awaiting review, both unchanged by C140).
- Group 2: test-rt-host PASS; test-runtime2-cp16-n1 PASS.
- Group 3: test-rt-vm-mutants PASS (`make -k`, no failing target).
- Group 4: test-rt-vm PASS.
- Group 5: test-rt-host-mutants PASS.

Changed-test audit: one new harness (tests/runtime2/async_memory.py) and one new Makefile target
(`test-runtime2-async-memory`, a prerequisite of `test-rt-vm`); no existing test or expectation is changed.

## 10. C140a (review REVIEW-C140-ASYNC-MEMORY, D-081)

Sections 1-9 are the C140 evidence as committed in 3759f481 (only the word "quadratic" is corrected to "cubic",
F6). C140a changes:

- **F1 (fabricated position).** A detached recursion whose parents return without awaiting or joining
  (`fn(w(n)) { if (n == 0) { return 0 }; t := thread(w, n - 1); return 1 }` / `t := thread(w, 400)`, 1 GiB address
  space) reported `ab.f:1:1` in 4 of 8 runs of the frozen binary (3:8 otherwise). Cause, found with per-site probe
  messages: a parent whose body is done waits only for its un-awaited child (`Coro::next` -> `wait_parallel`); when
  that wait cannot start a worker the refusal is raised with no current instruction (offset 0). Now the task records
  its spawning instruction (`spawn_proto`, `spawn_instr`, `spawn_offset`), and such a refusal is reported at the
  oldest live child's spawn. The same record field gives a timer/cleanup-wait failure the task's spawn position (F8).
  *Tests (corrected in C140b, D1/T1; section 11):* `detached-async` and `detached-thread` are deterministic: the
  fault-injection build refuses exactly that wait (`wait:1`), and every run must report the child's spawn (6:3,
  6:8); M9 (the position fix removed) reports 1:1 in every run and is caught every time. The real-limit detached
  recursion is no longer a gate case: its position failure was scheduling-dependent (the reviewer measured about
  23% of runs on M9, not the 50% assumed here before).
- **F2 (over-release).** `release_locked` no longer clamps: a release larger than the reserved total aborts (it would
  be a defect of ours). Test `release-exact` (the reviewer's reproducer: one live 3 s sleeper, one awaited 1 ms task,
  three more sleepers under 3.5 charges: the third, the 4th live task, is refused at 9:7). Mutant M10 releases a
  completed task's charge twice and is caught (`release-exact`, `release-completion`: the abort); with the old clamp
  the same double release would have admitted the 4th task, which `release-exact` detects too.
- **F3 (D-081, Option B).** `std::bad_alloc` while a task is created is the ordinary limit `parallel task resources
  exhausted` (`limit.exceeded`, exit 1, not catchable), raised at the spawn, at exactly the two creation boundaries:
  the spawn/start path (`Coro::spawn_parallel` wraps the whole creation, `create_parallel`: record, future, seed and
  snapshot, the `parallel_live`/`parallel_output` entries and `start_parallel_task`) and the enqueue/submit path
  (`ParallelRuntime::submit`, which already rolls back its queue entry and `active_`; the spawn/start path also
  covers the transfer encoding of the task's arguments, confirmed under D-081 by D-082; `start_parallel_task` rethrows
  to the spawn). Rollback is the thread-failure rollback: the two lists get their entries **before** the start (so
  nothing can throw after a task is published) and lose them again unless it started; a task is charged only after a
  successful submit; the record and future die with the frame. Everywhere else `std::bad_alloc` stays Internal.
  - Real address-space evidence: the abandoned thread() recursion of depth 400 under 1 GB (un4.f), which gave exit
    70 (`parallel_live.push_back`, then `snapshot_isolate_program`, both on the spawn path, gdb) in 9 of 16 runs
    before F3, gave exit 1 at the spawn in 16 of 16 after it. Not a gate case: not deterministic.
  - Deterministic tests, default environment (no `MALLOC_ARENA_MAX`): a **test-only fault-injection build**
    (`NIFT_ASYNC_MEMORY_MUTANT=12`, made by sanctioned_mutant.py for the run and deleted after it) throws
    `std::bad_alloc` at the n-th pass through "start" or "submit" (`NIFT_TEST_PARALLEL_BAD_ALLOC`). It is compiled
    only under a `*MUTANT*` define, which the production build guard refuses, so the production binary has no hook
    (its strings contain no `NIFT_TEST_PARALLEL_BAD_ALLOC`; the macros expand to `static_cast<void>(0)`).
    `alloc-start`, `alloc-submit`: `before`, then `error: main.f:6:12: parallel task resources exhausted`, exit 1.
    `alloc-rollback-start`, `alloc-rollback-submit`: the failed creation is inside an abandoned task; the root then
    needs three live sleepers under 3.5 charges (a leaked charge or a listed task would refuse the third or hang
    the end): `all live`, then the noted failure at 6:8, exit 1. Mutant M11 (the conversion removed, hook kept, the
    frozen behaviour) gives `NIFT_VM internal failure: out of memory (std::bad_alloc)`, exit 70, and is caught.
  - `threads-default-environment`: thread exhaustion without `MALLOC_ARENA_MAX`, at configurations shown
    deterministic here (15 of 15 runs each): the first worker of a run under 12-40 MB, and an awaited async
    recursion of depth 400 under 1 GiB: exit 1, the exact message, never 70.
- **F8 (timer/cleanup waits).** `timer-wait-thread`: with the fault-injection build, the timer wait of a sleeping
  thread() task cannot start a worker (`timer:1`, `std::system_error`): `before`, then `error: main.f:6:6: thread:
  parallel task resources exhausted` (the task's spawn), exit 1; this exercises the `run_parallel_task` catch end
  to end (M3 maps it to Internal as well). **The cleanup wait is a new finding, not repaired:** it runs in
  `Vm::coro_abort_parallel`, which is `noexcept`, so a thread-creation failure there ends the process with
  `std::terminate` (exit 134), on the frozen binary as well (the same rethrow).
  - Reached with injection (`cleanup:1`: a task cancelled while its child is busy; 5 of 5 runs exit 134), and
    without injection by `detached-async` in the first C140a gate run (1 of 12 runs: after F1's position fix the
    parent's refused wait raises, and its teardown then waits for the still-running child).
  - Tried and withdrawn in C140a: letting the cleanup wait keep its block without a spare worker. It avoids the
    terminate but can deadlock: `cancel_queued` removes only the task's own queued children, queued grandchildren
    need a worker, and with every worker blocked and no thread to be had nothing runs them (`detached-thread` hung
    once, 1 of 12 runs, in the gate run of that version). A safe repair needs the scheduler to fail queued work when
    no worker can be started, outside this bounded checkpoint (D-077 #2 stop condition). Recorded for the owner.
  - (C140b: the tolerance that `detached-async`/`detached-thread` had for this terminate is removed; R1 is now a
    P1 safety defect with its own deterministic reproducer, section 11.)

**What stays outside any guarantee.** The controlled failures are the ones the VM can observe: a thread start that
fails, an allocation that throws at a creation boundary. A process killed by the operating system (the OOM killer,
a signal) before an allocation returns is outside any user-space guarantee. A configured heap budget is not a
process-wide RSS limit (known-limitations.md).

C140a production source (`git diff 3759f481 --numstat src`, split by finding; the test-only lines are the
mutant and fault-injection switches):

| Finding | Files | Added | Removed | Of the added: test-only |
| --- | --- | --- | --- | --- |
| F1 position | parallel_task.h (2), vm_coro_parallel.cpp (7), vm_coro.cpp (10) | 19 | 0 | 2 (M9) |
| F2 over-release | parallel_task.h (5/2), parallel_task.cpp (3/5), parallel_task_start.h (8) | 16 | 7 | 7 (M2, M10) |
| F3 bad_alloc | vm_coro_parallel.cpp (25/4), vm_coro.h (3), parallel_task.cpp (1), parallel_task_start.h (34) | 63 | 4 | 36 (fault hook, M11) |
| F8 timer wait | parallel_task.cpp (hook call in block_worker 1/1, spawn position 1/1), vm_coro_parallel.cpp (1) | 3 | 2 | 1 (hook call) |
| Total | 6 files | 101 | 13 | 46 |

## 11. C140b (review REVIEW-C140A-ASYNC-MEMORY; owner D-082): tests and records only

No production change: `nift` is byte-identical to C140a (sha256 `0895a234adc48478e82d5a1531b55a43540ea91961c50f86b849e9c4e52d9cd2`).
The only source edits are inside the test-only fault-injection block of `parallel_task_start.h` (compiled only
under `NIFT_ASYNC_MEMORY_MUTANT` 9, 11 or 12, which the production build guard refuses): a named `wait` site (a
worker's Blocking wait; it was the unused "-"), several comma-separated sites per run (`wait:1,cleanup:1`), and the
hook in mutant 9's build.

- **T1/D1 (F1 tests).** `detached-async` / `detached-thread`: the parent `w(1)` spawns `w(0)` (which sleeps 300 ms),
  returns and waits only for it; `wait:1` refuses that first worker Blocking wait. Expected in every run (3 each):
  `main done`, then `error: main.f:6:3: parallel task resources exhausted` (`6:8` for thread()), exit 1. M9 gives
  `1:1` in every run (5 of 5 for each form) and is caught deterministically. No probabilistic acceptance remains in
  any gate case; the real-limit detached recursion is not run by the suite.
- (Superseded by C141, section 12: R1 is fixed; `known-defect-r1`, the exit 3 and the "known, outstanding" reporting
  are gone, and the suite exits 0. The text below is the C140b/C140c record.)
- **R1: P1 safety defect (D-082), known and outstanding.** A thread-creation failure in the cleanup wait of a task's
  teardown (`ParallelBlock(Cleanup)` in the `noexcept` `Vm::coro_abort_parallel`) ends the process with
  `std::terminate`, exit 134, no Nift message.
  - Evidence: reachable without injection under an address-space limit; the reviewer measured about 2 in roughly 300
    real-limit detached-recursion runs (da.f 1 of 72 at 1 GiB with `MALLOC_ARENA_MAX=1`, ab.f 1 of 16 at 512 MB);
    the first C140a gate run hit it once. Pre-existing: the same rethrow at 932ff5e2 (C139c) and 3759f481.
  - Deterministic reproducer (`known-defect-r1`, fault-injection build, `NIFT_TEST_PARALLEL_BAD_ALLOC=cleanup:1`):
    the program `R1` in tests/runtime2/async_memory.py: a parent awaits a child that signals `ready` and then runs 12
    long string replacements (an 8 MiB string); the root cancels the parent after `ready`; the parent's teardown
    reaches the cleanup wait while the child is still inside a native call. 50 of 50 runs exit 134 here, 25 + 25 run
    concurrently. (The reviewer's own cleanup programs did not reach the wait because their child finished on
    cancellation before the parent's teardown looked; this child cannot.)
  - Reporting: the case runs on every suite run; when every run (3) ends in the terminate it prints
    `FAIL known-defect-r1: KNOWN DEFECT R1 (P1, D-082): cleanup-wait thread failure -> std::terminate (exit 134 in
    every run)` and `FAILED (known, outstanding) known-defect-r1: ...`, the summary line names it, and the harness
    exits **3** ("every case passes except R1"); any other outcome of the case (exit 0, exit 70, a mix) is a plain
    FAIL (exit 1). `make test-runtime2-async-memory` passes that status through (make reports `Error 3`) after
    running the mutants. Once R1 is repaired the same case expects the controlled `parallel task resources
    exhausted`, exit 1, in every run, and the suite exits 0.
  - Wiring (corrected in C140c, REVIEW-C140B G1; D-083): C140b made the target the last *prerequisite* of
    `test-rt-vm`, and claimed that no other target was skipped. That was wrong: make runs a target's recipe only
    after all its prerequisites succeed, so with R1 exiting 3 the suites of `test-rt-vm`'s own recipe (rt unit tests,
    vm_parity `--rt-diff`, vm_adversarial, vm_golden, vm_modules, ...) never ran, with or without `-k`; the C140b
    `test-rt-vm` gate result is not evidence for them. Since C140c:
    - async-memory is no prerequisite; it is the last of the 35 suites of the `test-rt-vm` recipe.
    - Every suite runs through `tests/runtime2/suite_status.sh run`, which keeps its complete log
      (`.build/test-rt-vm-logs/NAME.log`), records its status, and lets the next suite run even if it failed.
    - The last recipe line, `suite_status.sh summary`, prints one line per expected suite (`PASSED`, `FAILED rc=N`
      with a `FAIL test-rt-vm suite NAME` line, `NOT-RUN`, or `FAILED (known, outstanding)` for async-memory's exit
      3), and `test-rt-vm suites: 35 expected, R ran, P passed, F failed, N not run`; a suite that did not run is a
      failure, never a pass. Exit status: 0 every suite passed; **3** every suite passed except R1; 1 anything else
      (so a real failure is never hidden behind R1, and R1 never behind it: both are listed).
    - certify.py, which also runs a failing gate's recipe lines one by one, sees R1 as one failing line of
      `test-rt-vm` with its `FAIL known-defect-r1` fingerprint; no named baseline (v4.12.0, B4b, weekend/r2) has that
      line, so it classifies FAILED-NEW, never PASSED or FAILED-BASELINE (that needs an identical failure on every
      applicable named baseline).
  - Scope: a bounded repair has been investigated (INVESTIGATION-R1-CLEANUP-ABORT.md) and is pending an owner scope
    decision. (A withdrawn variant that waited without a worker could deadlock, section 10. A narrower
    controlled-exit mitigation, catch in `coro_abort_parallel`, print, `_exit(1)`, was suggested by the reviewer but
    is unbuilt, untested and unsuitable for embedding or persistent-session hosts as it stands.)
  - Unexplored triggers: thread limits other than address space (cgroup `pids.max`, `RLIMIT_NPROC`; root bypasses
    NPROC here) were not tested; a pids limit is the likeliest real deployment trigger and could reach R1 more often.
  - Consequence: the parallel-task resource-safety requirement stays FAILED while R1 is open. It blocks a clean
    safety PASS and a public release. C140, C140a and C140b are not a full resource-safety certification.


## 12. C141 (owner D-084 #4, D-085): R1 and R2 repaired

Owner authority: D-084 #4 (the bounded R1 repair, under the coordinator's brief) and D-085 (the reviewer's extended
Option 1, after the C141 stop report REVIEW-C141-STOP: the sweep alone turned R1's abort into a hang, and R2 already
existed at 0895a234). Approved semantics: once a fatal resource limit has been recorded, by the starve (below) or by
C140 (d)'s note path, every shared wait (`send`, both of its waits; `recv`; mutex `lock`) that would block ends the
run through the non-catchable limit path, exit 1, at that wait's position, with the first recorded reason.

- **R1 (cleanup wait, the starve).** A cleanup wait (`ParallelBlock(Cleanup)` in the `noexcept`
  `Vm::coro_abort_parallel` and `Vm::close_handle_runtime`) whose compensating worker cannot start no longer rethrows
  (`std::terminate`, exit 134). It keeps its block (its CPU permit stays released, so the waited-for task can still
  run), notifies `resume_cv_` and `cv_` like any successful block (the M1 fix), and starves the runtime
  (`ParallelRuntime::starve`, parallel_starve.cpp): the limit is recorded once (`parallel task resources exhausted`, at
  the spawn of the task being cleaned up); every queued task is swept: it fails unrun with `limit.exceeded` at its
  own spawn, its waiters are woken and its charge is released once (as `cancel_queued`); every later `submit` is
  refused with the same limit; the shared waits are woken. The starve allocates nothing and throws nothing: the queue
  is swapped with a deque kept for the purpose, the message is a static string, the error code is a 14-character
  literal; the fault-injection builds make any allocation by the starving thread throw, which in that `noexcept` path
  would be exit 134, and every starve the tests drive exits 1. `resume_cv_` needs no notify in the starve itself (a
  sweep changes no CPU permit).
- **R2 (shared waits after a fatal limit).** `ParallelRuntime::fatal_limit()` is the runtime's first recorded fatal
  limit (an atomic static string, set under the runtime mutex together with the existing C140 limit record, never
  overwritten): `note_limit` (C140 (d): a task ended with the budget or thread limit) and `starve` set it and wake
  the shared waits. `shared_detail::wait_shared` takes the caller's runtime (per runtime, never process-wide: the VM
  passes `options_.parallel`, which is the root's runtime on the main thread and the same runtime, non-owning, in
  every isolate) and, when its condition is not ready, returns that reason instead of blocking (also when woken); the
  four call sites report it as `limit.exceeded`. A wait whose condition is already ready completes as before.
- **What a run looks like.** R1's program: `before`, `runtime.cancelled`, `after`, then `error: main.f:18:8: parallel
  task resources exhausted`, exit 1 (the root's own code still runs; the limit ends the run, as for any un-awaited
  task's limit, C140 (d)). d1.f (R2) under a budget that refuses the producer: `before`, `error: main.f:12:6: parallel
  task memory budget exceeded`, exit 1, where it hung before.
- **Not changed.** Healthy scheduling (no limit recorded: no new branch is taken but a null check before blocking);
  the D-079 records (the limit errors stay non-catchable); output order (published output stays in program order;
  the cases compare stdout exactly). The existing limitations stay: a task that computes without waiting is never
  interrupted (non-cooperative code); and **found in C141, outside D-085, not repaired**: an uncaught error of the
  main program while a task it started is blocked in a shared wait nothing will satisfy waits for that task forever
  (`Vm::coro_uncaught` -> `Coro::wait_parallel`), with an ordinary error too (`1 / 0` after starting a task blocked in
  `recv`: exit 124 under a 10 s timeout here) and with the root's own refused spawn; it is not a recorded fatal limit.

### 12.1 Tests (tests/runtime2/async_memory.py; 18 new cases, `known-defect-r1` removed)

Injected cases run on the fault-injection build (12) 3 times each and must give the exact result every time; the
others on the production build. OPS, CONC and FIRST take their operation from their first line: op 0 is the
calibration (the same text, so the same charges: the least budget under which it succeeds is the budget under which
op 1's failing spawn, and only it, is refused).

| Case | Program, fault | Exact result (exit 1 unless stated) |
| --- | --- | --- |
| r1-cleanup-wait | R1, `cleanup:1` | `before/runtime.cancelled/after`, `18:8` resources |
| r2-thread-start | R1, `nothread:2`, 2 workers | `before`, `24:1` resources (the root's `recv(ready)`) |
| hang-a | hang_a.f (review), `cleanup:1`, 2 workers | the producer ran: `..42/after`, `19:8`; or it was swept: `37:6` (the recv) |
| hang-b | hang_b.f (review), `cleanup:1`, 2 workers | swept: `before/runtime.cancelled`, `45:6` (the recv); or ran: `19:8` |
| r2-budget | d1.f (review), production, least budget - 1 | `before`, `12:6` budget (hung before) |
| r2-submit, r2-start | d1.f, `submit:2` / `start:2` | `before`, `12:6` resources |
| wait-{recv,send,lock}-after | OPS op 1/2/3: the failure first (the root sleeps 300 ms) | `before`, `30:8` / `34:3` (the 2nd send) / `37:3` budget |
| wait-{recv,send,lock}-woken | the same, the wait first (the helper sleeps 300 ms) | the same |
| wait-concurrent | CONC: 3 tasks blocked in recv, send and lock; 3 budget failures; the root in recv | `before`, `52:8` budget, one error line, no `never` |
| first-reason | FIRST: a budget failure, then a starve (`cleanup:1`); and the starve alone | `38:8` budget; alone: `38:8` resources |
| starve-await-swept | SWEPT: a queued task (behind a 30M-iteration task) awaited after the starve | `42:6` `future: ` resources; its `marker` never printed |
| starve-spawn-refused | SPAWN: a spawn after the starve | `35:6` resources; no `marker` |
| healthy-unchanged | R1, hang_a, hang_b, SWEPT, SPAWN, d1 without any fault, production | exit 0 and the full output |

The hang-a and hang-b alternatives are the two schedules of the reviewers' programs (the producer dequeued before
or after the starve); both are exact and exit 1; the M14 case (SWEPT) does not depend on that schedule (its queued
task cannot start while two long tasks hold both permits). Accounting: the fault-injection builds abort (exit 134)
when a runtime is destroyed with any charge still reserved (`NIFT_CHECK_DRAINED`; checked once by inverting it: all
three exit paths tried aborted), and an over-release aborts in every build (C140a), so every injected case also
checks that each charge was released exactly once.

### 12.2 Mutants (each a sanctioned_mutant.py build; every one keeps the fault hook)

| Mutant | Change | Caught by |
| --- | --- | --- |
| 13 | the cleanup wait rethrows (std::terminate) | r1-cleanup-wait, starve-spawn-refused (exit 134) |
| 14 | no sweep of queued tasks | starve-await-swept (the queued task runs and prints) (hang-b does not catch it: its recv fails by the R2 path either way) |
| 15 | no refusal once starved | starve-spawn-refused |
| 16 | swept tasks marked cancelled | starve-await-swept (catchable `runtime.cancelled`) |
| 18 | `wait_shared` ignores the fatal limit | r2-budget, r2-submit, wait-recv-woken, wait-send-after, wait-lock-woken, wait-concurrent (hang: the 20 s timeout) |

A mutant for the M1 notify (17: the starving block returns without notifying) was built and run against every
injected case: none catches it, so it is not in MUTANTS and its switch was removed. A missed notify only delays a
task resuming at that moment until the next completion notifies `resume_cv_`; a hang needs every resumer to queue
before the starving thread's next instructions, which no test can force without a scheduling hook.

### 12.3 Harness (D-084 L1-L4)

- **The R1 tolerance and the exit-3 convention are gone** from async_memory.py, async_memory_gate.sh,
  suite_status.sh and the Makefile: the suite and test-rt-vm exit 0 or 1.
- **L1:** test_integrity_check.py's new rule `suite-wiring` (`make test-test-integrity` passes `--makefile Makefile`)
  fails when RT_VM_SUITES names a suite twice, when a `-@` line of the test-rt-vm recipe is not
  `$(RT_VM_SUITE) NAME --`, when the recipe runs a suite twice, or when the recipe's suites and RT_VM_SUITES differ.
  Shown on scratch copies of the Makefile: a duplicated name, a bare `-@python3 ...vm_golden.py` line and a second
  vm-golden line are each a finding (exit 1); the real Makefile is clean.
- **L2:** a stale status file or log is never evidence. `test-rt-vm-invalidate`, the first prerequisite, deletes the
  previous run's status and logs; the recipe's first line writes this run's identity (HEAD, tree, uncommitted-file
  count, nift path and sha256, UTC time, nonce) as the status file's first line; the summary rejects a status file
  whose identity is not the current checkout and binary, and writes `LOGDIR/SUMMARY` last; `make test-rt-vm-verify`
  (`suite_status.sh verify`) accepts only a completed, passed run of the current identity. Negative control (scratch
  Makefile with two suites, l2/negctl.log): with a completed PASS run present, a failing prerequisite leaves nothing
  (verify: FAIL); without the invalidate prerequisite (the C140c shape) and a changed binary, the stale PASS files
  are rejected by identity; an interrupted run (no SUMMARY), a failed suite and a hand-edited identity are rejected;
  the positive control verifies. Durable certification evidence is unchanged and stays certify.py's: B21 (C42) is
  implemented in scripts/certify_evidence.py and scripts/certify.py (evidence outside every tree; missing evidence
  is FAILED, never a pass) and enforced by tests/runtime2/certification_guard.sh (its B21 section and the owner's
  six-step acceptance test).
- **L3/L4, I2:** R1 and R2 are recorded as fixed by C141 (known-limitations.md). No document carried a `make -k test`
  note (REVIEW-C140C L4), so none is removed. I2 (test-rt-vm's permanent exit 3 stopping a plain `make test`) is
  gone with the exit 3; a plain `make test` still stops at the first failing target, and `test-runtime2-host-deps`
  is a known FAILED `test:` prerequisite (24 refusals, C129, D-071 #1), so a full local run still needs `make -k
  test` for that unrelated reason.

### 12.4 Size

Production (`src/`, `git diff --numstat` against 9fe26ecd): see the C141 report for the split; the non-comment,
non-blank added lines are about 105 production and 40 test-only (the fault hook, the mutant switches, the
no-allocation and drained checks, all compiled only under `NIFT_ASYNC_MEMORY_MUTANT`).

Gate results for this commit are in the C141 report (run on the committed tree).
