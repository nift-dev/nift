#pragma once
// Script invocation bindings (`cmd`, `args`) for a fresh VM-owned session and
// for a script unit compiled on the VM: one description for both.
#include "rt/session.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nift::rt {
// Legacy's script invocation bindings (Parser::install_script_invocation_bindings):
// `cmd` (string) and `args` (array of strings), immutable, deep-readonly. The
// symbols' slots are a session's namespace layout (value cell, binding type).
std::vector<InvocationBinding> invocation_bindings(const std::string& command,
                                                   const std::vector<std::string>& arguments);
// Sets each binding's `shadow`: the target (an Ident) of a declaration
// `name := …` directly in the unit's top-level statement list whose offset
// `in_entry` accepts as the entry script's own text (a linked unit also holds
// its modules' top levels: a module's `args :=` never replaces the entry's
// binding; the linker refuses it, import-scope:invocation-binding). Legacy
// lets the entry's declaration replace the binding (`is_script_invocation`);
// the VM then compiles the unit without it, so the declaration is an ordinary
// top-level binding and any use before it stays refused.
void locate_invocation_shadows(std::vector<InvocationBinding>& bindings, const Ast& ast, NodeId root,
                               const std::function<bool(std::uint32_t)>& in_entry);
// The bindings' names (empty for null): names the entry unit provides to the
// module linker (rt/module_link.h). Views into `invocation`.
std::vector<std::string_view> invocation_names(const std::vector<InvocationBinding>* invocation);
} // namespace nift::rt
