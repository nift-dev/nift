# CP22 — VM coroutines, scheduler and event loop (refinement record)

**Status:** refinement record R0 (Opus, 2026-10-27), written before production
edits and committed first; implementation continues on `cp22-opus` while the
architect reviews it. Nothing here is accepted until reviewed.
**Base:** local `runtime-2` `8e65196b7f67abef317e7cfecb1eb97c227c5d96`
(tree `8fb9bb9f`; CP13B + CP14B + CP18 landed). CP17 is **not** in the base.
**Roadmap row:** CP22 | M7 Concurrency | VM coroutines + work-stealing
scheduler + event loop | depends on CP12 | 10k sleeps of 10 ms in < 0.2 s;
linear spawn scaling to 100k.

## 1. Objective

Run Nift's existing asynchronous programs (`fn[async]`, `@fn[async]`,
`async (…) => …` lambdas, `await`, futures, `sleep`) on the Runtime 2.0 VM
with **real coroutines**: a suspended computation keeps its own VM state (pc,
registers and every live Value, call frames, locals, closure, CP13B handlers,
Error origins, resource references) and is resumed in the same dispatch loop.
No simulation by re-running scripts, no legacy execution, no textual
continuation, and no fallback once execution has begun (D-013). `sleep` in a
coroutine program suspends instead of blocking, which is what makes the
roadmap gate (10,000 sleeps of 10 ms in < 0.2 s) reachable; legacy needs
~50 s for that workload on this host (its pool has `max(2, hw)` threads that
each block in `sleep_for`; 1,000 sleeps measured 5.26 s locally).

No new syntax (D-045): only the documented forms above are compiled.

## 2. Legacy oracle (settled by probes)

Oracle: the same binary with `NIFT_VM` unset/`0`, the smoke tests
`tests/v45_async.sh`, `v45_threads.sh`, `v45_mutex.sh`, `v45_atomics.sh`,
`v45_concurrency_hardening.sh`, and the legacy source
(`ParserExpression.cpp` `spawn_future`/`await_future`, `Parser.cpp`
`finalize_execution_workers`, `ParserAsyncPool.cpp`). The repository has no
nift.dev copy of the async documentation; `v45_async.sh` is the documented
contract. Probe programs are kept as fixtures under `tests/runtime2/cp22/`
with their legacy goldens.

| # | Topic | Legacy behaviour (probe) |
| --- | --- | --- |
| L1 | Spawn | Calling an async callable returns a future at once; the body runs on a pool worker (`max(2, hardware_concurrency)` threads) |
| L2 | Isolation | The worker gets a **snapshot** taken at the call: arguments by value, every visible variable cloned, functions/lambdas/modules cloned. Writes in the task are invisible to the caller and vice versa (`q4`: task sees `g = 1` and returns 11 while the caller sets `g = 100`; caller's `g` stays 100; `t1`, `t2` the same for lambdas and captures) |
| L3 | Arity | Checked at the call site before spawning, with the synchronous wording (`p5`: `2:6: callable argument count mismatch: f`) |
| L4 | `await e` | Waits for completion. Value → result. Non-future → fatal `await: expected future` at the `await` (`q3`). Awaiting twice returns the same value (`q2`) |
| L5 | Thrown Error | A task that throws completes as a recoverable failure; `await` re-raises **the original Error** (message, code, origin) as catchable (`p4`, `s4`); uncaught it reports the Error's own origin without prefix (`p4`: `1:20: boom`) |
| L6 | Fatal task error | `await` stops the run with `future: <task message>` at the task's position (`p3`: `1:20: future: return: division by zero`; `s3`: `future: sleep: …`) |
| L7 | Unawaited failure | Ignored; exit status unaffected (`p2`) |
| L8 | Structured completion | A task's future completes only after every task it spawned has completed (the worker `Parser` destructor waits for them before the completion is published; `r1`) |
| L9 | End of run | `finalize_execution_workers` waits for every pending task: normal end (`p1`), top-level `return` (`s1`: the return value is printed **before** the drained task's output), and after a fatal error in the main program (`q6`: the error is reported, then the pending task still runs and prints) |
| L10 | Futures as values | `type(p)` is `future`; aliases compare equal (`p6`); truthy (`t4`); binding type is **string** (`s6`: `y := 5; y = f(3)` fails with `cannot assign string to int binding`); arrays of futures work (`q2`); `print(p)` fails (`value is not directly renderable`); `stringify` exposes the internal tag string (`q2`); `p.await()` is not a method (`q1`) |
| L11 | Methods | `done()` (bool) and `status()` (`running` / `done` / `error`) (`q7`) |
| L12 | Ordering | Task output interleaves nondeterministically with the caller and with other tasks (`t3`: `m t1 t2` and `m t2 t1` across runs) |
| L13 | `sleep(ms)` | Blocks the calling thread (main or worker); validation messages as CP16 `host_sleep` |
| L14 | Async named function as a value | `g := add; g(1, 2)` returns a future whose task fails with `future: invalid async callable` (`s2`): a legacy defect. Async **lambdas** through values work (`t1`) |
| L15 | `await` precedence | A unit starting `await ` awaits the whole rest (`await b + await a` is `await (b + await a)`; `t3`), as the CP10 parser already reproduces |
| L16 | Transfer | Arguments and results must be transferable: timers and Host resources (file, stream, …) are refused at run time (`non-transferable resource`); callables, futures, threads, mutexes and atomics are shared; variables holding non-transferable values are silently left out of the snapshot |
| L17 | Threads etc. | `thread()`, `mutex()`, `atomic<int>/<bool>` run OS threads/locks with the same snapshot rule |

## 3. Runtime 2.0 semantics (decisions in this record)

Every row is either identical to legacy or one of legacy's own allowed
behaviours; none needs a new owner decision. The two places where legacy
diverges by accident are refused, not reproduced (D-015).

1. **Isolation (L2) is kept exactly.** A task runs on its own register stack.
   The global window (main's register frame, where CP12b globals live) is
   copied at the spawn for exactly the global slots the task can reach
   (computed once per Program from the bytecode: the async proto and every
   proto it can call or spawn; any `CallValue` in that set widens it to every
   proto that is ever made a function value). The copy is O(slots), shares
   COW aggregates, and is never written back. Values are COW (D-040), so
   arguments and results are copies.
2. **Ordering (L12):** deterministic, FIFO. The caller continues after a
   spawn; the new task is queued at the tail of the run queue; a task runs
   until it suspends (`await` of a pending future, `sleep`, a preemption
   safepoint) or ends. Timers wake in (deadline, sequence) order. This is one
   of legacy's possible interleavings, so no compatibility change is needed;
   the differential corpus only contains programs whose legacy output is
   deterministic.
3. **Progress/fairness:** compiled coroutine programs get a `Yield`
   safepoint at every loop back-edge (only in units that contain async code;
   other programs are compiled exactly as before). Every 1,024 safepoints the
   scheduler polls due timers and, if another task is ready, rotates. A busy
   loop polling `p.done()` therefore terminates as it does in legacy.
4. **End of run (L8, L9):** structured completion exactly as legacy: a task
   completes after its body and all its children; the run returns only when
   the main program and all tasks have finished. A top-level `return` value is
   emitted into the output at the `return` (before draining, as `s1`); a
   fatal error of the main program is recorded, the remaining tasks drain,
   then the error is reported (as `q6`). An internal VM failure stops at once
   (exit 70) and abandons every task (D-013: VM failures are never disguised).
5. **Errors (L5, L6, CP13B A2):** a task's catchable Error is stored
   unchanged in its future; `await` re-raises the same Error value (equality,
   code, message and origin preserved; await-site data never overwrites it).
   A fatal task error is stored as message + task source position; `await`
   stops with `future: <message>` at the task's position. The task's own
   message follows the VM's existing D-009#5 rule (location of the failing
   expression, no `return:` prefix), so `p3` reads `future: division by zero`
   at the division instead of legacy's `future: return: …` at the `return`:
   this is the already-approved D-009#5 difference, recorded with a narrow
   rt-diff policy, not a new change.
6. **Futures (L10, L11):** a refcounted VM heap value `Kind::Future`
   (`FutureObject`). `type` → `future`; equality is identity; truthy; binding
   tag string (4) as legacy; `done()`/`status()` as legacy. A future is only
   admitted in positions whose legacy behaviour the VM reproduces (§5).
7. **`sleep` (L13)** in a coroutine unit is the `Sleep` opcode: identical
   validation messages and Host-op trace line (CP16 `HostCall`, op `sleep`),
   then the task suspends until its deadline (`sleep(0)` yields). In units
   without async code `sleep` stays the CP16 blocking native, byte-for-byte as
   today.
8. **Trace contexts:** each task gets legacy's label
   (`nift::trace::child_label("async")`, taken in the spawner's context, in
   creation order) and the trace context is switched with the task, so
   rt-diff compares each task's Host ops in order exactly as for legacy.
9. **Cancellation:** there is no script-level cancellation API in legacy and
   none is added (no new syntax). CP22 cancellation is internal: teardown of
   abandoned tasks on an internal stop or VM destruction, releasing every
   Value exactly once; D-034 deadlines/cancellation of Host processes are the
   mechanism CP23 and CP17/CP20 integration reuse.

## 4. Architecture

### 4.1 Representation (new files `src/rt/vm_coro.{h,cpp}`, `src/rt/coro_*.{h,cpp}`)

* `FutureObject : HeapObject` (`Kind::Future`, the last free 4-bit tag;
  see §9 for the CP17 alternative): state (pending/done/failed), result
  Value, failure record (StopReason, message, code, catchable, Error value,
  task source offset, main offset), the waiting task ids. Children: the
  result and the Error (destroyed iteratively like other aggregates).
* `Task`: own `stack` (exactly sized: global window + callee frame, grows on
  demand), `frames`, `handlers`, `global_ready`, `closure`, the function
  value it runs (retained), resume (proto, base, pc), state, parent id,
  live-children count, its future (retained until completion), timer
  sequence, trace label.
* `Scheduler`: task table with free list, FIFO run queue, timer min-heap,
  the main program as task 0, the safepoint budget, the snapshot slot table,
  and a `WakeSource` (§4.3).

### 4.2 VM integration (edits to `vm.cpp`/`vm.h` kept minimal, own commit)

* New opcodes (`opcodes.h`): `Spawn`, `CallValueAny`, `Await`, `FutureOp`,
  `Sleep`, `Yield`, `TaskEnd`. The dispatch loop gets one entry for all of
  them that calls `Vm::op_coro`, which may switch tasks by swapping the VM's
  stack/frames/handlers/global_ready/closure with the task record and
  reloading `R/K/proto/base/pc`; dispatch then continues in the same loop.
  No C++ stack switching, no recursion, no threads.
* The bail path and `Halt`/`TopReturn` call the scheduler only when the
  program has coroutines (`coro_` non-null): an uncaught failure of a task
  completes its future and switches; the main program's end/failure starts
  the drain. Programs without async code execute exactly the old path (one
  null-pointer test on the bail and halt paths only; no hot-path change, CP20
  F6 untouched).
* A task starts in a fixed trampoline frame (`Nop; TaskEnd`) under the
  callee, so `Return` lands in `TaskEnd`, and every report helper that reads
  `frames_` (`main_offset`, `in_loop_path`, `error_result`) sees a valid
  frame.
* Memory: suspended tasks' stacks are charged to the VM heap budget beside
  the running stacks; exceeding it is the existing internal
  `memory budget exceeded` stop. Task stacks are released when the body ends,
  before the children finish.

### 4.3 Event loop

The VM thread is the only thread that runs VM code (D-001 #7: heaps belong to
one isolate, refcounts are not atomic). When nothing is runnable it waits on
its `WakeSource` (mutex + condition variable + posted-wake count) with the
earliest timer deadline as the timeout. Timers live in the scheduler's heap
(O(log n) per sleep), so 10k sleeps cost one wait per distinct wake-up, not
10k OS timers. Host events reach the scheduler only by posting to the
`WakeSource` from the CP16 host loop (`loop_at`/`loop_when_readable`
callbacks run on the loop thread): there is no second polling thread and no
competing event loop. In the base no VM-visible Host op completes
asynchronously (VM `run()`/processes arrive with CP17/CP20 integration), so
the cross-thread path is exercised by a unit test (TSan) that posts from the
real host loop.

### 4.4 Compiler (minimal edits to resolver/compiler/infer, own commit)

* `resolver.cpp`: `fn[async]`/`@fn[async]` top-level functions are admitted
  (`kFlagAsync` no longer refused; deferred bodies and methods still are).
* `compiler_call.cpp`: a direct call of an async function emits `Spawn`
  (same argument evaluation, arity check, D-018 refusals as a call; parameter
  text types are value-derived, as legacy's `__future_argN` bindings).
* `compiler_closure.cpp`: async lambdas are admitted when they capture
  nothing; in a coroutine unit every `CallValue` is emitted as `CallValueAny`
  (spawns when the callee proto is async).
* `Await` (`await e`), `FutureOp` (`.done()`, `.status()` on a future),
  `Sleep` (`sleep(ms)` in a coroutine unit), `Yield` (loop back-edges in a
  coroutine unit).
* `infer`: `kKindFuture = 1u << 14` (outside `kKindAll`, after CP17's handle
  bits 11–13). A call of an async unit has type Future whose element is the
  unit's return type; `await` yields that element; `done()` bool,
  `status()` string, `type(p)` string.

## 5. Admission and refusal (all before execution)

A unit with async code is compiled only when every rule holds; otherwise it
falls back whole (D-001/D-013) with the named reason, and `NIFT_VM=require`
reports it:

| Reason | Refused because |
| --- | --- |
| `coro-future-flow:<context>` | a future (or an aggregate that may contain one) reaches a position other than: binding declaration/assignment, user-call or spawn argument, `return`, array literal element, `push` argument/receiver, index read, `for` iterable, `length()`/`size()`, `await` operand, `.done()`/`.status()` receiver, `type()` argument, `if`/`!` truthiness. Everything else (print, concatenation, comparison, built-ins such as `stringify`) would expose legacy's tag string |
| `coro-async-value` | a named async function used as a value (legacy defect L14) |
| `coro-capture` | a closure with captured cells in a coroutine unit (legacy clones captures into each task; shared cells would be visible across tasks) |
| `coro-main-cell` | cells or location references in the main program of a coroutine unit (snapshot copies would share them) |
| `coro-struct` | struct declarations in a coroutine unit (slot-addressed instances; not needed by the corpus) |
| `coro-collection-callback` | collection callbacks in a coroutine unit (suspension inside the CP14B controller is not yet proven) |
| `coro-sleep-arity` | `sleep` with other than one argument in a coroutine unit |
| `coro-module` | imports in a coroutine unit (module initialisation order vs spawns unproven) |
| `coro-thread-callee` | `thread(…)` whose first argument is not a directly named top-level synchronous function (lambdas and values: unproven arity/position rules; non-callables; an async function is `coro-async-value`, its join would yield a future) (§11.6) |
| `coro-thread-arity` | `thread(f, …)` with the wrong argument count (legacy reports it against its internal `__thread_callable` at 1:1) |
| existing `callable:mutex`, `atomic…` | mutexes and atomics stay on legacy (CP23; D-036 assigns `mutex` to CP22/CP23, the CP12B inventory to CP23) |
| `fn-flags` | unchanged for deferred bodies and methods |
| `coro-blocking-host:<name>`, `coro-task-recursion`, `coro-task-loop`, `coro-task-native:<name>` | the cooperative admission rule (§12.1, owner decision 2026-10-27): cooperative execution must be provably legacy-equal |

Admission is measured by the D-016 suites: async programs move from the
unsupported suite to the supported suite only for the admitted subset; each
refusal above has an unsupported fixture proving fallback before execution
(`vm_executions: 0`).

## 6. Host handles (CP17) and Host processes (CP16/CP20)

CP17 is not in the base, so no VM script value can be a Host handle yet and
nothing here invents a temporary handle API. CP22 needs these CP17
interfaces (inspected in `r17`/`i17`, not used yet):

* `infer.h` `kKindHandle`/`kKindFile`/`kKindStream` (bits 11–13) and
  `compiler_handles.h` `check_handle_flow`: the future-flow check must also
  refuse handle values in spawn arguments, task results and snapshot slots
  (legacy L16 refuses them at run time or drops them from the snapshot).
* `value.h` `HandleBoxObject` and `handles/handle_box.h`
  (`box_counted_handle`, `share_into_isolate`): boxed counted handles are heap
  values; a task's stack, frames and future result must release them exactly
  once on completion, failure and abandonment (CP17's abandoned-frame
  lifecycle correction applies to task frames too).
* `Vm::open_handle_runtime`/`close_handle_runtime`, `handles::ContextKind`,
  `HandleTable::open_context`/`close_context`/`transfer`: each task gets a
  child handle context of its parent's (D-035: children may use ancestors'
  handles); a task's end closes its context after its children; handles a
  task returns are transferred to the awaiting context (CP17 "Returning a
  handle from an async worker"); `close_handle_runtime` runs after the drain.
* Host processes (`run()` on the VM, CP16 process layer, CP20): waits and
  deadlines become suspension points posting to the `WakeSource` from the
  host loop; launches stay on a long-lived thread (CP16 impl "PDEATHSIG is
  per thread"); D-034 deadlines mark when cancellation begins.

Until then: a coroutine unit that contains any Host handle-producing
built-in is refused (`coro-handle`, conservative; in the base the VM has no
such built-in, so the rule is a tripwire). Reconciliation steps are listed in
the handover (§11) for the integrated CP17 tip.

### 6.1 Reconciliation steps with the integrated CP17 tip

Read from `integ-cp17-bd` `eeb3bead` (not a base):

1. `opcodes.h`: CP17 appends `StmtMark` after `ArrayMutate`; the CP22
   opcodes are re-appended after `StmtMark` (no reordering).
2. `Vm::Vm`: `open_handle_runtime()` before `coro_init()`. `Vm::run`'s
   `done:`: `coro_teardown(result)` **before** `close_handle_runtime(result)`,
   so handle boxes held by abandoned tasks are released before the script
   context closes. `Vm::run` must stay within rt-lint's 150 lines (both
   branches compact lines).
3. Abandoned frames: CP17 adds `Handler::creation_mark` and a handle-release
   step in `Vm::unwind`'s frame-pop loop. `Vm::Coro::release` drops a task's
   whole frame stack (end of body, failure, teardown); it must run the same
   release step for every frame and handler it drops (a task that ends by an
   uncaught failure is the abandoned-frame case), and `end_body` must not
   free storage before that step.
4. W13 (`check_handle_flow`) and the coroutine flow check run in sequence
   in `compile_unit`; the flow check must additionally refuse a value whose
   kinds include `kKindHandle` as a spawn argument, in a task's result, and in
   a global slot of an async proto's snapshot (`coro-handle`), until per-task
   handle contexts and result transfer exist (§6).
5. Per-task handle contexts: spawn opens a child context of the spawner's
   (`open_context`), `end_body` closes it after the children complete, and a
   handle returned by a task is `transfer`red to the awaiting context.
6. `HandleBoxObject` values are heap values: the snapshot copy and argument
   moves need no change (refcounts are per isolate, one isolate here).

## 7. Requirements → tests

| Requirement | Test (all new under `tests/runtime2/cp22/` unless noted) |
| --- | --- |
| Legacy parity, both dispatches | `run.py`: every fixture under legacy (`NIFT_VM=0`) vs golden, and VM (`NIFT_VM=require`, `NIFT_VM_STATS` outcome `vm`) on the computed-goto and the portable switch build (`-DNIFT_RT_VM_SWITCH`) |
| Refusal before execution | `refuse/*.f`: each §5 reason, `vm_executions == 0`, legacy output unchanged |
| Isolation, ordering, structured end, errors | fixtures `iso_*`, `order_*`, `drain_*`, `err_*` (incl. CP13B A2: awaited Error equals the original and keeps its origin) |
| Fairness/progress | `fair_*` (busy loop polling `done()`, preemption), `sleep_*` wake order |
| Adversarial interleavings | `adversarial.py`: generated programs (random spawn trees, awaits, sleeps, throws, loops) compared VM vs legacy where legacy output is order-independent, plus VM-only invariants (each task completes once, results routed to the right future) |
| Cancellation/teardown | internal-stop abandonment under ASan (no leak, exactly-once release), heap returns to baseline (`heap_live`) |
| Stress | 10k × `sleep(10)`; 100k spawns; deep await chains; 1M yields |
| Mutants | lost wakeup, double resume, early timer, leaked task frames, await result from the wrong future, snapshot write-back, drain skipped, Error re-raised with await-site origin — each compiled (`-DNIFT_CORO_MUTANT=n`), each caught by its named fixture with an exact witness; crash/timeout/missing stats = harness failure |
| Negative controls | dead binary (stats missing → failure), unrelated failure (a fixture broken on purpose → reported as unrelated), timeout control |
| Sanitizers | ASan/UBSan VM build on the corpus + stress subset; TSan unit test for `WakeSource` with the host loop |
| Gates kept | `test-rt-vm`, `test-rt-vm-sanitize`, CP13B/CP14B/CP18 suites, `test-test-integrity`, certification guard + self-test, `test-rt-lint`, `warnings-check` |

## 8. Performance method

Local numbers on this shared 2-vCPU container are **exploratory only**;
the gate results must come from the reference platform (Linode, owner rule)
with callgrind instruction counts as the gate (D-005) and wall medians as the
reported result. Workloads (`benchmarks/runtime2/cp22_coro.py`, new):
10k × `sleep(10)` wall time (gate < 0.2 s); spawn+await of N tiny tasks for
N = 1k, 10k, 100k (instructions per spawn; proposed linearity criterion:
per-spawn cost at 100k ≤ 1.25× that at 1k, to be pre-registered by the
architect); scheduling overhead (yield round trip, instructions); wakeup
latency (deadline → resume, wall, p50/p99); fairness (rotation counts between
two CPU-bound tasks); cancellation/teardown latency (100k pending tasks);
idle CPU while all tasks sleep (process CPU time during a 1 s sleep ≈ 0);
resource growth (`heap_peak`, RSS, fds before/after drain).

## 9. Scope reconciliation (architect answers, 2026-10-27)

* **"Work-stealing" (proposed roadmap amendment for the owner; the roadmap
  text is not edited here).** Work stealing across OS threads would run one
  isolate's coroutines on several threads, which D-001 #7 rules out
  (non-atomic refcounts, isolate-owned heaps). CP22 delivers a per-isolate
  scheduler; stealing between isolates belongs with CP23's capture-only
  isolates. The CP22 gates are unchanged. Architect: agreed.
* **CPU parallelism vs legacy: open owner decision.** Legacy async tasks run
  in parallel on a thread pool; VM tasks of one isolate interleave on one
  thread, so a CPU-bound async program can be slower on the (opt-in) VM. This
  is a potential regression only the owner can accept. Measured on the same
  binary, legacy vs VM (`benchmarks/runtime2/cp22_coro.py --cpu-n`, four
  tasks of a tight arithmetic loop, exploratory 2-vCPU numbers in the
  handover). Options: (a) accept until CP23 isolates (CP23 runs tasks whose
  captures are copyable in parallel isolates); (b) refuse CPU-heavy async
  units on the VM (no static criterion exists; not recommended); (c) bring
  isolate-parallel tasks forward into CP22 (CP23 scope). Recommendation: (a).
* **`Kind::Future`** takes the last value of the 4-bit heap-tag mask
  (superseded by §11.2: the mask is one constant over a byte with three
  unused bits; Future is not irreversible).
  Alternative after CP17 lands: futures as CP17 boxed handles
  (`HandleKind::Future`, the box's `release` hook pointing at the scheduler);
  all future operations are in the new files, so the switch stays local.
  Architect: acceptable as recommended. `kKindFuture = 1u << 14` does not
  collide with CP17's handle bits 11-13 (checked against `integ-cp17-bd`
  `eeb3bead`).
* **Gates, pre-registered by the architect.** Linear spawn scaling passes
  when the per-spawn cost at 100,000 spawns is at most 1.25x the per-spawn
  cost at 1,000 spawns; cost = callgrind instructions of spawn plus completion
  of a trivial task, the fixed startup removed with a 0-spawn baseline run;
  wall clock and peak RSS at 1k/10k/100k reported as exploratory. "10k sleeps
  of 10 ms in < 0.2 s" is a wall-clock gate only the reference platform can
  pass; local numbers are exploratory, never passes.

## 10. Commit strategy

1. This record.
2. Value model: `Kind::Future`/`FutureObject` (value.h/.cpp, heap accounting,
   compare/render/cycle/bridge touch points) with unit tests.
3. Scheduler, `WakeSource` and task records (new files) with unit tests
   (incl. TSan).
4. VM integration: opcodes + `vm.cpp`/`vm.h` hooks (own commit, interface
   stated in the message).
5. Compiler/infer/resolver admission (own commit).
6. Differential corpus, refusals, adversarial generator, stress, mutants,
   negative controls; Makefile targets wired into `test-rt-vm`,
   `test-rt-vm-sanitize`, certification guard and plan.
7. Benchmarks + exploratory evidence; this record's Evidence section.

## 11. Owner decisions, round 2 (2026-10-27) — new proposal, not accepted

*Round 3 (§12) supersedes the provisional acceptance below: parallel
`async` and `thread()` keep real parallelism; the scheduler runs only
programs admitted by §12.1.*

The owner's answers on candidate e06ea42d: the cooperative single-threaded
scheduler is accepted **provisionally** as the implementation architecture
(no performance obligation waived, no async functionality permanently
reduced); `Kind::Future` is not approved as irreversible; work-stealing may be
proposed for CP23, no roadmap amendment approved; CP17 reconciliation stays
blocked until CP17 is accepted and integrated.

### 11.1 CPU-bound scaling: single-threaded scheduling vs multithreaded execution

**Exploratory** (this container: 2 vCPUs, `nproc` 2, sched_getaffinity 2,
shared, not the reference platform). `benchmarks/runtime2/cp22_coro.py
--scale-only`: k = 1, 2, 4, 8 concurrent `fn[async]` tasks with the same work
each (weak scaling), the same binary and program on both engines, median of
3 runs, wall and process CPU time (user + sys, `wait4`). Outputs identical on
both engines in every row; every VM run executed on the VM (`outcome: vm`).
Legacy executes tasks on its pool (`max(2, hardware_concurrency)` threads,
i.e. multithreaded execution); the VM schedules them on one thread
(single-threaded scheduling, no parallel execution).

| Body (iterations per task) | k | Legacy wall / CPU (s) | Legacy CPU/wall | VM wall / CPU (s) | Legacy wall / VM wall |
| --- | --- | --- | --- | --- | --- |
| arith (600k) | 1 | 0.413 / 0.407 | 0.98 | 0.063 / 0.062 | 6.6 |
| arith | 2 | 0.495 / 0.889 | 1.80 | 0.110 / 0.107 | 4.5 |
| arith | 4 | 0.952 / 1.719 | 1.81 | 0.250 / 0.237 | 3.8 |
| arith | 8 | 2.268 / 3.875 | 1.71 | 0.423 / 0.418 | 5.4 |
| string (300k) | 1 | 0.283 / 0.282 | 1.00 | 0.029 / 0.029 | 9.8 |
| string | 2 | 0.295 / 0.570 | 1.94 | 0.055 / 0.054 | 5.4 |
| string | 4 | 0.731 / 1.163 | 1.59 | 0.106 / 0.105 | 6.9 |
| string | 8 | 1.275 / 2.313 | 1.81 | 0.216 / 0.214 | 5.9 |
| native: `sha256` of 64 KiB (10k) | 1 | 0.642 / 0.642 | 1.00 | 0.537 / 0.528 | 1.20 |
| native | 2 | 0.692 / 1.340 | 1.94 | 1.103 / 1.101 | **0.63** |
| native | 4 | 1.375 / 2.628 | 1.91 | 2.147 / 2.128 | **0.64** |
| native | 8 | 2.692 / 5.192 | 1.93 | 4.252 / 4.209 | **0.63** |

Reading (single-threaded scheduling ≠ multithreaded execution):

* VM wall time grows linearly with k (one thread: wall ≈ CPU ≈ k × the
  single-task cost); legacy uses up to `min(k, cores)` cores (CPU/wall ≈ 1.8-1.9
  here, the 2-core ceiling).
* Where the work is interpreted (arith, string), the VM's single-thread cost
  is 6.6-9.8× lower, so it wins at every k on 2 cores. On a machine with P
  cores, legacy's wall time is about k × c_legacy / min(k, P) × (parallel
  efficiency, ≈ 0.9 here), the VM's k × c_vm: **the VM loses once min(k, P)
  exceeds c_legacy / c_vm divided by the efficiency** — for these bodies with
  about 8 or more concurrent CPU-bound tasks on 8+ cores (arith) and 11+
  (string). This is a projection; it cannot be measured on 2 cores.
* Where the time is spent in natives (the same C++ code on both engines), the
  VM's advantage disappears (1.2× at k = 1) and **the VM already loses on
  this 2-core machine: 1.6× slower wall at k ≥ 2**; on P cores the loss
  approaches min(k, P) / 1.2.
* The same holds for `thread()` (same task path) and, once VM Host ops can
  block (CP17/CP20 processes and handles), for tasks that block in Host ops:
  legacy overlaps them on threads, the VM's one thread would serialise them
  unless the Host op suspends the task (CP22 §6 plan).
* Options (unchanged, owner): (a) keep cooperative scheduling until CP23
  isolates run capture-only tasks in parallel (the provisional decision); (b)
  run Host ops off-thread with completion through the WakeSource (needed
  anyway for blocking Host ops; does not help CPU-bound natives); (c) bring
  isolate-parallel execution into CP22. The obligation stands: the loss rows
  above are a regression against legacy for CPU-parallel async programs until
  one of these lands.

### 11.2 Value-kind extensibility (is Kind::Future irreversible?)

Where a runtime kind is encoded, and what one more kind costs:

| Layer | Encoding | Room after Future (14) and threads | Cost of one more kind |
| --- | --- | --- | --- |
| `rt::Value` tag (byte 15) | a full byte (`Kind : uint8_t`) | 241 values | none |
| `HeapObject::tag` | low 4 bits = kind, bit 7 = D-040 identity summary, **bits 4-6 unused** | 1 value (15) with `kKindMask = 0x0F`; 113 with `0x7F` | change `kKindMask` to `0x7F` (one constant, two static_asserts): `kind()` stays one `and` with a different immediate |
| infer `KindSet` (compile time only) | `uint16_t` bit set; bits 11-13 CP17 handles, 14 future, 15 thread | none | `KindSet` becomes `uint32_t`: 2 more bytes per AST node in `UnitKinds`, compile time only, nothing at run time |
| `binding_tag` (legacy text types) | per-kind switch | n/a | one `case` |
| identity fast path (`aggregate.cpp`: kinds >= Array may carry identity) | ordering of `Kind` | a new kind appended is treated as identity-capable (conservative, correct) | a new *scalar* kind must be inserted before Array (renumbering; `Kind` values are never persisted) |
| C ABI / bindings | not exposed: `include/nift` has its own `nift::Value::Type` and `nift_program_result` carries copied text only; no FFI marshals `rt::Kind` in the base (CP18 FFI is legacy-side) | n/a | none |
| Bytecode / caches | `Kind` values are not serialised (no persistent bytecode; D-041's module cache is in-memory and keyed by compiler version) | n/a | none |

Evidence (one small compile, `kindprobe/`): `src/rt/value.cpp` compiled with
the base header and with `kKindMask = 0x7F` plus two more kinds
(`Probe15`, `Probe16`): every `HeapObject::kind()` site is the same single
instruction (`and $0xf` → `and $0x7f`), 1,056 vs 1,060 instructions in the
object (the 4 extra are the two new `kind_name` cases). So the "last free
tag" in §9 was overstated: the 4-bit field is an artificial mask over a byte
with three unused bits, and the representation is adequate; no type-system
redesign is needed.

Alternatives compared:

* **(A) current: `Kind::Future` (14), threads as a heap subkind of it**
  (`FutureObject::thread`, §11.6). One heap kind serves every task handle
  (futures, threads; channels/isolate handles of CP23 could join it). Hot
  paths: none (futures are never on arithmetic or collection paths; the
  dispatch switch only reaches them through the cold coroutine opcodes).
* **(B) a generic heap kind with a subkind byte** (e.g. `Kind::Task` whose
  object carries `{future, thread, …}`): what (A) already is, apart from the
  name. Renaming `Future` to a neutral name is cosmetic.
* **(C) futures as CP17 boxed handles** (`HandleKind::Future` already exists in
  the base enum; CP17's `HandleBoxObject` makes `Kind::Handle` a heap kind with
  a box flag): frees tag 14 but makes every handle operation distinguish
  futures by the aux byte, `type`/`binding_tag`/equality read the box, and
  futures enter the handle table's lifecycle (contexts, transfer, release
  scheduling) which they do not need; depends on CP17 being integrated.
  More cost, no gain while tag room is not scarce.
* **(D) widen the mask now** (0x0F → 0x7F): free and reversible; not done in
  this candidate because no further kind is needed yet; the analysis above is
  the evidence that it is a one-constant change when one is.

Recommendation: keep (A); treat `Kind::Future` as "the task-handle kind" (heap
subkinds for thread and later CP23 handles). Nothing irreversible: no ABI,
serialisation or persisted format depends on the value 14.

#### 11.2.1 Round 4: boundary-by-boundary analysis (Kind::Future stays provisional)

The question is how `Kind::Future`, and one more runtime kind after it,
cross every boundary. No representation change is proposed.

| Boundary | Today (code reference) | Future (and thread subkind) | One more kind K | Risk / owner-visible point |
| --- | --- | --- | --- | --- |
| Value encoding | `rt::Value` byte 15 = `Kind` (`value.h`) | 14 | 15; byte has 241 free values | none |
| Heap header | `HeapObject::tag`: bits 0-3 kind, bit 7 D-040 identity summary (`heap.h`), bits 4-6 unused; `kind()` masks with `kKindMask` | fits (14) | 15 fits; 16+ needs `kKindMask` = `0x7F` (one constant + two static_asserts; same single `and`, checked by compile) | **identity bit 7 must stay outside the kind mask**: a widened mask of `0x7F`, never `0xFF` |
| Destruction / children | `destroy_heap_object`, `child_count`, `child_at`, iterative release (`value.cpp`) | cases for Future (2 children) | one case each; the switches have defaults, so a missing case is not a compile error | a missing case leaks or crashes: covered by the heap-baseline tests (M4 style) |
| Heap accounting | `footprint` (`heap_account.cpp`), `task_bytes` via `heap_retally` | counted | one case | budget checks |
| Equality / sameness | `compare.cpp` (identity for Function, Future) | identity | decide value vs identity per kind | D-040 V16 identity summary |
| Identity fast path | `aggregate.cpp`: `kind() < Array` cannot carry identity | appended after Array: identity-capable (correct) | appended: same; a new *scalar* kind must be inserted before Array (renumber; nothing persists `Kind`) | ordering is load-bearing |
| Text types / bindings | `binding_tag` (`vm_ops.cpp`) | 4 (string, as legacy) | one case | legacy text-type parity |
| Legacy bridge | `legacy_bridge.cpp convert_to`: `default` → "<kind> values have no legacy representation" | no representation; never crosses (no VM→legacy handoff after execution starts, D-013; modules refused in coroutine units) | same default | a kind that must reach legacy needs a converter |
| Serialisation (JSON, stringify, render, print) | natives switch on `Kind`; `default` refuses (`json argument kind`, `public_type_name` → null) | unreachable: the flow check (`coro-future-flow:*`) refuses futures in every serialising position | needs the same flow check or a defined serialisation | a new kind must not reach these natives unchecked |
| Persistence / caches | no persistent bytecode; D-041 module cache is in-memory, keyed by compiler version; `NIFT_VM_STATS` reports no kinds | none | none | none today; a future on-disk cache must version `Kind` |
| C ABI / public API | `include/nift/c_abi.h` exposes outcomes and copied text only; `nift::Value::Type` (`include/nift/value.h`) is a separate legacy enum | not exposed | not exposed unless a binding needs it | an embedding API for VM values would need its own stable enum, not `rt::Kind` |
| FFI | CP18 FFI is legacy-side; VM FFI (later) marshals per declared C type, not per `Kind` | not marshallable (refuse) | per kind | FFI must refuse task handles |
| Compiler inference | `KindSet` `uint16_t`: bits 0-10 base, 11-13 CP17 handles, 14 future, 15 thread: **full** | `kKindFuture`, `kKindThread` | `KindSet` → `uint32_t`: `UnitKinds::kinds` (one per AST node: +2 bytes/node), `Ty::kinds`, every `KindSet` signature (35 uses in headers); compile time only, no run-time cost | owner-visible: the next inferable kind forces the widening |
| CP23 isolates | values cross isolates by copy or freeze (architecture #7) | a future/thread handle is bound to its isolate's scheduler: copying must **refuse** (or CP23 defines a proxy) | a channel or isolate handle is shared across isolates: refcount must be atomic or the object must live outside the isolate heap | **CP23 types are better modelled as `HandleKind`s** (`HandleKind::{Future, Mutex, Atomic}` already exist in `value.h`, boxed by CP17's `HandleBoxObject`) than as new `Kind` tags; that consumes no tag |

Conclusions:

* Today `Kind::Future` is adequate. It crosses no persistent, ABI, FFI or
  legacy boundary, and every serialising boundary refuses it by default. The
  compile-time flow check keeps it away from those boundaries.
* The scarce resource is the compile-time `KindSet` (full at 16 bits). The
  heap tag has room in practice (byte plus three free bits), and the value
  tag has plenty.
* Cross-isolate CP23 types belong in the handle space. Future and thread
  handles must be refused at the isolate boundary.
* The open alternative from §11.2 remains: Future as a CP17 boxed handle,
  which frees tag 14. It is worth deciding when CP17 is integrated, because
  CP23's handles will follow whichever model CP17 sets.
* Status: **provisional**, pending the independent review the owner
  requested.

### 11.3 Work-stealing and mandatory CP22 semantics

Checked against `checkpoints.md` (row CP22: "VM coroutines + work-stealing
scheduler + event loop", gates 10k sleeps < 0.2 s and linear spawn scaling),
`decisions.md`, `architecture.md` and the scope assignments of
`checkpoints/cp12b-coverage-inventory.md` / `cp12b-resolver.md` /
`cp13-diagnostics-design.md` / `cp16-host-ops-design.md`:

* No decision (D-001…D-052) defines a CP22 *semantic* that needs parallel
  execution or work stealing: the CP22 gates are a sleep latency and a spawn
  scaling gate, both met by an M:1 scheduler; D-001 #7 (isolate-owned heaps,
  non-atomic refcounts) forbids running one isolate's tasks on several OS
  threads; D-034's deadline/cancellation mechanism (cited by CP16 as "the
  mechanism CP22 reuses") is untouched; D-041 leaves module-instance states
  "room for CP22–CP23" (concurrent imports), which CP22 refuses (`coro-module`)
  — an admission gap, not a semantic of work stealing.
* So leaving work-stealing out leaves **no CP22 semantics unfinished by
  itself**; its only effect is performance (§11.1), and the proposal is to
  move it to CP23 with isolates (proposal only; `checkpoints.md` unchanged).
* The check found **one mandatory CP22 item the candidate had left out**:
  the CP12B inventory assigns `thread()` to CP22 ("CP22 (threads, scheduler);
  CP23 (mutexes, atomics)"; `callable:thread` 100 programs; also CP13 S11
  "thread failure `thread: return: …`" → CP22). e06ea42d refused it. It is now
  implemented on the scheduler (§11.6). Mutexes and atomics stay CP23.
* `fn-flags` (`fn[async]` → CP22, `@fn` → CP24): see §11.4.

### 11.4 `@fn[async]`: out of CP22 scope (evidence)

* `@fn` is the template function form (`docs/ADVANCED-TEMPLATING.md`
  `@fn(add(a,b)){ @return(a+b) }`); the parser marks it `kFlagDeferredBody`
  ("FnDecl written as template `@fn`: body translated at call time",
  `rt/ast.h`). The base VM refuses every `@fn`, async or not (`fn-flags`,
  `resolver.cpp`), before CP22.
* The scope documents assign it to CP24: inventory row "`fn-flags`
  (`fn[async]`, `@fn`) → CP22 (async) / CP24 (`@fn`)"; "CP22/CP24 | fn[async]
  (CP22), @fn deferred bodies (CP24)"; `cp12b-resolver.md` "`@fn` is a
  translated body" (CP24). CP24 (template compiler) is a later checkpoint.
* Legacy probe: `@fn[async](add(a, b)) { return a + b }` behaves as
  `fn[async]` (prints 3); a plain `@fn(add…)` likewise as `fn`. Compiling the
  deferred body is CP24's job; once CP24 compiles `@fn` bodies, the CP22
  machinery (Spawn, snapshot, futures) applies unchanged because the flag only
  changes how the body is obtained. Fixture `refuse/r_at_fn_async.f` keeps the
  refusal (`fn-flags`) under test.

### 11.5 Windows timer path

The scheduler's own timers never depended on the host loop: a task's
`sleep` deadline is the scheduler's timer heap and the VM thread waits in
`WakeSource::wait_until` (portable `std::condition_variable`), so `sleep`
suspension works on Windows as on POSIX. What was missing on Windows is the
cross-thread wake path `WakeSource::post_at` → CP16 `loop_at`, which was a
stub returning 0 (never fires). It is now implemented in
`src/rt/host/process_loop.cpp` (own commit; CP21 owns the file): the
`_WIN32` branch is a portable timer loop (one lazily started thread,
`std::condition_variable`, same contract: never early, callbacks without the
lock, cancel is a no-op for fired ids); `loop_when_readable` stays a no-op
there (Windows has no CP16 fd watches). The same branch is compiled on Linux
with `-DNIFT_HOST_LOOP_PORTABLE` and run by `test-runtime2-cp22-tsan`
(`coro-wake-portable`) under TSan, and every CP22 source and the loop compile
with the MinGW-w64 cross compiler (`-fsyntax-only`). Not done: running on a
Windows host (no Windows machine or emulator here; the Windows CI target is
not run from this container).

### 11.6 Threads on the scheduler

Legacy (`v45_threads.sh` and probes `probe2/t*.f`, `u*.f`, `v*.f`):
`thread(f, args…)` runs `f(args…)` on an OS thread with the async snapshot
rule; `join()` returns the result (repeatable), `done()`, `status()`
(`running`/`done`/`error`), `type` "thread", binding type string; a fatal
failure joins as `thread: <msg>` at the thread's position (`thread: return:
…` at the `return`, the D-009#5 shape), a thrown Error re-raises unchanged
(catchable); unjoined failures are ignored; unjoined threads drain at the end;
a thread created in a parent may be joined in a child (args or snapshot) but
one created in a child is invalid in the parent (`thread: invalid handle`);
`await t` is `await: expected future`; `p.join()` on a future is a parse-level
error.

**Default: refused before execution (`callable:thread`, as in the base).**
The cooperative implementation below compiles only with the test opt-in
`NIFT_VM_COOPERATIVE_THREADS=1` (`CompileLimits::cooperative_threads`), pending
the owner decision of §11.7; fixture `refuse/r_thread_default.f` keeps the
default under test.

VM (opt-in): `thread(f, args…)` compiles to `CallValueAny` with the thread bit
(`c & 0x8000`; argument windows are < 0x8000) and spawns the task exactly as
an async call (snapshot, FIFO, structured completion, drain), marking its
`FutureObject` as a thread (heap subkind, §11.2); `join()` is `Await` with
`c = 1` (legacy's `thread: ` prefix); infer gives the handle `kKindThread`.
Admitted: `f` a top-level synchronous function named directly with the
right arity. Refused before execution (legacy defects or unproven):
`coro-thread-callee` (a lambda/value or a non-callable; an async function is
refused before that as `coro-async-value`),
`coro-thread-arity` (legacy reports it against its internal
`__thread_callable` at 1:1), `coro-future-flow:Return` (a handle returned
from a thread body), `coro-future-flow:Member` (`join` on a future). Ordering
is FIFO, one of legacy's interleavings (VM-only fixture `v_thread_order`).
The D-009#5 location rule applies to `thread: ` exactly as to `future: `
(policy `D-009#5-thread`, EXPECT.tsv). The CPU parallelism of legacy threads
is the §11.1 question.

### 11.7 `thread()` execution model (decided: option (b), §12)

**Contract.** Legacy and its documentation promise actual concurrency:
`ReleaseNotes.md` (v4.5) "Real native concurrency: `thread(...)`, replayable
`join`, `hardware_concurrency()`, mutex-backed explicitly shared state,
sequentially-consistent `atomic<int>` / `atomic<bool>` … and async
functions/futures on a bounded native worker pool"; the v4.5 handover
(`docs/handover/V4.5-NATIVE-RUNTIME-SHELL.md`): "add real native concurrency:
threads, mutexes and `async` / `await` backed by actual parallel execution";
"`async` / `await` must be genuinely parallel-capable. Do not implement a
JavaScript-style single-threaded event-loop substitute and call it
multithreading." (The nift.dev pages are not in the repository; these are
the in-repo statements.) The cooperative model therefore **changes the
observable execution model** of `thread()`; the same text bears on `async`,
whose cooperative scheduling the owner accepted provisionally (§11).

**Safe default implemented:** a unit calling `thread()` is refused before
execution and runs on legacy unchanged (`callable:thread`). The cooperative
code path stays in the tree behind the opt-in only to produce the evidence
below.

Evidence (opt-in build path; legacy is the same binary with `NIFT_VM=0`):

1. **Parallel execution:** none on the VM. Every task of an isolate runs on
   the one VM thread (D-001 #7); `thread()` tasks interleave at loop
   safepoints (every 1,024 back-edges), suspension points (`join`, `await`,
   `sleep`) and task ends. VM CPU/wall ≤ 1.0 in every scaling row; legacy
   reaches ~1.9 on 2 cores.
2. **Scaling with `thread()`** (1/2/4/8 threads, wall/CPU, 2 cores):
   exploratory, `cp22_coro.py --scale-only --scale-kind thread`
   (legacy: one OS thread per `thread()`; VM: opt-in, one thread), median of 3:

   | Body | k | Legacy wall / CPU (s) | Legacy CPU/wall | VM wall / CPU (s) | Legacy / VM wall |
   | --- | --- | --- | --- | --- | --- |
   | arith (600k) | 1 | 0.362 / 0.361 | 1.00 | 0.050 / 0.048 | 7.3 |
   | arith | 2 | 0.380 / 0.736 | 1.93 | 0.095 / 0.094 | 4.0 |
   | arith | 4 | 0.779 / 1.483 | 1.90 | 0.193 / 0.192 | 4.0 |
   | arith | 8 | 1.574 / 2.864 | 1.82 | 0.374 / 0.366 | 4.2 |
   | native sha256 (10k) | 1 | 0.577 / 0.559 | 0.97 | 0.475 / 0.468 | 1.21 |
   | native | 2 | 0.578 / 1.097 | 1.90 | 0.928 / 0.919 | **0.62** |
   | native | 4 | 1.115 / 2.181 | 1.96 | 1.816 / 1.807 | **0.61** |
   | native | 8 | 2.233 / 4.310 | 1.93 | 3.634 / 3.602 | **0.61** |

   Same outputs on both engines, every VM run `outcome: vm`. A faster VM
   (arith) is not parity: it is the interpreter's single-thread speed; with
   native-bound work the VM is 1.6× slower from 2 threads on, and on P cores
   the gap grows towards min(k, P) / 1.2 (projection, §11.1).
3. **Blocking without suspending:**
   * tight CPU loop: preempted at loop safepoints, so a thread spinning on
     another's `done()` terminates as in legacy (`th_spin_wait.f`, also
     `busy_poll.f`, VM-only `v_preempt.f`). A single long native call (one
     `sha256` of a large string, one `sort`) is not preempted: other threads
     wait for it to return (bounded by the call; legacy runs them meanwhile).
   * blocking Host call: the task holds the VM thread for the whole call.
     `tests/runtime2/cp22/blocking_probe.py`: thread A `open()`s a FIFO whose
     external writer writes after 2 s, thread B returns `epoch()`: B finished
     after **0-2 ms on legacy, 1,998 ms on the VM** in two runs (same output
     otherwise).
     Starvation for the duration of the block; it becomes a **deadlock where
     legacy would not** whenever the unblocking event depends on another VM
     thread (e.g. a pipe/process handle written by a sibling thread). In the
     admitted VM subset no built-in can write to such a channel (no VM file
     or process handles before CP17/CP20), so no deadlock witness can be
     written today; the hazard arrives with CP17/CP20 unless those Host ops
     suspend the task (record §6).
   * mutex wait / atomics: `mutex()` and `atomic<…>` are refused
     (`callable:mutex`, `callable:atomic<int>`; fixtures
     `refuse/r_thread_mutex.f` 750, `r_thread_atomic.f` 2000 run on legacy).
     Under a cooperative model a `lock()` that blocks instead of suspending
     would deadlock as soon as the holder is suspended: CP23 must make lock
     waits suspension points.
   * join on another thread: a suspension point (never blocks the VM
     thread). Join cycles cannot form: a thread can only reach handles created
     before it (snapshot at creation); self-join reads an unbound name and
     fails as in legacy (`th_self_join.f`, D-009#5-thread).
4. **Cancellation and Errors:** no script-level cancellation in legacy or
   VM; a run stopped internally abandons every task once (coro_unit,
   teardown benchmark; threads use the same task record). Errors:
   `th_error_fatal` (`thread: …`, D-009#5-thread), `th_error_thrown` (the
   original Error, catchable, repeatable join), `th_error_uncaught`,
   `th_status_error`, `th_unjoined` (unjoined failure ignored).
5. **Futures and lifetime:** futures passed into threads and threads into
   async tasks (`th_transfer`, `th_with_async`, `tr_thread` with per-thread
   Host-op traces); structured completion and end-of-run drain as for async
   (`th_unjoined`: a sleeping unjoined thread still prints after main);
   handles cannot flow upwards (`refuse/r_thread_result.f`).
6. **Legacy differential witnesses:** 16 `th_*`/`tr_thread` fixtures
   (join ordering, repeated joins, joins in loops/arrays, snapshot isolation,
   errors, drain), VM-only `v_thread_order` (FIFO interleaving), refusals for
   mutex/atomics/lambda/arity/value/result, and the adversarial generator
   (threads inside tasks and from main, joined in reverse and twice). Legacy
   output with mutexes and atomics is deterministic only for the final
   values, which the refusal fixtures check.

**Options for the owner:**

* **(a) Keep `thread()` on real OS threads, as legacy does** — on the VM this
  needs an isolate per thread (CP23 capture-only isolates: values copied in,
  results copied out, mutex/atomic as shared host objects). Exact legacy
  model; it is CP23 work, not CP22.
* **(b) Refuse `thread()` on the VM until CP23** — current default in this
  candidate; programs using `thread()` keep running on legacy (no fallback
  change for CP25 accounting beyond the existing `callable:thread`).
* **(c) The cooperative model** — only if the owner amends the contract
  above (it is not allowed by the v4.5 text as written); costs: no CPU
  parallelism (§11.1 and item 2), blocking Host ops starve siblings (item 3),
  locks must become suspension points in CP23.

Recommendation: (b) now, (a) in CP23. The same contract text should be
weighed against the provisional acceptance of cooperative `async`.

## 12. Owner decisions, round 3 (2026-10-27): parallelism is preserved

**Decision 1 — `thread()` keeps real parallelism.** Option (b) is approved:
`thread()` is refused before execution on the VM (`callable:thread`) until
CP23 isolates provide genuine parallel execution. Cooperative `thread()` is
never the default.

The test switches `NIFT_VM_COOPERATIVE_THREADS=1` and (round 4)
`NIFT_VM_COOPERATIVE_OBSERVE=1` are kept and are **non-contractual**. They
exist only to run evidence and scheduler tests: the thread fixtures of §11.6
and §11.7, and the corpus fixtures that observe scheduling (§12.1 A5). No
user-facing behaviour, documentation or support depends on them, and they
may be removed at any time. CP23 replaces the thread switch with
isolate-backed parallel threads.

**Decision 2 — parallel `async` keeps real parallelism.**
* The coroutine scheduler stays as internal Runtime 2.0 infrastructure
  (suspension, resumption, futures, timers, the event-loop wait point).
* The VM runs an `async` program cooperatively only when the compiler
  establishes, by the rule below, that cooperative execution cannot differ
  observably from legacy's parallel execution.
* Every other `async` program is refused before execution, with no effects,
  and runs on legacy unchanged.
* No coroutine syntax is added and `async` is not redefined.

### 12.1 Cooperative admission rule (`compiler_coro_admit.{h,cpp}`)

The rule is checked at compile time, after the §5 refusals and the future
flow check, for every unit containing `fn[async]` or `async (…) =>`. It
uses two sets.

**Task code (T)** is every async function and async lambda, plus every
function and lambda reachable from them:
* by a direct call or by naming it;
* lambdas nested inside them;
* when task code calls through a value: every lambda in the unit and every
  function named other than as a direct callee.

**Allowlists:**
* `HOST_OK` = {`sleep`, `epoch`, `getenv`, `os`, `arch`, `platform`,
  `hardware_concurrency`}. `sleep` suspends; the others read process state in
  constant time.
* `TASK_FN` = {`print`, `sleep`, `error`, `type`, `epoch`, `getenv`, `os`,
  `arch`, `platform`, `hardware_concurrency`, `thread`}. `thread` is
  reachable only with the non-contractual switch.
* `TASK_METHOD` = {`done`, `status`, `size`, `length`, `push`, `join`}.

The unit is admitted only if every one of these holds (first violation
reported):

| # | Criterion | Refusal reason |
| --- | --- | --- |
| A1 | No call anywhere in the unit (main included) of a CP16 Host built-in outside `HOST_OK`. While a Host call blocks, legacy's other threads keep running; on the VM nothing else does. | `coro-blocking-host:<name>` |
| A2 | No call cycle among T, counting calls through values as reaching every value target. A recursive task does unbounded CPU work. | `coro-task-recursion` |
| A3 | Every `while`/`for` loop in T has, at the top level of its body, a statement `sleep(<integer literal ≥ 1>)`, so every iteration suspends. `sleep(0)` and `await` do not count: neither is guaranteed to suspend. | `coro-task-loop` |
| A4 | Every built-in function called in T is the unit's own function, a call through a value (itself task code), or in `TASK_FN`. Every method called in T is in `TASK_METHOD`. Data-proportional built-ins such as hashing, sorting, string/JSON/number methods, collection callbacks and file reads are refused. | `coro-task-native:<name>` |
| A5 | No observation of scheduling: no `print` in T; no `epoch` and no `done()` / `status()` call anywhere in the unit. (Round 4, owner request: output and clock and poll results depend on the schedule, see the proof.) | `coro-observe:<name>` |

The existing §5 refusals still apply (captures, modules, structs, async
values, future flow, async recursion). A5 is skipped only under the
non-contractual test switch `NIFT_VM_COOPERATIVE_OBSERVE=1` (VM-only scheduler
tests and the differential corpus that predates A5); it is never on by
default.

#### 12.1.1 Proof of equivalence (round 4)

**What is observable.** The observable result of a run is:
* stdout and stderr: the bytes and their order;
* the exit status;
* the Host-op trace, per trace context and in order within each context
  (rt-diff's rule);
* the filesystem and environment effects.

Wall-clock duration is not a program result (§8): legacy guarantees no upper
bound on any duration. CPU parallelism is a performance property, governed by
§11.1 and A1–A4.

**Legacy's model** (`ParserAsyncPool.cpp`, `ParserExpression.cpp`
`spawn_future`/`await_future`, `Parser.cpp` `finalize_execution_workers`):
* Tasks run on a FIFO-fed pool of P = max(2, hardware_concurrency) OS
  threads.
* A task blocked in `sleep` holds its worker. An awaiting *worker* helps by
  running queued tasks (`run_one`); the main thread does not.
* A task receives its snapshot and arguments at the call.
* A task's result and Error reach other code only through `await`.
* A task completes after its children; the run ends after every task.

Legacy therefore guarantees only these orderings:

| Edge | Ordering |
| --- | --- |
| HB1 | program order within main and within each task |
| HB2 | a call precedes everything its task does |
| HB3 | everything a task (and its descendants) does precedes the return of an `await` on it |
| HB4 | `sleep(n)` lasts at least n ms |

Everything else is unordered. Which interleavings are actually reachable
depends on P and on OS timing: on a 2-worker machine three sleeping tasks
serialise (witness item 2 below).

**(a) Output from tasks (`print`).**
* Legacy's task output interleaving is nondeterministic. The print-order
  tool's item 1 shows several distinct orders across legacy runs of one
  program on this machine.
* Every VM order respects HB1–HB3. Item 4 checks this on generated programs,
  with 0 violations.
* That is necessary but **not sufficient** for "every VM order is one legacy
  can produce". Legacy's reachable set also depends on P. Item 2: legacy with
  P = 2 prints `a c b` every run; the VM prints `c a b`, an order legacy
  produces only with P ≥ 3.
* So VM output order is within legacy's results across machines, but not
  always within legacy's results on the same machine. **`print` in task code
  is refused** (A5, `coro-observe:print`).
* Main's own `print`s stay admitted. Main's output order is HB1 program
  order, and its values come only through `await` (HB3), which is
  deterministic (next paragraph).

**(b) `sleep(n)` and timing.**
* `sleep(n ≥ 1)` guarantees a suspension point and HB4 (VM timers never fire
  early: mutant M3, `v_timer_not_early`). It guarantees nothing about
  legacy's scheduling.
* A program could observe timing through:
  * the clock (`epoch`);
  * a poll of a task's state (`done()`/`status()`);
  * the interleaving of task output.
* All three are refused by A5. The clock witness is item 3: elapsed time
  main observes is ≥ 200 ms on legacy P = 2 and about 100 ms on the VM, for
  the same program.
* The only timing property an admitted program can depend on is HB4, and it
  has no way to observe even that. `sleep` is equivalent to a pure delay;
  removing it would not change the result.

**(c) The theorem.** Under A1–A5, an admitted program's observable result is
the same for every schedule that respects HB1–HB3, and therefore equal on
legacy (any P, any timing) and on the VM.

1. *Tasks are deterministic functions.*
   * A task's inputs are its arguments and its global snapshot, fixed at the
     call (HB2, §3 item 1), plus `getenv` and the other `HOST_OK` reads.
   * Those reads are constant during the run, because A1 refuses `setenv`,
     `cd` and every other effecting Host built-in.
   * Its code has no clock (A5) and no randomness (`secure_random_bytes` is
     refused by A1).
   * So its result value or Error is a function of the call.
2. *Tasks have no observable side effects.*
   * No `print` (A5) and no effecting Host op (A1).
   * Global writes stay in the task's snapshot (§3 item 1).
   * Trace lines for `getenv`/`sleep` go to the task's own context, in task
     program order. Context labels are assigned at the call (`child_label`,
     creation order), so each context's sequence is deterministic.
3. *Main observes tasks only through `await`.*
   * `await` returns the task's (deterministic) result or Error, after HB3.
   * There is no `done`/`status`/`epoch` (A5).
   * Main's own effects are in program order (HB1), and the values it
     prints are deterministic by 1 and 3.
4. *End of run.* Drain (structured completion) waits for every task. A
   task's unawaited failure is ignored (L7) and it printed nothing, so the
   exit status and output do not depend on when it finished.
5. *Progress.*
   * Both engines terminate on admitted programs. Task work is bounded
     (A2–A4); sleeps are finite; await waits only on earlier tasks (no
     cycles, §4); legacy's pool makes progress by `run_one` and FIFO order.
   * Neither can deadlock: the VM's scheduler raises an internal stop
     instead (M1 witness).

So VM result = legacy result. Legacy's runtime errors in task code follow
D-009#5 exactly as §3 records, with policies in EXPECT.tsv.

**Residuals that remain.**
* The `D-009#5-*` diagnostic-location policy.
* Latency, which is not a program result: a long main-program built-in
  delays task wake-ups.

**Witnesses for each claim:**

| Claim | Witnesses |
| --- | --- |
| A1–A4 refusals | `refuse/r_ca_*` (10 fixtures) |
| A5 refusals | `refuse/r_obs_print_default`, `r_obs_epoch_default`, `r_obs_task_epoch_default`, `r_obs_done_default`, `r_obs_status_default` (run with no switch) |
| Default admission of the corpus | `run.py --mode default` runs every `cases/*.f` with no switch; each fixture is either admitted and equal to its golden, or refused for an admission reason before execution with legacy equal to its golden (counts in the Evidence section) |
| Equivalence on generated admitted programs | the adversarial generator (tasks never print; only main prints, after awaits; no clock or poll), run without the observe switch on both dispatch builds, compared with legacy |
| (a), (b) claims | `tests/runtime2/cp22/print_order.py` items 1–4 (log `print-order-r5.log`) |
| Mutants | M12 (A1–A4 disabled) and M13 (A5 disabled) are each caught by their refusal witnesses |
| Fixtures moved to `refuse/` by A1–A4 | `spawn_tree` → `coro-task-loop`; `tr_exists`, `tr_thread` → `coro-blocking-host:exists`; `tr_fs` → `coro-blocking-host:is_dir`; replaced by the admitted `spawn_tree_light`, `tr_env`, `tr_thread_env` |

### 12.2 Nonblocking Host integration

Only `sleep` suspends today. Making other Host built-ins suspend needs
three things:
* each native split into a VM-thread *prepare* step (validate, resolve the
  path, v1 trace), a worker-thread *attempt* step on plain data with no
  `rt::Value`, refcount or heap accounting, and a VM-thread *complete* step
  (classify, v2 trace, record, build the Value);
* the attempt step completing through `WakeSource::post`;
* the Host layer's observer, trace channel and path policy made safe across
  the overlap. `cd` changes the process working directory, which every
  concurrent attempt observes.

The natives live in `src/rt/natives`, owned by CP21/CP19 and outside this
checkpoint's edit rights. This is therefore **blocked**, not dropped: it
remains CP22 work (owner, round 4) and resumes when the CP17/CP19/CP21
interface boundaries are supplied. The design above is the proposal; A1
refuses blocking Host calls until it lands. Interfaces CP22 needs from those
owners: (1) per-built-in prepare/attempt/complete entry points with the
attempt on plain data; (2) a thread-safe HostCall completion (trace v2,
observer record) callable on the VM thread after an off-thread attempt; (3)
a statement of which Host ops are cwd-sensitive, so `cd` can serialise
them; (4) for CP17 handles: completion callbacks posting to the
WakeSource. The
scheduler side (suspend, post, resume, error routing) exists and is tested
through `sleep` and the TSan wake-path tests.

### 12.3 Roadmap boundary (approved in principle, round 4) and exact amendment text

The owner approved the boundary in principle; the documents are not yet
edited. No legacy-supported parallel operation may become silently
cooperative: every one of them is refused before execution today.

| Requirement | Today (CP22 candidate) | Owner |
| --- | --- | --- |
| `thread()` with real parallel execution | refused (`callable:thread`), runs on legacy | **CP23** (isolate per thread) |
| `mutex()`, `atomic<…>` | refused, runs on legacy | **CP23** |
| Parallel `async` for CPU-bound or blocking work | refused (A1–A4), runs on legacy | **CP23** (isolate pool) |
| Isolates, channels | absent | **CP23** |
| Cooperative scheduling, futures, suspension/resumption, timers and cancellation, Error propagation and future lifetimes | implemented (§3, §4, §12.1) | **CP22** |
| Nonblocking Host-operation integration (Host ops that suspend instead of blocking) | blocked on CP17/CP19/CP21 interface boundaries (§12.2); A1 refuses blocking Host ops meanwhile | **CP22**, must not stay unfinished |
| Work-stealing | not required for semantics | optional CP23 implementation choice |

**Proposed exact text for `docs/runtime-2/checkpoints.md`** (rows 39–40;
for the owner to approve verbatim):

```
| CP22 | M7 Concurrency | VM coroutines: cooperative scheduler, futures, suspension/resumption, timers and cancellation, Error propagation and future lifetimes, event loop, nonblocking Host-operation integration; cooperative execution only for programs proven schedule-independent (cp22-coroutines.md §12.1), everything else refused before execution | CP12 (Host integration: CP17, CP19, CP21 interfaces) | 10k sleeps of 10 ms in < 0.2 s; linear spawn scaling to 100k; admitted programs equal legacy (differential corpus, adversarial, mutants); no legacy-supported parallel operation executed cooperatively |
| CP23 | M7 Concurrency | Capture-only isolates, channels, cancellation; real parallel `thread()`, parallel `async` for CPU-bound or blocking work, `mutex`/`atomic`; isolate pool (work-stealing optional) | CP22 | Labs concurrency suite; no data races under TSan; no async/thread program refused for parallelism; legacy-vs-VM CPU scaling at 1/2/4/8 tasks within the agreed factor |
```

**Proposed note for `decisions.md`** (new decision, owner wording to be
supplied): "CP22/CP23 boundary (owner, 2026-10-27): real parallelism of
`thread()` and `async` is preserved; the VM executes cooperatively only
programs whose results are schedule-independent; parallel execution arrives
with CP23 isolates; nonblocking Host integration stays in CP22." CP25's
zero-fallback gate depends on CP23 for these refusals.

None of these requirements is optional, and **CP22 is not complete while
nonblocking Host integration remains open**.

## Evidence (Opus, 2026-10-27; local 2-vCPU container, Linux 6.18, GCC 13.3)

Raw logs: the handover's evidence directory. Same-model work; not an
independent review.

* `make test-runtime2-cp22` (goto and portable switch builds): legacy oracle
  67/67, VM differential 48/48 (3 under the D-009#5-future policy with `.r2`
  goldens), refusals 19/19 before execution, VM-determinism 6/6, per-task
  Host-op traces 3/3, adversarial 80 generated programs x 2 builds (0
  differences, 1,740 tasks), negative controls (dead binary, unrelated
  failure, timeout, unmutated "mutant") rejected.
* `make test-runtime2-cp22-mutants`: 10/10 mutants detected by their named
  witnesses.
* `tests/rt/coro_unit.cpp`: 76/76 (also under ASan+UBSan+LSan);
  `coro_wake_unit.cpp` 124/124 under TSan (posts from threads and the CP16
  host loop).
* `make -k test-rt-vm` (whole VM regression including CP13B, CP14B, CP16 N1,
  closures, `for`, corpus, generators, guard controls): exit 0.
* rt-lint, test-integrity, header-name collisions, certification guard and
  its self-test: pass.

Exploratory performance (`benchmarks/runtime2/cp22_coro.py`; not the
reference platform, so none of these is a gate result):

| Workload | Result |
| --- | --- |
| 10k tasks x `sleep(10)` | 0.024-0.027 s wall (in-process unit: 0.022 s); legacy 1k sleeps: 5.18 s |
| spawn+await, callgrind instructions per spawn | 1k: 5,208; 10k: 5,076; 100k: 4,927; ratio 100k/1k = 0.95 (criterion <= 1.25) |
| spawn wall / peak RSS | 1k 3.3 ms / 14.5 MiB; 10k 8.9 ms / 14.5 MiB; 100k 70 ms / 29.8 MiB |
| yield (two interleaved CPU tasks vs sequential) | +4.9 % wall, 1,954 preemptions per 4M iterations |
| wake-up latency (100 x `sleep(10)`) | 0.27 ms mean beyond the deadline |
| idle (10 tasks sleeping 1 s) | 3 ms CPU over 1.0 s |
| teardown at a 64 MiB memory budget | 78,187 suspended tasks abandoned, run stopped in 0.13 s |
| CPU-bound async (4 tasks, 700k iterations) | legacy 1.25 s wall / 2.34 s CPU (thread pool); VM 0.27 s wall / 0.27 s CPU (one thread) |

### Evidence, round 2 (owner decisions of 2026-10-27; same container)

Run on the final code of this round (threads gated by default; raw logs in
the handover's evidence directory, `*-r2`/`*-r3` names):

* `make test-runtime2-cp22` (goto and switch builds): legacy oracle 91/91,
  VM differential 64/64 (5 under D-009#5 policies: 3 `future`, 2 `thread`),
  refusals 27/27 before execution (incl. `r_thread_default`, the default
  `callable:thread`), VM-only 7/7, per-task traces 4/4, adversarial 80 × 2
  builds with threads (0 differences, 2,200 tasks), negative controls.
* Mutants: 11/11 detected by their named witnesses (M11: `await` accepts a
  thread handle, witness `th_await_thread.f`).
* ASan+UBSan+LSan: coro_unit 76/76; `test-runtime2-cp22-sanitize` vm 64,
  refuse 27, vm-only 7, adversarial seed 2 (30 programs, 616 tasks).
* TSan: `coro_wake_unit` 127/127 on the POSIX host loop and 127/127 on the
  portable (Windows) timer loop (`-DNIFT_HOST_LOOP_PORTABLE`).
* MinGW-w64 (`x86_64-w64-mingw32-g++-posix -fsyntax-only -Wall -Wextra
  -pedantic`): all 26 `.cpp` files changed by CP22 compile without warnings
  (compile check only; nothing run on Windows).
* `make -k test-rt-vm`: exit 0; rt-lint, test-test-integrity (incl. header
  name collisions), certification guard and self-test (27 controls): pass.
* Exploratory scaling (§11.1, §11.7) and the blocking probe (§11.7).
* Refreshed exploratory measurements on the final code
  (`cp22_coro.py --callgrind --runs 5`, `bench3.jsonl`; not gate results):
  10k × `sleep(10)` 0.025 s wall; callgrind instructions per spawn 1k 5,242,
  10k 5,110, 100k 4,961 (0-spawn baseline 3.07 M subtracted), ratio
  100k/1k 0.95 (pre-registered criterion ≤ 1.25); spawn wall / peak RSS 1k
  3.7 ms / 14.8 MiB, 10k 7.0 ms / 14.8 MiB, 100k 50 ms / 29.7 MiB; yield
  overhead +4.1 %; wake-up latency 0.20 ms; idle 2.4 ms CPU per 1 s;
  teardown of 78,187 suspended tasks at the 64 MiB budget 0.12 s.

### Evidence, round 3 (cooperative admission; same container)

These runs used the final code of this round. Raw logs are under the
handover's evidence directory, named `*-r4`.

* **`make test-runtime2-cp22`** (goto and switch builds):

  | Check | Result |
  | --- | --- |
  | Legacy oracle | 106/106 |
  | VM differential | 65/65 per build |
  | Refusals before execution | 41/41 per build |
  | VM-only | 7/7 |
  | Traces | 3/3 |
  | Adversarial | 80 programs × 2 builds, 0 differences, 2,040 tasks |
  | Negative controls | all rejected |

  The witness counts of round 2 (91/64/27) are kept or exceeded. The four
  fixtures the rule now refuses remain as legacy witnesses in `refuse/`.
* **Mutants:** 12/12 caught by their named witnesses. M12 (admission
  disabled) is caught by `refuse/r_ca_cpu_loop` and `r_ca_blocking_host`.
* **ASan+UBSan+LSan:** coro_unit 76/76; corpus VM 65, refuse 41, vm-only 7;
  adversarial seed 2 (30 programs).
* **TSan:** 127/127 on both the POSIX and the portable host loop.
* **`make -k test-rt-vm`:** exit 0. Also passing: rt-lint, test-integrity,
  certification guard and self-test (27). MinGW-w64 syntax check: 27/27
  changed `.cpp` files, 0 warnings.
* **Exploratory measurements** (`bench4.jsonl`):

  | Measurement | Result |
  | --- | --- |
  | 10k sleeps | 0.029 s |
  | Callgrind instructions per spawn (1k/10k/100k) | 5,242 / 5,110 / 4,961, ratio 0.95 |
  | Wake-up latency | 0.20 ms |
  | Idle | 2.8 ms CPU per s |
  | Teardown of 78,187 tasks | 0.14 s |

  The CPU-bound `yield` and `fairness` workloads are now refused on the VM
  (`coro-task-loop`), as the rule requires.

### Evidence, round 4 (A5 and the equivalence proof; same container)

Raw logs: `test-cp22-r6.log`, `mode-*-r5.log`, `print-order-r5.log`,
`mutant-12/13-r5.log`, `lint/integrity/guard*-r5.log`.

* **`make test-runtime2-cp22`** (goto and switch builds), exit 0:

  | Check | Result |
  | --- | --- |
  | coro-unit | 80/80, now including the default-refusal checks |
  | Legacy oracle | 111/111 |
  | VM | 65/65 |
  | Refusals | 46/46 |
  | VM-only | 7/7 |
  | Traces | 3/3 |
  | Adversarial | 80 × 2 builds, with no observe switch, 0 differences |
  | Controls | all rejected |
  | `print_order.py` | pass |

* **`--mode default`** (no test switch), all 65 cases pass:
  * 38 are admitted and equal their golden.
  * 27 are refused before execution and legacy equals the golden:
    * 16 `callable:thread`;
    * 5 `coro-observe:print`;
    * 4 `coro-observe:done`;
    * 2 `coro-observe:status`.
* **`print_order.py`** results:

  | Item | Result |
  | --- | --- |
  | 1 | legacy produced 7 and 10 distinct output orders in 30 runs, in two sessions |
  | 2 | legacy with P = 2 prints `a c b` (10/10 runs); the VM prints `c a b` |
  | 3 | elapsed time is 201-202 ms on legacy and 100-101 ms on the VM |
  | 4 | 40 generated programs, 0 happens-before violations |

* **Mutants M12 and M13:** each caught by its refusal witnesses. Only these
  two were rebuilt this round, to keep heavy use light: the admission code
  is the only mutated source that changed. M1–M11 were last run in round 3
  (12/12), and their sources have not changed since.
* rt-lint, test-integrity, certification guard and self-test: pass.
* **Not re-run this round** (owner: keep heavy use light): the ASan/UBSan
  sanitize target, TSan (no scheduler or wake-path change) and the full
  `test-rt-vm` regression. All three last passed in round 3, before A5 and
  before the coro-unit and `rt_vm_tool` changes of this round.
