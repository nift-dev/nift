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
suite. A detection requires exit 1, a completed suite and the defect's specific
assertion/output signature. Signals, timeouts, missing binaries and build
failures are harness failures. The normal binary must pass first. Mutations
cover output publication, reclamation, future identity, program domains and
member-atom translation. The current mutation linker is a Linux/GNU local
certification tool, not a cross-platform certification claim.

## Allocation containment

```
python3 tests/runtime2/cp23/allocation.py --binary .build/rt/cp23-allocation-unit --output /tmp/cp23-oom.json
```

Repeat with `--persistent` and with `--mode unawaited` / `--mode cycle`.
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

Run the allocation driver against the sanitizer allocation binary in all six
modes too. A skipped, setup-failed or timed-out lane is not a pass. Serialize
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
