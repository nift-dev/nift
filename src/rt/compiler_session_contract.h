#pragma once
// Check persistent structural bounds after the whole-unit effects fixed point.
#include "rt/compiler.h"
#include "rt/infer.h"
namespace nift::rt::detail {
struct SessionContractRefusal { NodeId node = kNoNode; const char* reason = nullptr; };
SessionContractRefusal session_shape_contract(const SessionCompileContext&, const UnitKinds&);
}
