#pragma once
// Monotone nested collection effects for the persistent compiler namespace.
#include "rt/infer_types.h"
namespace nift::rt {
NodeId session_mutation_root(const Ast&, NodeId receiver);
infer_detail::TyRef session_mutation_shape(const Ast&, NodeId receiver,
    const infer_detail::TyRef& root, const infer_detail::TyRef& element);
} // namespace nift::rt
