#pragma once
// Shared static native argument validation.
#include "rt/compiler_expr.h"

namespace nift::rt::detail {
bool plain_readonly_arity_string(const Ast&, const Node&, const Binding*);
}
