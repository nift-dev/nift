#pragma once
// Script invocation bindings (`cmd`, `args`) for a fresh VM-owned session and
// for a script unit compiled on the VM: one description for both.
#include "rt/session.h"

#include <string>
#include <vector>

namespace nift::rt {
// Legacy's script invocation bindings (Parser::install_script_invocation_bindings):
// `cmd` (string) and `args` (array of strings), immutable, deep-readonly. The
// symbols' slots are a session's namespace layout (value cell, binding type).
std::vector<InvocationBinding> invocation_bindings(const std::string& command,
                                                   const std::vector<std::string>& arguments);
// The target (an Ident) of the declaration `name := …` directly in the
// program's top-level statement list, or kNoNode. Legacy lets such a
// declaration replace the invocation binding (`is_script_invocation`); the
// VM then compiles the unit without that binding, so the declaration is an
// ordinary top-level binding and any use before it stays refused.
NodeId invocation_shadow(const Ast& ast, NodeId root, std::string_view name);
// The bindings' names (empty for null): names the entry unit provides to the
// module linker (rt/module_link.h). Views into `invocation`.
std::vector<std::string_view> invocation_names(const std::vector<InvocationBinding>* invocation);
} // namespace nift::rt
