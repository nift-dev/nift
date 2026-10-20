#pragma once
// Mutation gate (review CP12 round 2): deliberate VM bugs, compiled only with
// -DNIFT_VM_MUTANTS (`make test-rt-vm-mutants`) and selected at run time with
// NIFT_VM_MUTANT=n. In every other build each macro expands to nothing.
//   M1 Sub dispatched to Add in the checked table      M5 wrong error column
//   M2 large slow-path results off by 1000 (vm_ops)   M6 wrong index number in the message (vm_ops)
//   M3 spurious overflow in AddI for small operands   M7 wrong kinds in the assignment message (vm_ops)
//   M4 a dropped loop iteration (i += 1 from 1 to 3)  M8 the Mod fast path's sign
//   M9 BindType ignores a text type held in a register (a declaration from a
//      parameter, stage 4) and binds with the unknown type -1
//   M10 (compiler) a path argument is admitted whatever its kinds (aggregates too)
//   M11 a missing member in a path argument gets the value wording, not the location one (vm_ops)
//   M12 (prepared_outer) a double-typed outer binding of a parameter copy's name is admitted
#include "rt/vm_ops.h"

#ifdef NIFT_VM_MUTANTS
#define MUTATE_ADDI(x, offset)                                                                       \
    if (vm_mutant() == 3 && (x).as_int() >= 100) {                                                  \
        raise(fault, StopReason::Error, "signed 64-bit integer overflow");                          \
        FAIL();                                                                                      \
    }                                                                                                \
    if (vm_mutant() == 4 && (x).as_int() == 1 && (offset) == 1) { R[pc->a] = Value::integer(3); NEXT(); }
#define MUTATE_MOD(v) ((vm_mutant() == 8) ? -(v) : (v))
#define MUTATE_CHECKED_TABLE(table) \
    if (vm_mutant() == 1) (table)[static_cast<std::size_t>(OpCode::Sub)] = &&LC_Add;
#define MUTATE_ERROR_OFFSET(offset) \
    if (vm_mutant() == 5) ++(offset);
#define BIND_TEXT(c) ((vm_mutant() == 9 && !((c) & kConstantBit)) ? std::int64_t{-1} : RK(c).as_int())
#define MUTATE_PATH_SCALAR(ok) \
    if (vm_mutant() == 10) (ok) = true;
#define MUTATE_OUTER_DOUBLE(ok, known) \
    if (vm_mutant() == 12 && (known) == 3) (ok) = true;
#else
#define MUTATE_ADDI(x, offset)
#define MUTATE_MOD(v) (v)
#define MUTATE_CHECKED_TABLE(table)
#define MUTATE_ERROR_OFFSET(offset)
#define BIND_TEXT(c) (RK(c).as_int())
#define MUTATE_PATH_SCALAR(ok)
#define MUTATE_OUTER_DOUBLE(ok, known)
#endif
