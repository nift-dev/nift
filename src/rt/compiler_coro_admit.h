#pragma once
// CP22 cooperative admission (owner decisions 2026-10-27/28, record §12.1):
// the VM runs a coroutine unit on its single-threaded scheduler only when the
// compiled program is proven schedule-independent and free of CPU-parallel or
// blocking work. The check reads the compiled bytecode, so every operation is
// identified exactly (a built-in method, a callable stored in a field and a
// thread `join` are different instructions); anything it cannot classify is
// refused. Everything refused runs on legacy, before any effect.
#include "rt/ast.h"
#include "rt/compiler.h"
#include "rt/proto.h"
#include "rt/resolver.h"

#include <cstdint>
#include <string>

namespace nift::rt {

// True: admitted. Otherwise `reason` / `offset` name the first violation:
//   coro-blocking-host:<name>  A1: a CP16 Host built-in other than sleep and the
//                              constant-time environment reads, anywhere
//   coro-task-recursion        A2: a call cycle among task code (calls through
//                              values reach every function value)
//   coro-task-loop             A3: a loop in task code whose body does not start
//                              with `sleep(<integer literal >= 1>)`
//   coro-task-native:<name>    A4: a built-in in task code outside the constant-
//                              time allowlist (identified by id, not by name)
//   coro-task-op:<opcode>      A4: an instruction task code may not execute
//   coro-observe:<name>        A5: `print` in task code, `epoch`, `done()` or
//                              `status()` anywhere (skipped when `observe`)
// Task code: the async protos and every proto they reach by Call or Spawn,
// and, when task code calls through a value (CallValue, CallValueAny, which
// includes a callable read from an object field), every proto that is ever a
// function value (LoadFunction or Closure anywhere in the program).
bool check_cooperative_admission(const Program& program, bool observe, std::string& reason,
                                 std::uint32_t& offset);

// A3 marker: a loop body whose first statement is `sleep(<integer literal
// >= 1>)` with the built-in `sleep`; the compiler records it in the loop's
// Yield (a = 1), so every iteration, `continue` included, suspends first.
bool suspending_loop_body(const Ast& ast, const Node& body, const UnitPlan& plan);

// The two non-contractual test switches (record §12), off by default:
// NIFT_VM_COOPERATIVE_THREADS=1 (thread() as a cooperative task) and
// NIFT_VM_COOPERATIVE_OBSERVE=1 (A5 off, for VM-only scheduler tests).
void cooperative_switches(CompileLimits& limits);

}  // namespace nift::rt
