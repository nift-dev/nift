#pragma once
// Postfix links (call, member, index, safe navigation) of the expression
// layer: which shapes the legacy evaluator dispatches regularly.
#include "rt/expr_parser.h"

#include <string_view>

namespace nift::rt::detail {

// Builtin methods the legacy evaluator applies to any receiver as a final
// `.method(...)` postfix (ParserExpression.cpp:3125, `known`).
bool known_method(std::string_view name);

} // namespace nift::rt::detail
