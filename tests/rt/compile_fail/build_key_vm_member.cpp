// Tooling review F3 (x/k5_vm.cpp): a Vm member that is not an in-place owner
// (Vm::op_iter_check) cannot construct an in-place key. Must NOT compile.
#include "rt/vm.h"
namespace nift::rt {
bool Vm::op_iter_check(const Instr& in, Value* R, Fault& fault) {
    const build::InPlaceKey key;
    Construction::array_push(key, R[in.a], Value::integer(99));
    (void)fault; return true;
}
}
