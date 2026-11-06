#pragma once
// B4b 22: the kind inference's fixed-point check (tests): one more walk of every unit after
// convergence changes no node's kinds unless an invalidation was missed.
#include "rt/infer_unit.h"

#include <string>
#include <vector>

namespace nift::rt {
// B4b 27: the fixed-point gate build's failure (stderr `B4B-FIXPOINT:`, exit 86).
[[noreturn]] void fixpoint_gate_fail(const Ast& ast, const std::vector<NodeId>& stale,
                                     const std::vector<std::string>& state, bool session);
}
