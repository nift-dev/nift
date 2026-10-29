#pragma once
// CP22 coroutine admission of the Runtime 2.0 compiler (D-013: before
// execution): where a future may flow, which constructs a coroutine unit
// refuses, and the post-compile bytecode checks (docs cp22-coroutines.md §5).
#include "rt/ast.h"
#include "rt/compiler.h"
#include "rt/infer.h"
#include "rt/proto.h"
#include "rt/resolver.h"

#include <cstdint>
#include <string>

namespace nift::rt {

// Refuses (reason, offset) a coroutine unit whose futures reach a position the
// VM does not reproduce exactly, or that uses an excluded construct. True: ok.
// A unit calling thread() is refused (`callable:thread`) unless
// limits.cooperative_threads (owner decision pending, record §11.6).
bool check_coroutine_unit(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds,
                          const CompileLimits& limits, std::string& reason, std::uint32_t& offset);

// After compilation: no capturing closure, no cell or location in main, no
// collection callback in a coroutine program. True: ok.
bool check_coroutine_program(const Program& program, std::string& reason, std::uint32_t& offset, bool parallel = false);

}  // namespace nift::rt
