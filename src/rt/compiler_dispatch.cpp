// B4 (docs/runtime-2/checkpoints/b4-dispatch.md): a built-in method call on a
// receiver whose kinds are a closed union of plain families (or of map, set
// and stack for their read-only natives) that bind_method does not bind. The
// admission table (rt/op_admission.h) decides; the call compiles to one
// flagged CallNative whose native the VM selects from the receiver's run-time
// family, or legacy's ordinary error for a family without that method.
#include "rt/compiler_dispatch.h"

#include "rt/builtin_bind.h"
#include "rt/op_admission.h"

// rt-lint: cold-file compile-time admission of dynamic method dispatch

namespace nift::rt::detail {

// -1: not a Dynamic case (the caller keeps its refusal); 0: refused here (an
// argument rule); 1: compiled.
int UnitCompiler::dynamic_method_call(const Node& node, NodeId receiver, std::string_view name, std::uint16_t dst) {
    const MethodAdmission admitted = admit_dynamic_method(kinds(receiver), name, node.count);
    if (admitted.admission != Admission::Dynamic) return -1;
    // B5: a struct instance among the receiver kinds fails only if no struct
    // of the unit declares a method of that name (legacy would call it).
    // Legacy's text for a struct instance depends on the receiver's syntax: a
    // name gives "struct has no method: <name>", an element or member read the
    // tagged value's "unknown value or malformed expression" (probed, b5-refsem.md
    // section 8 B); the table has the second only, so a named struct receiver
    // keeps its refusal.
    if (kinds(receiver) & kKindStruct) {
        if (ast_.at(receiver).kind == NodeKind::Ident) return -1;
        for (const StructInfo& s : plan_.structs)
            if (s.method(name)) return -1;
    }
    for (const DispatchCandidate& branch : admitted.branches)
        if (branch.spec && !builtin_arguments_ok(node, *branch.spec, receiver)) return 0;
    if (admitted.fails()) {
        // Legacy reports a family without the method before evaluating any
        // argument: arguments must be unobservable (W2/W4's rule), and a
        // malformed-expression report quotes the call's text on one line.
        const NodeId* args = ast_.list(node);
        for (std::uint32_t k = 0; k < node.count; ++k)
            if (!unobservable_argument(args[k])) return fail(node, "builtin-receiver:" + std::string(name)) ? 1 : 0;
        for (const DispatchCandidate& branch : admitted.branches)
            if (branch.failure == DispatchFailure::Malformed && text(node).find('\n') != std::string_view::npos)
                return fail(node, "builtin-receiver:" + std::string(name)) ? 1 : 0;
    }
#if defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 12
    // T2: the tagged sites lose the run-time kind check (compiled as static calls).
    const bool flagged = (kinds(receiver) & kKindTagged) == 0;
#else
    const bool flagged = true;
#endif
    return builtin_call(node, *admitted.spec, receiver, dst, flagged) ? 1 : 0;
}

} // namespace nift::rt::detail
