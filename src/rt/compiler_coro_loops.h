#pragma once
// CP22 cooperative admission, rule A3 (record §12.1).
#include "rt/proto.h"

#include <cstddef>

namespace nift::rt {

// A3 on one proto's bytecode: every cycle of its control-flow graph passes
// through the genuine suspension primitive, a `Sleep` instruction (the
// built-in sleep, bound by id) whose argument the same basic block loads with
// an integer constant >= 1. Otherwise `head` is where an unsuspending cycle
// closes (compiler_coro_loops.cpp).
bool loops_suspend(const Proto& proto, std::size_t& head);

}  // namespace nift::rt
