#pragma once
// Coroutines in units that import modules (weekend candidate `coro-module`,
// docs/runtime-2/checkpoints/weekend-coro-module.md): which coroutine units
// with static imports the VM admits under the parallel task model, and the
// two refusals that remain (owner rulings): `coro-module-init-task`, a task
// that may start while a module initialises, and `coro-module-capture`, a
// task that may capture module state across the module/coroutine boundary.
// A linked unit is one program (rt/module_link.h), so every CP22/CP23
// analysis applies to the admitted ones unchanged.
#include "rt/ast.h"
#include "rt/compiler.h"
#include "rt/infer.h"
#include "rt/resolver.h"

namespace nift::rt {

// Before compiling, after the persistent-session refusal. True: admitted (or
// not a linked coroutine unit under the parallel model). False: `result`
// holds the refusal, its offset in the linked text and `positioned` (the
// offset is the refused node's own).
bool check_coroutine_modules(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds,
                             const CompileLimits& limits, CompileResult& result);

}  // namespace nift::rt
