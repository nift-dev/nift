#pragma once
// Constant data expressions used by imported module initializers.
#include "rt/ast.h"

#include <optional>
#include <string>

namespace nift::rt {
struct UnitPlan;

// bytes([ASCII octets]).decode("utf-8"), with no binding or user call.
std::optional<std::string> module_ascii_literal(const Ast& ast, const Node& call);
bool imported_module_data(const UnitPlan& plan, const Node& call);
}
