// B4 (docs/runtime-2/checkpoints/b4-dispatch.md): a built-in method call on a
// receiver whose kinds are a closed union of plain families (or of map, set
// and stack for their read-only natives) that bind_method does not bind. The
// admission table (rt/op_admission.h) decides; the call compiles to one
// flagged CallNative whose native the VM selects from the receiver's run-time
// family, or legacy's ordinary error for a family without that method.
#include "rt/compiler_dispatch.h"

#include "rt/builtin_bind.h"
#include "rt/module_data.h"
#include "rt/op_admission.h"
#include "rt/module_link.h"
#include "rt/vm_ops.h"

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

// The immediately reached right-hand call reads the same local parameter
// that the built-in type test just checked. No intermediate code can rebind it.
int UnitCompiler::guarded_string_contains(const Node& node, std::uint16_t dst) {
    if (session_ || in_lambda_ || !in_function_ || plan_.coroutines ||
        !imported_module_data(plan_, node)) return -1;
    const auto& check = ast_.at(node.a);
    const auto& call = ast_.at(node.b);
    if (check.kind != NodeKind::Binary || check.op != Op::Eq || call.kind != NodeKind::Call || call.count != 1)
        return -1;
    const auto& test = ast_.at(check.a);
    const auto& label = ast_.at(check.b);
    const auto& method = ast_.at(call.a);
    if (test.kind != NodeKind::Call || test.count != 1 || label.kind != NodeKind::String ||
        text(label) != R"("string")" || method.kind != NodeKind::Member || text(ast_.at(method.b)) != "contains")
        return -1;
    const auto& type_name = ast_.at(test.a);
    if (type_name.kind != NodeKind::Ident || text(type_name) != "type" ||
        find_binding("type") || plan_.index.count("type") || plan_.struct_index.count("type")) return -1;
    const int s = current_struct();
    if (s >= 0 && plan_.structs[static_cast<std::size_t>(s)].method("type")) return -1;
    const auto tested = ast_.list(test)[0];
    const auto& receiver = ast_.at(method.a);
    const auto& input = ast_.at(tested);
    if (receiver.kind != NodeKind::Ident || input.kind != NodeKind::Ident || text(receiver) != text(input)) return -1;
    const auto* binding = find_binding(text(input));
    if (!binding || binding->cell || binding->decl_node == kNoNode ||
        ast_.at(binding->decl_node).kind != NodeKind::Param) return -1;
    const auto possible = kinds(method.a);
    if (!(possible & kKindString) || (possible & ~(kKindAll | kKindTagged))) return -1;
    if (!plain_postfix(call) || !plain_postfix(method) || !no_trailing_comma(call)) return 0;
    const auto& spec = natives::builtin_spec(natives::BuiltinId::StrContains);
    return builtin_call(call, spec, method.a, dst) ? 1 : 0;
}

bool UnitCompiler::length_call(const Node& node, NodeId receiver_node, bool size, std::uint16_t dst) {
    // A direct stream read can return null at EOF. As a script print
    // operand the length operator can use its own error position. Other
    // contexts can require statement or function-frame attribution.
    bool stream_line_length = false;
    const Node& length_receiver = ast_.at(receiver_node);
    if (!size && print_arg_ == &node && !in_function_ && !session_ && !(fb_->flags() & kInstrReturnCall) &&
        kinds(receiver_node) == (kKindString | kKindNull) && length_receiver.kind == NodeKind::Call) {
        const Node& read = ast_.at(length_receiver.a);
        if (read.kind == NodeKind::Member) {
            const natives::BuiltinSpec* spec = nullptr;
            stream_line_length = bind_method(kinds(read.a), text(ast_.at(read.b)), spec) == MethodBinding::Bound &&
                                 spec->id == natives::BuiltinId::StreamReadLine;
        }
    }
    // An unproven receiver is checked at run time (legacy's reached error or
    // the container length); a proved non-container kind keeps the refusal.
    const KindSet length_kinds = kinds(receiver_node);
    const bool package_length = plan_.linked && plan_.linked->locate(node.begin).module > 0;
    const Binding* local_length = ast_.at(receiver_node).kind == NodeKind::Ident
                                      ? find_binding(text(ast_.at(receiver_node))) : nullptr;
    const bool local_value_length = local_length && !local_length->global && !local_length->cell &&
        local_length->decl_node != kNoNode && ast_.at(local_length->decl_node).kind == NodeKind::Ident;
    const NodeKind length_receiver_kind = ast_.at(receiver_node).kind;
    const bool non_ident_length = length_receiver_kind == NodeKind::Call ||
        length_receiver_kind == NodeKind::Member || length_receiver_kind == NodeKind::Index;
    const bool unproved_length = length_kinds == 0 || (length_kinds & (kKindUnknown | kKindNull)) != 0;
    const bool dynamic_length = package_length || ((local_value_length || non_ident_length) && unproved_length);
    if (!stream_line_length && length_kinds != kKindBytes && !dynamic_length &&
        !kinds_within(receiver_node, size ? kKindAggregate : static_cast<KindSet>(kKindAggregate | kKindString), node,
                      "type:length"))
        return false;
    // Legacy's prepared `length()` counts an object's members; the canonical
    // one rejects an object (D-018).
    if (!size && (kinds(receiver_node) & kKindObject) && loop_type_error_ambiguous())
        return fail(node, "context:object-length");
    const std::uint16_t mark = fb_->top();
    std::uint16_t receiver = 0;
    if (!reg_any(receiver_node, receiver)) return false;
    const bool return_error = return_error_attribution();
    fb_->set_offset(return_error && deferred_.return_statement != 0 ? deferred_.return_statement : site(node));
    const auto mode = stream_line_length ? LengthMode::StreamLine : size ? LengthMode::Size : LengthMode::Length;
    const auto saved_flags = fb_->flags();
    if (return_error) fb_->set_flags(static_cast<std::uint8_t>(saved_flags | kInstrReturnError));
    const std::size_t at = fb_->emit(OpCode::Length, dst, receiver, static_cast<std::uint16_t>(mode));
    fb_->set_flags(saved_flags);
    fb_->note(at, std::string(text(node)));
    fb_->release_to(mark);
    return true;
}

} // namespace nift::rt::detail
