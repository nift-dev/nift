#pragma once
// Structural limits of a compilation unit, checked before any analysis
// (CP11 review round 3, F3).
//
// Every later pass over the AST (planning, aliasing, the prepared-context
// analysis, code generation) may recurse on the C++ stack along the tree, so
// the unit's tree depth is bounded first, by an iterative walk: a unit deeper
// than kMaxUnitTreeDepth nodes (statements and expressions together, e.g. a
// 100,000-term `1 + 1 + …` chain, which is left-nested) is refused
// (`nesting-limit`) and legacy runs it. No input can then exhaust the stack
// during compilation.
#include "rt/ast.h"

#include <cstddef>
#include <cstdint>

namespace nift::rt {

inline constexpr std::size_t kMaxUnitTreeDepth = 256;

struct UnitShape {
    bool ok = true;
    std::uint32_t offset = 0;  // the first node found beyond the limit
};

UnitShape check_unit_depth(const Ast& ast, NodeId root);

// Pre-order first Opaque node of the subtree, iteratively (kNoNode if none).
NodeId first_opaque_node(const Ast& ast, NodeId root);

} // namespace nift::rt
