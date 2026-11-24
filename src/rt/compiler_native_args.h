#pragma once
// Shared static native argument validation.
#include "rt/compiler_expr.h"

namespace nift::rt::detail {
const Node* direct_null_index_call(const Ast&, NodeId root, NodeId indexed);
bool plain_readonly_arity_string(const Ast&, const Node&, const Binding*);
}
