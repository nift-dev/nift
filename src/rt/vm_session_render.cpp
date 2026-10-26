#include "rt/vm_session_render.h"
#include "rt/heap.h"
#include "rt/render.h"
// rt-lint: cold-file REPL presentation; no Error mutation or legacy execution
namespace nift::rt {
bool Vm::op_session_render(const Instr& in, Value* R, VmResult& result, Fault& fault, const Proto* proto) {
    const auto& value = R[in.a];
    const auto* note = proto->note(static_cast<std::size_t>(&in - proto->code.data()));
    const auto source = note ? note->text : std::string();
    if (value.is(Kind::Array) || value.is(Kind::Object)) {
        if (in.b) return true;  // legacy standalone calls are fire-and-forget for collections
        return raise(fault, StopReason::Error, "cannot render JSON " +
            std::string(value.is(Kind::Array) ? "array $[" : "object $[") + source +
            (value.is(Kind::Array) ? "]; select an element first" : "]; select a member first"));
    }
    if (value.is(Kind::Function)) {
        result.statement_text += callable_marker(*value.heap_as<FunctionObject>()); return true;
    }
    if (value.is(Kind::Error))
        return raise(fault, StopReason::Error, "Error values cannot be rendered as text in $[" + source + "]");
    if (value.is(Kind::Bytes)) return raise(fault, StopReason::Error,
        "bytes values cannot be rendered as text in $[" + source + "]; decode as UTF-8 first");
    if (!append_rendered(value, result.statement_text)) return internal(fault, "session statement rendering");
    return true;
}
} // namespace nift::rt
