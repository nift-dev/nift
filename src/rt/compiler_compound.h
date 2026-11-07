#pragma once
// D2 family B: a compound assignment on a variable whose rhs can rebind it reads the target
// first (UnitCompiler::compound_snapshot, rt/compiler_expr.h); internal to the compiler.
#include "rt/compiler_expr.h"
