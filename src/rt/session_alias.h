#pragma once
// Transactional inference of bounds shared by retained storage locations.
#include "rt/compiler.h"
namespace nift::rt {
// The limits' kind walks, session transaction and script invocation bindings.
UnitKinds infer_session_aliases(const Ast&, NodeId, const UnitPlan&, const GlobalPlan*, const CompileLimits&);
}
