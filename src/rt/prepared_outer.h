#pragma once
// Stage 4 (coordinator ruling, VM subset; no language change): the outer
// bindings a parameter copy `out := p` in a prepared function may meet.
//
// Legacy types `out` when it prepares the body, from a binding named `p` in
// the caller's scopes (decl_type). An int-typed binding there types it like
// no binding at all, from the value (probes glob/h_*.f: a string or double
// binding wins, of any origin; an int one, or one from untyped text holding
// an integral number, does not). So the copy keeps p's type exactly when
// every binding of that name outside the function is certainly int typed.
#include "rt/infer.h"
#include "rt/resolver.h"

#include <cstddef>
#include <string_view>

namespace nift::rt {

// True when every declaration of `name` outside function `f` (top level and
// the other functions) is int text, or untyped text whose value kinds are
// certainly int, and no other function has a parameter `name`. Without
// kinds (nullptr) only the absence of such bindings qualifies.
bool outer_bindings_int_typed(const Ast& ast, NodeId root, const UnitPlan& plan, std::size_t f,
                              std::string_view name, const UnitKinds* kinds);

} // namespace nift::rt
