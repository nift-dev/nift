#pragma once
// AST shapes for synchronous argv calls that never expose a command handle.
#include "rt/ast.h"

namespace nift::rt {
struct UnitPlan;
namespace detail {
bool package_process_scope(const UnitPlan& plan, const Node& call);
bool process_program_literal(const Ast& ast, const Node& call);
const Node* process_pipeline_command(const Ast& ast, const Node& call, NodeId& output);
}
} // namespace nift::rt
