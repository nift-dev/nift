// B09 (docs/runtime-2/checkpoints/combined-b3-b5.md section 56): test-only stubs for the
// isolated built-in binaries (builtins-bench, host-random-budget).
//
// Three small functions that the shared natives need (ParallelBlock, parallel_task_identity and
// notify_parallel_wait_states) live in src/rt/parallel_task.cpp, in the same translation unit as
// the isolate task runner (CP23: 1dd496d6, e002dbc5, a5179308). Linking the real object drags in
// the whole VM and, through it, the legacy value bridge, which the isolation contract forbids.
// Production is unaffected: the CLI links everything.
//
// These stubs keep production's behaviour on a thread where no ParallelRuntime runs, which is
// every thread of an isolated binary (it has no VM, so it never starts a pool or creates a task
// record). The two record functions trap on a non-null record instead of diverging silently.
//
// Drift guard (F-2): tests/rt/parallel_isolation_stubs_check.py fails when
//   - a stub's mangled symbol is not defined identically by parallel_task.o (signature drift),
//   - the production bodies change from the fingerprint pinned there (review these stubs),
//   - the parity probe (tests/rt/parallel_isolation_probe.cpp) prints different results linked
//     with the real objects and with these stubs.
#include "rt/parallel_task.h"

namespace nift::rt {

// production: runtime_(running_pool); running_pool is nullptr without a ParallelRuntime, so the
// block is admitted (no block_worker, no starve: C141's offset is unused) and the destructor does nothing.
ParallelBlock::ParallelBlock(ParallelWaitKind kind, std::uint32_t) : runtime_(nullptr), kind_(kind) {}
ParallelBlock::~ParallelBlock() {}

// Unreachable without a VM-created ParallelTaskRecord; production returns 0 for a null record.
std::uint64_t parallel_task_identity(const std::shared_ptr<ParallelTaskRecord>& record) {
    if (record) __builtin_trap();
    return 0;
}

// Unreachable without a VM-created ParallelTaskRecord; production does nothing for a null record.
void notify_parallel_wait_states(const std::shared_ptr<ParallelTaskRecord>& record) {
    if (record) __builtin_trap();
}

} // namespace nift::rt
