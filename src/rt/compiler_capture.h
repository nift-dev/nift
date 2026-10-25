#pragma once
// CP12C static capture analysis: mark the local/parameter declaration nodes a
// nested lambda captures, and the direct top-level declarations that remain
// globals. Used by the compiler before any code is emitted.
#include "rt/ast.h"
#include "rt/infer.h"
#include "rt/resolver.h"

#include <cstdint>
#include <unordered_set>

namespace nift::rt::detail {

void prepare_capture_bindings(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds,
                              std::unordered_set<NodeId>& top_level_decls,
                              std::unordered_set<NodeId>& captured,
                              std::unordered_set<NodeId>& selector_roots);

} // namespace nift::rt::detail
