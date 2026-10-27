#pragma once
// CP22 cooperative admission (owner decision 2026-10-27, record §12): the VM
// runs a coroutine unit on its single-threaded scheduler only when the
// compiler establishes that cooperative execution cannot differ from legacy's
// parallel execution observably: the tasks do bounded, non-blocking work
// between suspension points and nothing in the unit blocks in a Host call.
// Everything else is refused before execution and runs on legacy.
#include "rt/ast.h"
#include "rt/compiler.h"
#include "rt/resolver.h"

#include <cstdint>
#include <string>

namespace nift::rt {

// True: admitted. Otherwise `reason` / `offset` name the first violation:
//   coro-blocking-host:<name>  a Host built-in other than sleep and the
//                              constant-time environment reads, anywhere in the unit
//   coro-task-recursion        a call cycle among the functions tasks can run
//   coro-task-loop             a loop in task code whose body does not
//                              unconditionally suspend (`sleep(<literal >= 1>)`)
//   coro-task-native:<name>    a function or method in task code outside the
//                              constant-time allowlist
//   coro-observe:<name>        an observation of scheduling: `print` in task
//                              code, `epoch` or `done()`/`status()` anywhere
//                              (skipped when `observe`, the non-contractual
//                              NIFT_VM_COOPERATIVE_OBSERVE=1 test switch)
bool check_cooperative_admission(const Ast& ast, NodeId root, const UnitPlan& plan, bool observe,
                                 std::string& reason, std::uint32_t& offset);

// The two non-contractual test switches (record §12), off by default:
// NIFT_VM_COOPERATIVE_THREADS=1 (thread() as a cooperative task) and
// NIFT_VM_COOPERATIVE_OBSERVE=1 (A5 off, for VM-only scheduler tests).
void cooperative_switches(CompileLimits& limits);

}  // namespace nift::rt
