#pragma once
// Legacy argument aliasing analysis for the Runtime 2.0 compiler (CP11,
// review D6). Legacy passes an argument written as a member or index place
// (`f(o.k)`, `f(a[0])`, also parenthesised) as a location reference: a callee
// that mutates its parameter changes the caller's aggregate, and the alias
// propagates when the callee passes the parameter on (`g(q) { f(q) }`). The
// VM copies arguments, so a call that passes a place to a parameter the
// callee may mutate or rebind is refused at compile time.
#include "rt/resolver.h"

namespace nift::rt {

// Fills plan.escaping: parameter k of function f escapes when the body of f
// (any nested block) uses the parameter's name as a method-call receiver
// other than `.length()`/`.size()` and the read-only CP14 built-in methods, as the object of an index assignment, as
// the target of `=`, a compound assignment or `++`/`--`, or as a bare-name
// argument to a parameter of a user function that escapes (fixpoint).
// Conservative: a block-local shadowing binding of the same name counts too.
// Left empty when no call in `root` passes a place argument.
void plan_escaping_params(const Ast& ast, NodeId root, UnitPlan& plan);

// True when `node` is an argument written as a place other than a bare name
// (member or index access).
bool is_place_argument(const Ast& ast, NodeId node);

} // namespace nift::rt
