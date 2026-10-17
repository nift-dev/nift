#pragma once
// Control-flow statements of the statement layer: comments, while, try/catch,
// for, if/else chains, return, throw, break and continue. The
// StatementParser members are defined in control_flow.cpp.
#include "rt/statement_parser.h"

namespace nift::rt::detail {

// First bytes of every keyword/comment form; any other statement goes
// straight to the general statement rule.
bool may_start_keyword(char c);

} // namespace nift::rt::detail
