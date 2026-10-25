#pragma once
// Finite comparator target and transitive location-effect admission.
#include "rt/infer.h"
#include <string_view>
namespace nift::rt {
// Empty means proved disjoint; nonempty strings are evidence categories only.
std::string_view collection_sort_effects(const Ast&, NodeId root, NodeId callback,
                                        NodeId receiver, const UnitKinds&, bool invoke = true);
}
