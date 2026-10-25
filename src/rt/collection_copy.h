#pragma once
// CP14B binding chains that require independent array values.
#include "rt/ast.h"
#include "rt/infer.h"
namespace nift::rt {
bool collection_binding_copy(const Ast&,const UnitKinds&,const Node&);
}
