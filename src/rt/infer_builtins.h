#pragma once
// Result types of calls to natives for the kind inference (CP12 rt/infer.h):
// print, the Length opcode's length()/size() and the CP14 built-ins, bound
// with the compiler's own rule (rt/builtin_bind.h). Internal to the inference.
#include "rt/infer_types.h"

#include <vector>

namespace nift::rt::infer_detail {

// `recv.name(values...)` (call node `n`): int for length()/size(), the bound
// built-in's result, the empty type when the receiver kinds are empty (never
// executed), Unknown when not bound (the compiler refuses the call).
TyRef method_result(const Ast& ast, const Node& n, const TyRef& recv, const std::vector<TyRef>& values);
// `name(values...)` with no user function of that name: print (null), a free
// built-in's result, or Unknown.
TyRef function_result(const Ast& ast, const Node& n, const std::vector<TyRef>& values);

} // namespace nift::rt::infer_detail
