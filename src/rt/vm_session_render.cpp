#include "rt/vm_session_render.h"
#include "rt/heap.h"
#include "rt/render.h"
#include "rt/eval_result.h"
#include "rt/shared_atomic.h"
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
    // B4 (b4-dispatch.md 6): no internal tag for a function; a struct or a
    // map/set/stack shows its compact form; an atomic its current value.
    if (value.is(Kind::Function)) return raise(fault, StopReason::Error, "opaque runtime value is not serializable");
    if (std::string shown; session_presented(value, shown)) { result.statement_text += shown; return true; }
    if (value.is(Kind::Handle) && value.handle_kind() == HandleKind::Atomic) {
        SharedOpError failure;
        Value loaded;
        if (!shared_atomic_load(value, loaded, failure)) return raise(fault, StopReason::Error, failure.message);
        return append_rendered(loaded, result.statement_text) || internal(fault, "session statement rendering");
    }
    if (value.is(Kind::Error))
        return raise(fault, StopReason::Error, "Error values cannot be rendered as text in $[" + source + "]");
    if (value.is(Kind::Bytes)) return raise(fault, StopReason::Error,
        "bytes values cannot be rendered as text in $[" + source + "]; decode as UTF-8 first");
#if !(defined(NIFT_SAFETY_MUTANT) && NIFT_SAFETY_MUTANT == 5)
    // B0 (b0-safety.md): a handle, a future, or a map/set/stack from an earlier
    // statement as the shown tail value: an ordinary statement error (the
    // compiler refuses only the collections it can see, session:collection-result).
    if (value.is(Kind::Handle) || value.is(Kind::Future))
        return raise(fault, StopReason::Error, "opaque runtime values cannot be rendered as text in $[" + source + "]");
    if (value.is(Kind::Collection))
        return raise(fault, StopReason::Error,
                     "map, set and stack values cannot be rendered as text in $[" + source + "]");
#endif
    // Struct instances are refused before the statement (session:collection-result).
    if (!append_rendered(value, result.statement_text)) return internal(fault, "session statement rendering");
    return true;
}
} // namespace nift::rt
