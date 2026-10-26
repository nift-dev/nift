#pragma once
// Shared compiler adaptation for value-returning eval units.
#include "rt/compiler.h"

namespace nift::rt {
struct UnitKinds;
void normalize_eval_callbacks(Ast& ast, NodeId root, const UnitKinds* kinds = nullptr);
void normalize_session_callbacks(Ast& ast, NodeId root, const SessionCompileContext& context);
} // namespace nift::rt
