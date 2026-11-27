#pragma once
// Binary operator lowering (arithmetic, ordering, concatenation and guards).
#include "rt/compiler_expr.h"

namespace nift::rt::detail {

OpCode fused_not_jump(Op op);
bool ordering_kinds_ok(KindSet l, KindSet r);

} // namespace nift::rt::detail
