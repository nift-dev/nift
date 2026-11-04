#pragma once
// The Vm members that may construct an in-place construction key (tooling.md 2.4).
// Tooling review F3: rt::build::InPlaceKey befriends this class, not the whole
// of Vm, and this class befriends exactly the owning members: Vm::cold
// (ObjectSet) and Vm::struct_op (StructAdd), and Vm::op_keyed_mutate only in
// the R14 test mutant build. The key is returned as a prvalue (C++17
// guaranteed elision: never copied or moved; the inline call costs nothing).
#include "rt/build.h"
#include "rt/vm.h"

namespace nift::rt {

class VmInPlace {
    static build::InPlaceKey key() { return build::InPlaceKey(); }
    friend bool Vm::cold(const Instr&, const Proto*, const Note*, Value*, const Value*, std::string&, Fault&);
    friend bool Vm::struct_op(const Instr&, const Note*, Value*, const Value*, Fault&, const Proto*);
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 14
    friend bool Vm::op_keyed_mutate(const Instr&, Value*, Fault&, const Proto*);
#endif
};

} // namespace nift::rt
