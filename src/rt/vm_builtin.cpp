#include "rt/vm_builtin.h"

#include "rt/native.h"
#include "rt/natives/builtin.h"

#include <utility>

// The VM's call into the CP14 built-in natives (CallNative with an id from
// kFirstBuiltin on): off the hot path, like every CallNative (Vm::cold).

namespace nift::rt {

// CP14: a pure built-in over R[a .. a+c) (receiver first for a method). The
// natives store `out` last, so the result may replace R[a] directly; the
// argument registers are temporaries of this call and are cleared.
bool Vm::builtin(const Instr& in, Value* R, Fault& fault) {
    const std::size_t id = static_cast<std::size_t>(in.b - kFirstBuiltin);
    if (id >= natives::builtin_count()) return raise(fault, StopReason::Internal, "unknown built-in");
    const natives::BuiltinSpec& spec = natives::builtin_spec(static_cast<natives::BuiltinId>(id));
    natives::Context context;
    context.heap_limit = heap_limit_;
    Value out;
    std::string message;
    switch (natives::call_builtin(spec, context, &R[in.a], in.c, out, message)) {
        case natives::Status::Ok: break;
        case natives::Status::Error: return raise(fault, StopReason::Error, std::move(message));
        case natives::Status::Internal:
            // A receiver or argument kind the compiler promised away.
            return raise(fault, StopReason::Internal,
                         "unexpected operands (compiler defect): built-in " + std::string(spec.name));
        case natives::Status::Memory:
            // The native stopped building its result over the budget (the
            // partial result is already freed): always a stop, never a retry.
            update_heap_limit();
            if (frames_.size() >= kLegacyMaxDepth) {
                beyond_legacy_depth_ = true;
                return raise(fault, StopReason::Error, "callable recursion depth exceeded (memory budget)");
            }
            return raise(fault, StopReason::Internal, "memory budget exceeded");
    }
    R[in.a] = std::move(out);
    for (std::uint16_t k = 1; k < in.c; ++k) R[in.a + k] = Value();
    return true;
}

} // namespace nift::rt
