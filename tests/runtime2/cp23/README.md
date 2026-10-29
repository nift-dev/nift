# CP23 local verification

These tests execute compiled VM Programs directly or require the CLI VM and
verify its execution/fallback counters. They do not authorise integration or
reference-host performance acceptance.

The aggregate unit/admission targets are `test-runtime2-cp23`,
`test-runtime2-cp23-sanitize` and `test-runtime2-cp23-tsan`; allocation, mutation,
neighbour and performance campaigns remain explicit separate steps below.

## Functional and adversarial

From the candidate checkout:

```
make -j8 nift .build/cp13b/nift-switch \
  .build/rt/isolate-transfer-unit .build/rt/isolate-task-unit \
  .build/rt/cp23-parallel-unit .build/rt/cp23-shared-unit \
  .build/rt/cp23-ownership-unit .build/rt/cp23-contention-unit \
  .build/rt/cp23-resource-unit .build/rt/cp23-allocation-unit
```

Run each unit with a bounded timeout. `ownership_unit` checks source/worker
symbols (including 70,000 unrelated source names), captured closures, graph identity, shared and task-record cycles,
cyclic imported heap reclamation, 20,000-deep graphs, a 64-task saturated pool,
reuse, queued cancellation, outstanding-work destruction, nested one-permit
progress, mutex cancellation, and output happens-before boundaries.
`contention_unit` checks per-producer FIFO across four OS senders, eight-thread
mutex/atomic increments and 100 close/cancel races. All worker handle boxes
are reconstructed on their owning threads.

`resource_unit` checks worker file/stream closure, managed-file reporting,
Error unwind, cancellation, a one-permit blocking FIFO reader/writer, Linux descriptor balance and live-resource transfer
refusals. It preserves CP17's `host-handle-flow` refusal before script effects.

```
python3 tests/runtime2/cp23/compatibility.py --nift ./nift --output /tmp/cp23-compat.json
python3 tests/runtime2/cp23/compatibility.py --nift .build/cp13b/nift-switch --output /tmp/cp23-switch.json
python3 tests/runtime2/cp23/mutations.py --evidence /tmp/cp23-mutants
```

The 85-row `CP22-CLASSIFICATION.tsv` preserves the historical CP22 corpus.
Every newly admitted fixture requires one VM execution and zero legacy
executions. Four differences are named qualifications; historical goldens are
unchanged. In particular `r_f3_member_print_default` remains an output-order
qualification; its event multiset is checked without prescribing inter-worker
order. Keep the inherited legacy-oracle golden discrepancy visible.

Mutations compile real altered source objects and run the complete ownership
suite, or the parallel suite for the cancellation-completion witness. A detection requires exit 1, a completed suite and the defect's specific
assertion/output signature. Signals, timeouts, missing binaries and build
failures are harness failures. The normal binary must pass first. Mutations
cover output publication, reclamation, future identity, program domains and
member-atom translation, and cancellation completion. The current mutation linker is a Linux/GNU local
certification tool, not a cross-platform certification claim.

## Allocation containment

```
python3 tests/runtime2/cp23/allocation.py --binary .build/rt/cp23-allocation-unit --output /tmp/cp23-oom.json
```

Repeat with `--persistent` and with `--mode unawaited` / `--mode cycle` / `--mode cancelled`.
Each tested site runs in a fresh process. The driver fails one real C++
allocation, or every later C++ allocation on the selected thread role. It
checks fatal Internal propagation, owned heap balance, registry release,
signals and timeouts. It exhausts the bounded workload's source/worker sites;
it does not claim coverage of every program or C allocation in the runtime.
The cyclic mode checks partial construction and linking as well as success.

## Sanitizers

Build the corresponding `.build/rt-san/cp23-*-unit` and
`.build/rt-tsan/cp23-*-unit` targets. The isolate source filename uses an
underscore: `cp23-isolate_task-unit`; the graph target is
`.build/rt-san/isolate-transfer-unit`. Objects and libffi are instrumented.

Run outside a ptrace/tracing sandbox, with ASan shadow memory available:

```
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 <san-binary>
TSAN_OPTIONS=halt_on_error=1 <tsan-binary>
```

Run the allocation driver against the sanitizer allocation binary for each affected mode in both one-shot and persistent configurations too. A skipped, setup-failed or timed-out lane is not a pass. Serialize
heavy campaigns with the workstation's shared heavy-test lock.

## Neighbours and performance

Retain CP22 VM/cooperative, VM-only, trace, binding, adversarial and print-order
checks; v45 async/thread/mutex/atomic scripts; CP17 resource checks; CP20 shell
spawn; and CP21 interactive-host/terminal-lease checks. CP19 reconciliation is
an owner/integrator obligation until its accepted implementation is combined
with this candidate. `run()` remains refused by the current foundation;
legacy fallback is not VM Host-process support.

`performance.py` records raw interleaved samples, paired bootstrap intervals,
CPU/RSS, hardware/affinity, binary hashes, sources, output correctness and VM
statistics. Fixed total CPU work is measured at 1/2/4/8 permits. Website builds
rewrite and verify all 1,000 pages on matched independent fixtures. The
`--confirm-sequential` option is one longer confirmation of the initially
short sequential samples. Keep the first observations and every failed gate;
do not retry until green. Local measurements do not replace authorised
Linode or cross-platform certification.

## R2 cancellation and one-permit regressions

`parallel_unit` uses channel handshakes for pending-status assertions. The CPU
overlap witness runs only when the configured runtime has at least two permits;
all correctness cases also run with one permit. The R2 aggregate campaign uses
1, 2, 4 and 8 permits without forcing a larger pool inside the tests.

```
python3 tests/runtime2/cp23/cancellation.py --nift ./nift --output /tmp/cp23-cancel.json
python3 tests/runtime2/cp23/cancellation.py --nift .build/cp13b/nift-switch --output /tmp/cp23-cancel-switch.json
```

Each command checks 120 completion races plus 8 script-Error/consumed-request
cases at 1/2/4/8 permits, both async futures
and thread joins, repeated cancellation, and two identical observations of each
terminal result. Either completion or catchable cancellation may win the race;
empty failures, mixed outcomes, fatal failures, signals and timeouts fail. The
focused suite additionally checks cancellation after native sleep returns,
already completed script failures, late cancellation and unawaited shutdown.
Allocation mode `cancelled` checks that failures during task/cancellation Error
construction still propagate Internal rather than becoming cancellation.

On Linux the resource suite also cancels a reader blocked in native FIFO I/O,
then releases it with a writer and checks catchable cancellation plus descriptor
and registry balance. The same case runs under ASan/LSan/UBSan and TSan.

## Sleeping-task admission follow-up

The aggregate now includes `cp23-admission-unit` (also ASan/LSan/UBSan and
TSan). It runs 200 and 10,000 sleeping tasks at each of 1/2/4/8 CPU permits,
checks repeated observation, cancellation, pending-ancestor safety, heap
constants, CPU parking, executing-slot and worker bounds, receiver/send/foreign-await resource
exhaustion and clean shutdown. A 100-level await chain must retain its R2 result at every permit count.
The default outstanding budget is 16,384, the ordinary blocking budget is 1,024,
and timer/cleanup compensation is capped at 64;
`ownership_unit` explicitly configures 64 solely to exercise real task-budget
exhaustion. It does not configure more CPU permits to make sleepers pass.

```
python3 tests/runtime2/cp23/admission.py --nift ./nift \
 --switch .build/cp13b/nift-switch --negative-control /path/to/frozen-R2-nift \
 --output /tmp/cp23-admission.json
```

This adds an actual pre-fix binary control and checks CLI VM-only execution in
both dispatch modes. Reference-host latency is never inferred from local wall
times. Allocation mode `timers` checks four concurrent sleepers with one-shot
and persistent allocation failure. The mutation lane adds real topology-limit
and retained-timer-permit defects, each requiring a completed admission suite and
its own signature; normal, dead, timeout and unrelated inputs must be rejected.

Uncaught thread reporting is checked by `cp23-reporting-unit` in the normal,
ASan/UBSan/LSan and TSan aggregates. `thread_diagnostics.py --nift PATH
--switch PATH --r2 PATH --output FILE` records 24 VM executions against a
frozen R2 binary: direct and nested uncaught prefixes are corrected, while
caught Error identity/messages, internal catches, rethrows, ordinary mutex
errors and division diagnostics retain R2 behaviour. The thread-reporting
mutant must fail the completed reporting suite; a timeout is not detection.

R4 adds the `cp23-rejected-wait-unit` normal/sanitized aggregates. Its neutral
record witnesses inspect local waiter IDs and foreign observer leases, preserve
an unrelated owner target and another scheduler sharing the same target, cancel
an admitted wait, then complete a subsequent wait successfully. The
`rejected-wait-cleanup` compiled mutant must fail its completed suite; timeout
and unrelated failures remain invalid detections. Allocation mode `waiting`
exercises pending local and foreign awaits and publication/teardown failures.
`default_controls.py` checks the exact one-fixture CP22 default contract against
actual R2 and corrupt-prefix/location/statistics controls. The complete CP22
default lane is required on both dispatch builds, beyond the sleep-only probe.
