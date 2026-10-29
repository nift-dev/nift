#pragma once
// Language validation rule for for-loop and catch bindings (D-009#3 extended,
// owner decision 2026-10-28, CP22 record §12.1.4).
//
// Declarations and parameters may not take a reserved built-in name. A
// for-loop binding (`for (x : …)`, `for ((k, v) : …)`) or a catch binding
// (`catch (x)`) may: `for (range : ranges)` and `catch (err)` are ordinary
// code and both engines agree on them. They disagree when such a binding is
// called: legacy calls the built-in, the VM would call the binding. So a
// call `name(…)` inside the scope of a for-loop or catch binding whose name
// is reserved is invalid. The rule applies to the binding the CP10 parser
// identifies (its body: the loop block or the catch block, nested lambdas
// and functions included), never to other occurrences of the name (members,
// strings, object keys, calls outside the scope).
//
// It is checked before anything runs, whichever engine runs the unit: by the
// CP13 prevalidator (rt/prevalidate.h, a diagnostic and exit 1) and, for
// units that skip it, by the VM's planner (refusal `binding-call-reserved`).
// Self-contained (no RuntimeValue): listed in src/rt/parser-sources.txt.
#include "rt/ast.h"

#include <cstdint>
#include <string>

namespace nift::rt {

struct BindingCallViolation {
    bool found = false;
    std::uint32_t offset = 0;  // the binding's name
    std::uint32_t call = 0;    // the first call of it in its scope
    std::string message;
};

// The first violation in source order of the binding, if any.
BindingCallViolation reserved_binding_call(const Ast& ast);

// CP25-R: whether `name(…)` is called with its callee inside `scope` (nested
// lambdas and functions included). A user function's parameter with a
// reserved built-in name is compiled only when it is never called there:
// legacy then reads and writes it as an ordinary parameter in every context.
bool reserved_name_called_in(const Ast& ast, std::string_view name, const Node& scope);

} // namespace nift::rt
