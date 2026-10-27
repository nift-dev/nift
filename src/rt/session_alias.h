#pragma once
// Transactional inference of bounds shared by retained storage locations.
#include "rt/compiler.h"
namespace nift::rt {
UnitKinds infer_session_aliases(const Ast&, NodeId, const UnitPlan&, std::size_t,
                               const GlobalPlan*, SessionCompileContext*);
}
