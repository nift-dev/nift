#pragma once
// Static resolution of free names in functions to top-level bindings (CP12
// phase B, "globals"): where legacy's dynamic scoping provably finds the
// top-level binding on every call, a free name is that global; anywhere else
// the unit stays refused, with a reason that says why.
//
// Legacy resolves a name a function does not declare at call time, through
// the scopes of the active calls and then the top level (semantics.md "Free
// names", language-consistency L25). The lexical meaning (the top-level
// binding) and the dynamic one agree exactly when, for a free name `x` used
// in function `f`:
//   G1  `x` is declared exactly once outside functions, directly in the
//       program (not in a block, not by a destructuring pattern) and never
//       in a top-level block (no block binding can shadow it while `f` runs);
//   G2  no function that can be active when `f` runs (`f` and its transitive
//       callers) declares `x` (parameter or local, any block);
//   G3  every top-level statement from which `f` is reachable comes after the
//       declaration of `x` (so the binding exists whenever `f` runs; the
//       declaring statement itself may not reach `f`).
// Refusals: `dynamic-caller` (no top-level binding; only a caller's local
// can resolve it: dynamic scoping proper, GETDYN), `dynamic-shadow` (G2),
// `not-one-top-level-binding` (G1), `before-declaration` (G3), `undeclared`
// and `legacy-binding` (a name legacy itself provides, e.g. `args`).
// One meaning everywhere (D-018): the rule is per (function, name), not per
// call. Where it fails the name is refused (`free-name:<why>`), never given
// a compatibility path. Errors stay loud (D-020): an assignment through a
// resolved global keeps the global's binding-type check (compiler hook).
#include "rt/ast.h"
#include "rt/resolver.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nift::rt {

struct GlobalRef {
    bool ok = false;
    NodeId decl = kNoNode;       // the top-level Declare statement (ok)
    std::uint32_t position = 0;  // its index among the program's statements (ok)
    bool written = false;        // some function assigns it (=, compound, ++/--)
    std::string refusal;         // `free-name:<why>` when !ok
};

struct GlobalPlan {
    // free[f]: every free name of plan.functions[f] (read or written) and its
    // resolution. A name absent from free[f] is not free in f.
    std::vector<std::unordered_map<std::string_view, GlobalRef>> free;
    // Top-level names some function resolves to (G1-G3 hold for every use).
    std::unordered_map<std::string_view, NodeId> globals;
};

GlobalPlan plan_globals(const Ast& ast, NodeId root, const UnitPlan& plan);

} // namespace nift::rt
