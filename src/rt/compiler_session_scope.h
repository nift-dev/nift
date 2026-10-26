#pragma once
// Cold lexical and source-context admission proofs for persistent bindings.
#include "rt/ast.h"
#include "rt/resolver.h"
namespace nift::rt::detail {
bool session_method_uses_bare_field(const Ast&, NodeId schema, std::string_view field);
bool session_lambda_assignment_spelling(const Ast&, const Node&);
bool session_isolated_method_local(const Ast&, const UnitPlan&, const Node&);
}
