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
| existing `callable:thread`, `callable:mutex`, `atomic…` | OS threads, mutexes and atomics stay on legacy: running VM code on another thread needs isolates (CP23, D-036 assigns `mutex` to CP22/CP23) |
| `fn-flags` | unchanged for deferred bodies and methods |

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

## 9. Scope reconciliation (for the architect; not blocking)

* **"Work-stealing".** Work stealing across OS threads would run one isolate's
  coroutines on several threads, which D-001 #7 rules out (non-atomic
  refcounts, isolate-owned heaps). CP22 therefore delivers a per-isolate
  scheduler; stealing between isolates belongs with CP23's capture-only
  isolates. Recommendation: amend the roadmap title, keep CP22's gates
  (neither gate needs parallelism).
* **CPU parallelism vs legacy.** Legacy async tasks run in parallel on a
  thread pool; VM tasks of one isolate interleave on one thread. CPU-bound
  async programs can be slower on the VM until CP23 isolates; sleeping and
  waiting programs are far faster. Recorded as a known performance difference
  of the opt-in VM, not a semantic change.
* **`Kind::Future`** takes the last free 4-bit value tag. Alternative after
  CP17 lands: futures as CP17 boxed handles (`HandleKind::Future`, the box's
  `release` hook pointing at the scheduler). All future operations are in the
  new files, so the switch is local. Recommendation: keep `Kind::Future`
  (futures are VM values, not Host resources; D-001 #5 lists them apart from
  handles) unless the tag is needed for something else.

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

## Evidence

(Filled in as the implementation lands.)
