# CP23 R2 / CP19 integration obligations

This is a source-level handover, not an integrated build or acceptance. CP23 is
based on accepted foundation `475f5192f6ca510d19602e42041d6e10a88efc78`.
CP19 comparison identity is independently accepted R2
`846bba1e6247a5730c0f58494cf9dc22b412c2e6`, tree
`1695836cf7d5cebababdd8f46f0edb21836e57fb`. Claude owns reconciliation; this
correction round does not merge CP19 or modify the authoritative branch.

1. Combine the two `Vm::~Vm` definitions: CP23 aborts and joins children in
   `vm_parallel_cleanup.cpp`; CP19 calls `handle_table_.shutdown_noexcept()` in
   `vm_handles.cpp`. Workers must settle before resources they could use close.
2. Reconcile CP23 `finish_run` with CP19 `complete_run`: session command completion
   must retain the persistent Script context. Preserve CP19 `close_handle_runtime`
   `on_error` semantics while draining CP23 output and finishing structured tasks.
3. CP19 constructs its VM from a bootstrap Halt Program, then replaces linked
   protos per unit. CP23 task scheduling/maps use the initial Program. Define
   session scheduler ownership, retained-code survival and callable/task domain
   remapping before permitting session coroutine execution. Keep
   `session:coroutine` refused meanwhile. Never reuse a map across unrelated
   domains or retire code while a worker can reference it.
4. Session cancellation/finish must abort and join workers and drain output before
   closing session resource contexts. Retain exactly-once cleanup and explicit
   handling of workers blocked in native I/O; no forced OS-thread termination.
5. Mutex owner tokens identify a VM, not a command or OS thread. Test ownership
   across units and VM reset/finish. Failed owners do not silently unlock a mutex;
   cancellation must wake contenders without creating a second owner.
6. Preserve independent cleanup `host_fault`s. CP19's reviewed `session.cpp`
   currently replaces the resource result's host fault fields with JobTable
   fields. Reconciliation must retain both independent faults and messages,
   including a primary script Error plus cleanup failures.
7. CP19 SessionJobs/CP21 JobTable remains the sole ProcessGroup owner. CP23 task
   proxies do not become ProcessGroup, JobTable or CP17 HandleTable owners. Test
   command-to-command jobs, cancellation, lifecycle-ticket settlement and separate
   worker/Script/job cleanup failures. Preserve CP20 F6 unchanged.
8. `callable:run` remains refused. A legacy fallback is not compiled VM Host-process
   support. Preserve CP22 admission and section 6.1 lifecycle obligations.

Required integrated witnesses include persistent file/stream use across units,
job survival/cancellation and exactly-once settlement, async/shared/callable
cross-unit refusals or accepted behavior, mutex owners across command boundaries,
session teardown with blocked workers, exception/abandonment paths and independent
resource/job faults. Run both dispatch modes, relevant real mutants, allocation
containment, sanitizers and the unchanged CP19 performance gates. Standalone CP19
or CP23 acceptance cannot substitute for those integrated results.

The R2 evidence includes a read-only three-way `git merge-tree` comparison with
foundation as merge base and a conflict index. It does not resolve source
conflicts or create an integrated commit. R2's additional shared-file change is
`ParallelTaskRecord::completion_started`, read/written under its mutex by worker
completion and `shared_cancel`; preserve that linearization in reconciliation.

Textual conflicts in the read-only R1/CP19 comparison: `Makefile`,
`compiler.cpp`, `compiler_closure.cpp`, `compiler_expr.cpp`, `compiler_expr.h`,
`infer.cpp`, `vm.cpp`, `vm.h`, `vm_handles.cpp`, `vm_ops.cpp`, and `vm_report.cpp`
(all source names under `src/rt/`). A clean textual merge elsewhere does not
resolve the semantic ownership obligations above.
