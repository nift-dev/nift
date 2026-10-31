#pragma once
// The Phase A forward-compatibility guard (D-NEXT-refsem #1): arrays and
// objects stay copy-on-write values until Phase B makes them references, and
// the VM refuses, before any effect, every unit whose output could depend on
// which of the two models runs it, so that no program the VM accepts now
// changes meaning when Phase B lands.
//
// The two models differ only when an array or object that two holders can
// reach (two bindings, a binding and a container, a caller and a callee, a
// future and its awaiters) is mutated in place and the other holder is
// observed. The guard is a flow-insensitive analysis over the unit's syntax
// and the kind inference. For every binding B that may hold an array or
// object and is the root of an in-place mutation (a mutating method, an
// element or member assignment, a nested one, or B passed as the `x = f(x)`
// idiom argument to a mutating parameter) it requires B to be exclusive:
//   * B's value never reaches another holder: B is never read in a retaining
//     position (the value of a declaration, assignment, return, literal item,
//     container store, a user function's argument to a parameter that
//     escapes, an expression-bodied lambda's result), and, when B is mutated
//     below its top level, neither is any part of it (an element, a member,
//     an element-returning method's result, a for-loop binding over B);
//   * every value flowing into B is fresh: a literal, a constructing built-in
//     or method, a user function whose every return is fresh, `deepcopy(x)`
//     (and `copy(x)` for top-level mutation only); for a parameter, every
//     call site's argument, or the `x = f(x)` idiom with x exclusive itself;
//     when B is mutated below its top level, fresh at every depth (and so is
//     every value stored into it).
// A unit compiled for a persistent session assumes every binding may be
// mutated by a later statement. Refusal: `reference-guard:<route>`.
#include "rt/ast.h"
#include "rt/infer.h"
#include "rt/resolver.h"

#include <cstdint>
#include <string>

namespace nift::rt {

// False (with reason and source offset) when the unit must be refused.
bool check_reference_guard(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds, bool session,
                           std::string& reason, std::uint32_t& offset);

} // namespace nift::rt
