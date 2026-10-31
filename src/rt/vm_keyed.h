#pragma once
// D-063 / D-NEXT-refsem: the map/set/stack mutator opcode (KeyedMutate,
// Vm::op_keyed_mutate in vm_keyed.cpp) and the reference-cycle refusal shared
// with struct field stores (Vm::refuse_reference_cycle).
#include "rt/keyed.h"

namespace nift::rt {

// Legacy's wording when a stored struct or collection reaches the receiver
// through legacy's own tagged references (map set: "set would create a cyclic
// reference", stack push: "push would create a cyclic reference"), or null
// where legacy has no refusal (the VM's generic runtime.reference_cycle text).
const char* keyed_cycle_legacy_text(KeyedMethod method) noexcept;

} // namespace nift::rt
