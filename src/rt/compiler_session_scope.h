#pragma once
// Cold proof for a method's explicit locals that cannot reach another reader.
#include "rt/ast.h"
#include "rt/resolver.h"
namespace nift::rt::detail {
bool session_isolated_method_local(const Ast&, const UnitPlan&, const Node&);
}
