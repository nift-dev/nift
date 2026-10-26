#pragma once
// Shared compiler adaptation for value-returning eval units.
#include "rt/compiler.h"

namespace nift::rt {
void normalize_eval_callbacks(Ast& ast, NodeId root);
} // namespace nift::rt
