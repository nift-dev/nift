#include "rt/vm_ops_guards.h"

#include "rt/shared_state.h"
#include "rt/vm.h"

namespace nift::rt {

bool order_guard(const Value& left, const Value& right, const Note* note, Fault& fault) {
    if ((left.is_number() && right.is_number()) || (left.is(Kind::String) && right.is(Kind::String))) return true;
    return raise(fault, StopReason::Error, note ? note->text : std::string());
}

bool session_numeric(const Value& left, const Value& right, Fault& fault) {
    if (left.is_number() && right.is_number()) return true;
    return raise(fault, StopReason::Error, "arithmetic operators require numeric operands");
}

bool Vm::atomic_update_op(const Instr& in, Value* R, const Value* K, const Proto* proto, const Instr* pc,
                          Fault& fault) {
    const std::uint8_t op = static_cast<std::uint8_t>(in.flags & 0x0fu);
    const std::uint8_t mode = static_cast<std::uint8_t>(in.flags >> 4);
    const Value& operand = ((in.c & kConstantBit) != 0) ? K[in.c & 0x7fffu] : R[in.c];
    SharedOpError error;
    Value result;
    if (!shared_atomic_compound(R[in.b], op, operand, mode == 2, result, error))
        return raise_catchable(fault, error.code.empty() ? "runtime.invalid_argument" : error.code, error.message,
                               proto, report_offset(fault, proto, pc));
    R[in.a] = std::move(result);
    return true;
}

} // namespace nift::rt
