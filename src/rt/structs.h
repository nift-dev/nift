#pragma once
// CP12b structs: the struct table of a unit (UnitPlan::structs, rt/resolver.h).
//
// Legacy `struct (Name) { fields; methods }` declarations are admitted when
// they are exact on the VM, which keeps instances as values (D-040 V9) and
// admits only units where no instance is observably shared (the compiler's
// struct rules, compiler_structs.cpp):
//   - declarations at the top level, before any statement that runs (legacy
//     registers a struct when its declaration runs), with unique names that
//     no function shares (L47, L48);
//   - field initialisers are constants (legacy evaluates them at each
//     construction with the bindings visible then, L49);
//   - methods are plain functions of the struct (no async, no variadics),
//     named unlike top-level functions and fields; the constructor is the
//     method named like the struct.
// Refusals: struct-nested, struct-after-statement, struct-redeclared,
// struct-shadows-fn, struct-name, struct-field-init, struct-method,
// struct-member-clash.
#include "rt/resolver.h"

namespace nift::rt {

// Fills plan.structs / struct_index and appends every method to
// plan.functions (method_of: its struct). False (plan refused) otherwise.
bool plan_structs(const Ast& ast, NodeId root, UnitPlan& plan);

} // namespace nift::rt
