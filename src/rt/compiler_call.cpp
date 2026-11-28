#include "rt/compiler_call.h"
#include "rt/legacy_names.h"
#include "rt/compiler_handles.h"
#include <algorithm>
#include "rt/alias.h"
#include "rt/atom.h"
#include "rt/builtin_bind.h"
#include "rt/compiler_native_args.h"
#include "rt/native.h"
#include "rt/module_data.h"
#include "rt/module_link.h"
#include "rt/op_admission.h"
#include "rt/prepared_parse.h"
#include "rt/source_scan.h"
#include "rt/vm_mutants.h"
namespace nift::rt::detail {
bool UnitCompiler::call(const Node& node, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    if (callee.kind == NodeKind::Member) return method_call(node, dst);
    if (callee.kind != NodeKind::Ident) return fail(callee, "callee");
    if (!plain_postfix(node) || !no_trailing_comma(node)) return false;
    const std::string_view name = text(callee);
    const Binding* bound = find_binding(name);
    if (bound && name == "same" && bound->decl_node != kNoNode &&  // rt-lint: allow-string-compare compile time
        ast_.at(bound->decl_node).kind == NodeKind::Ident && bound->decl_kinds == kKindBool &&
        !session_ && !in_function_ && !in_lambda_) bound = nullptr;
#if !(defined(NIFT_ARGSCMD_MUTANT) && NIFT_ARGSCMD_MUTANT == 3)  // test-only control (args_cmd/mutants.sh)
    const bool invocation_cmd = bound && (bound->invocation || invocation_shadows_.count(bound->decl_node));
    if (invocation_cmd && name == "cmd") bound = nullptr;  // rt-lint: allow-string-compare compile time
#endif
    if (bound && (!(session_ && plan_.index.count(name)) ||
        session_->phased_functions.count(std::string(name)))) return call_value(node, node.a, false, dst);
    if (name == "error") return error_new(node, dst);  // rt-lint: allow-string-compare reserved Error constructor
    if (const int r = struct_callee(node, name, dst); r >= 0) return r == 1;  // CP12b structs
    NativeInfo native{}; const auto fn = plan_.index.find(name);
    if (fn != plan_.index.end() && indexed_return(fn->second))
        return indexed_call_value(node, fn->second, dst);
    if (fn == plan_.index.end() && name == "run")  // rt-lint: allow-string-compare compile time
        if (const int result = process_call(node, node, kNoNode, dst); result >= 0) return result == 1;
    if (fn == plan_.index.end() && name == "inject" && package_json_source(node))  // rt-lint: allow-string-compare
        return builtin_call(node, natives::builtin_spec(natives::BuiltinId::ProcessInjectJson), kNoNode, dst);
    const bool is_native = fn == plan_.index.end() && lookup_native(name, native);
    // CP14: a free built-in; a user function/binding of that name is reserved (D-009#3).
    const natives::BuiltinSpec* builtin = fn == plan_.index.end() && !is_native ? bind_function(name) : nullptr;
    if (fn == plan_.index.end() && !is_native && !builtin) return thread_call(node, name, dst);  // or refused
    const std::uint16_t arity =
        builtin ? 0 : is_native ? native.arity : ast_.at(plan_.functions[fn->second - 1]).count;
    const bool in_place = dst + 1u == fb_->top();
    const std::uint16_t base = in_place ? dst : fb_->top();
    const NodeId* args = ast_.list(node);
    // CP25: user-function arity errors follow operand evaluation. The
    // prepared evaluator can retry operands, so only admit operands whose
    // repeated evaluation has no observable effect.
    bool arity_error = !builtin && !is_native && node.count != arity && !(fb_->flags() & kInstrReturnCall);
    if (arity_error && loop_reachable()) {
        arity_error = node.count > arity && ast_.at(args[0]).kind == NodeKind::Ident &&
            std::all_of(args, args + node.count, [&](NodeId id) {
            const Node& arg = ast_.at(id);
            const auto spelling = text(arg);
            if (arg.kind == NodeKind::Number)
                return kinds(id) == kKindInt && spelling.size() <= 9 &&
                    spelling.find_first_not_of("0123456789") == std::string_view::npos;
            if (arg.kind != NodeKind::Ident || find_binding(spelling) || reserved_name(spelling) ||
                session_ || in_function_ || in_lambda_ || (module_names_ && !module_names_->empty())) return false;
            const auto function = plan_.index.find(spelling);
            return function != plan_.index.end() &&
                ast_.at(plan_.functions[function->second - 1]).begin < node.begin;
        });
    }
    // An empty textual print call reaches the legacy output diagnostic.
    if (is_native && node.count == 0 && name == "print" &&  // rt-lint: allow-string-compare compile time
        !session_ && !in_function_ && !in_lambda_ && !loop_reachable() && !(fb_->flags() & kInstrReturnCall))
        return legacy_fatal(node, "print: expected one value", dst);
    if (!builtin && node.count != arity && !arity_error) return fail(node, "static-error:arity");
    const std::size_t f = is_native || builtin ? 0 : fn->second - 1u;
    // Legacy evaluates a call with an argument its prepared parser rejects on
    // the textual path, arguments and callee included (Ast.cpp Kind::Call,
    // legacy_with_span): such a call is not loop-evaluator code, which the
    // context refusals (D-018) and the VM statistics' loop evidence follow.
    const bool saved_textual = textual_statement_;
    const auto saved_flags = fb_->flags();
    if (loop_region())
        for (std::uint32_t k = 0; k < node.count; ++k) {
            const Node& arg = ast_.at(args[k]);
            const std::uint32_t b = prepared_begin(arg);
            if (!legacy_prepared_parse_accepts(ast_.source().substr(b, prepared_end(arg) - b))) {
                textual_statement_ = true;
                update_flags();
                break;
            }
        }
    struct Restore {
        UnitCompiler& c;
        bool saved;
        ~Restore() { c.textual_statement_ = saved; c.update_flags(); }
    } restore{*this, saved_textual};
    if (const int r = builtin ? coro_call(node, name, builtin, dst) : -1; r >= 0) return r == 1;  // CP22
    if (builtin) return builtin_call(node, *builtin, kNoNode, dst);
    if (!operand_window(base, node.count)) return false; if (!is_native) ++call_args_;
    const NodeId saved_path = path_arg_, saved_chain = path_chain_;
    const Node* const saved_print = print_arg_;
    const Node* const saved_print_span = call_arg_span_;
    const bool saved_path_allow = path_allow_nested_;
    const bool package_scope = !is_native && f < plan_.functions.size() && plan_.linked &&
        plan_.linked->locate(ast_.at(plan_.functions[f]).begin).module > 0;
    path_allow_nested_ = package_scope;
    for (std::uint32_t k = 0; k < node.count; ++k) {
        // The argument itself may be a scalar path (argument_path); a path
        // anywhere else in it, or in an index key, is not.
        const NodeKind arg_kind = ast_.at(args[k]).kind;
        const bool path_kind = arg_kind == NodeKind::Ident || arg_kind == NodeKind::Member ||
                               arg_kind == NodeKind::Index;
        const bool escapes = !is_native && f < plan_.escaping.size() && k < plan_.escaping[f].size() &&
                             plan_.escaping[f][k];
        // A non-escaping argument may be read as a value only in imported
        // package code; entry scripts keep the conservative location refusal.
        path_arg_ = (escapes || !package_scope) && index_depth_ == 0 && path_kind && bare_argument(node, k)
                        ? args[k] : kNoNode;
        path_chain_ = kNoNode;
        print_arg_ = is_native ? &ast_.at(args[k]) : nullptr;
        call_arg_span_ = &node;
        if (ast_.at(args[k]).kind == NodeKind::Spread) return fail(ast_.at(args[k]), "spread");
        // A place argument is a legacy location reference (review D6).
        if (!is_native && !arity_error && is_place_argument(ast_, args[k]) && f < plan_.escaping.size() &&
            k < plan_.escaping[f].size() && plan_.escaping[f][k])
            return fail(ast_.at(args[k]), "alias-argument");
        if (is_native && !print_argument_ok(args[k], ast_.at(args[k]))) return false;
        if (!window_value(args[k], static_cast<std::uint16_t>(base + k))) return false;
    }
    path_allow_nested_ = saved_path_allow;
    path_arg_ = saved_path; path_chain_ = saved_chain; print_arg_ = saved_print; call_arg_span_ = saved_print_span;
    if (!is_native) --call_args_;
    if (arity_error) {
        const bool ok = legacy_fatal(node, "callable argument count mismatch: " + std::string(name), base);
        if (dst != base) fb_->emit(OpCode::Move, dst, base);
        fb_->release_to(in_place ? static_cast<std::uint16_t>(dst + 1) : base);
        return ok && builder_ok();
    }
    if (!is_native && (loop_region() || loop_type_error_ambiguous())) {
        // Legacy's prepared call types a parameter from the value only, the
        // canonical one from the argument's text and value (P5): refused where
        // the callee can observe a difference (D-018). A struct method outside
        // a loop runs canonically, so it keeps the canonical check.
        for (std::uint32_t k = 0; k < node.count; ++k)
            if (observable_[f][k] &&
                type_mismatch_possible(text_type_at(args[k]), kinds(args[k]), scope_index()))
                return fail(ast_.at(args[k]), "context:parameter-type");
    }
    if (!is_native && !pass_param_types(f, node.count, args, base)) return false;
    fb_->set_offset(site(node));
    if (is_native) {
        fb_->emit(OpCode::CallNative, base, static_cast<std::uint16_t>(native.id),
                  static_cast<std::uint16_t>(node.count));
    } else {
        const std::size_t at =  // CP22: Spawn for an async function
            fb_->emit(call_opcode(f), base, static_cast<std::uint16_t>(f + 1), static_cast<std::uint16_t>(node.count));
#if defined(NIFT_SESSION_ADMISSION_MUTANT) && NIFT_SESSION_ADMISSION_MUTANT == 24
        fb_->note(at, std::string(name));
#else
        fb_->note(at, std::string(name), false, false, session_ && (saved_flags & kInstrReturnCall));
#endif
    }
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(in_place ? static_cast<std::uint16_t>(dst + 1) : base);
    return builder_ok();
}
bool UnitCompiler::method_call(const Node& node, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    bool is_this = false;  // CP12b: a struct method
    if (const int s = struct_of_receiver(callee.a, is_this); s >= 0) return struct_method(node, s, dst);
    if (!plain_postfix(node) || !plain_postfix(callee) || !no_trailing_comma(node)) return false;
    if (const int result = process_pipeline(node, dst); result >= 0) return result == 1;
    if (const int result = imported_data_call(node, dst); result >= 0) return result == 1;
    const std::string_view name = text(ast_.at(callee.b));
    if (int r = keyed_method_call(node, dst); r >= 0) return r == 1;  // D-063 map/set/stack
    if (int r = collection_method_call(node, dst); r >= 0) return r == 1;
    if (int r = future_method(node, dst); r >= 0) return r == 1;  // CP22
    if (error_method_call(node, dst, name, callee.a)) return true;
    // `length()`/`size()` keep the CP11/CP12 Length opcode (the fast path,
    // with its own kind checks and the context:object-length refusal, Q3).
    const bool length_opcode =
        (name == "length" || name == "size") && node.count == 0;  // rt-lint: allow-string-compare compile time
    if (!length_opcode) {
        if (session_) {
            const int result = session_native_call(node, callee.a, name, dst);
            if (result >= 0) return result == 1;
        }
        bool package_matched = false;
        if (!package_method_call(node, callee.a, name, dst, package_matched)) return false;
        if (package_matched) return true;
        const KindSet k = kinds(callee.a);
        // A proved fractional number in loop code keeps the prepared/canonical
        // difference refusal. An unproved or string-capable receiver is
        // legacy's run-time conversion (the native validates the reached value).
        if (name == "to_int" && k != 0 && (k & kKindFrac) &&  // rt-lint: allow-string-compare B4
            loop_reachable())
            return fail(node, "context:number-to-int");
        const natives::BuiltinSpec* spec = nullptr;
        switch (bind_method(k, name, spec)) {
            case MethodBinding::Bound: case MethodBinding::Dead: break;
            case MethodBinding::NotBuiltin: {
                const KindSet receiver_kinds = kinds(callee.a);
                if (receiver_kinds == kKindObject || (receiver_kinds & kKindUnknown))
                    return call_value(node, node.a, true, dst);
                return fail(callee, "method:" + std::string(name));
            }
            case MethodBinding::Receiver:
                // CP19 x CP23: CP23's mutex `set` made `set` a builtin name;
                // a session keeps its specific refusal for a name legacy
                // treats as a special method (as before the integration).
                if (session_ && legacy_special_method(name) && (k & (kKindObject | kKindUnknown)))
                    return fail(node, "session:legacy-method-precedence");
                if (const int r = dynamic_method_call(node, callee.a, name, dst); r >= 0) return r == 1;  // B4
                // Probe mode only: Unknown defers to the underlying refusal.
                if (!unknown_defers(k) || bind_method(0, name, spec) != MethodBinding::Dead)
                    return fail(node, "builtin-receiver:" + std::string(name));
                break;
        }
        const Node& receiver = ast_.at(callee.a);
        using natives::BuiltinId;
        const bool split_arity = spec->id == BuiltinId::StrSplit && node.count != 1;
        const bool last_arity = spec->id == BuiltinId::ArrLast && node.count != 0;
        if (!in_function_ && !session_ && !(fb_->flags() & kInstrReturnCall) && (split_arity || last_arity)) {
            const auto name = text(receiver);
            const Binding* binding = receiver.kind == NodeKind::Ident ? find_binding(name) : nullptr;
            if (split_arity && (!plain_readonly_arity_string(ast_, receiver, binding) ||
                (binding && (assigned_.count(name) || std::any_of(assigned_fn_.begin(), assigned_fn_.end(),
                    [&](const auto& names) { return names.count(name); })))))
                return fail(node, "builtin-arity:split");
            if (!expr_to(callee.a, dst)) return false;
            return legacy_fatal(node, split_arity ? "split: expected one delimiter"
                                                : "last: expected no arguments", dst);
        }
        // W6 (Q14): legacy composes a chain of presentation methods over the
        // original value outside loop code; each call serializes its receiver
        // in loop code. Refused in every context (one meaning, D-018).
        if (presentation_builtin(*spec) && receiver.kind == NodeKind::Call &&
            ast_.at(receiver.a).kind == NodeKind::Member &&
            natives::find_builtin(natives::Receiver::Any, text(ast_.at(ast_.at(receiver.a).b))))
            return fail(node, "builtin-presentation-chain");
        if (presentation_builtin(*spec))  // B3: a struct's own presentation method (compiler_structs.cpp)
            if (const int dispatched = presentation_dispatch(node, k, name, dst); dispatched >= 0) return dispatched;
        return builtin_call(node, *spec, callee.a, dst);
    }
    return length_call(node, callee.a, name == "size", dst);  // rt-lint: allow-string-compare compile time
}
// A member or index path inside user-call arguments (coordinator ruling,
// VM subset, records20k): legacy binds the parameter to the path as a
// location. For a scalar value that is observably a copy (writes and
// rebinding of the root do not reach the parameter, and the binding type is
// the value's), but an aggregate is shared, so only an argument that is
// itself a path whose kinds are certainly scalar is admitted (not empty, no
// aggregate, not Unknown); every other path in call arguments is refused.
// Member and index reads can fail at run time (missing member or key, index
// out of range). Their legacy diagnostics are reproduced where the shape is
// known: outside user-call arguments (except an admitted path argument,
// above) and outside another index's brackets; elsewhere they are refused.
bool UnitCompiler::member(const Node& node, std::uint16_t dst) {
    bool is_this = false;  // CP12b: a struct field
    if (const int s = struct_of_receiver(node.a, is_this); s >= 0) return struct_member(node, s, is_this, dst);
    if (!plain_postfix(node)) return false;
    std::string location;
    if (!argument_path(node, location)) return false;
    if (error_member_access(node, dst, node.a)) return true;
    const Node& receiver = ast_.at(node.a);
    const Binding* parameter = in_lambda_ && receiver.kind == NodeKind::Ident ? find_binding(text(receiver)) : nullptr;
    const bool scalar_parameter_without_members = !session_ && return_read_offset_ == kNoNode &&
        parameter && parameter->decl_node != kNoNode &&
        ast_.at(parameter->decl_node).kind == NodeKind::Param && kinds(node.a) != 0 &&
        ((kinds(node.a) & ~kKindNumber) == 0 || kinds(node.a) == kKindNull);
    const bool null_script_member = print_arg_ == &node && kinds(node.a) == kKindNull &&
        receiver.kind == NodeKind::Ident && !in_function_ && !loop_reachable() && !session_ &&
        !plan_.coroutines && (!module_names_ || module_names_->empty());
    if (scalar_parameter_without_members || null_script_member)
        return expr_to(node.a, dst) &&
               legacy_fatal(node, "value has no member: " + std::string(text(ast_.at(node.b))), dst);
    // A receiver whose kind the compiler cannot prove (or has no proof for)
    // is checked at run time: legacy's missing-member error is reached.
    const KindSet member_kinds = kinds(node.a);
    // A lambda's `this` is resolved through the method receiver; a dynamic
    // read there is still refused (the lambda body may not capture it).
    const bool lambda_this = in_lambda_ && ast_.at(node.a).kind == NodeKind::Ident &&
                             text(ast_.at(node.a)) == "this";  // rt-lint: allow-string-compare compile time
    const bool dynamic_member = !lambda_this &&
        (member_kinds == 0 || (member_kinds & (kKindUnknown | kKindObject)) != 0);
    if (!dynamic_member && !kinds_within(node.a, kKindObject, node, "type:member")) return false;
    const std::uint16_t mark = fb_->top();
    std::uint16_t object = 0;
    const Node& chain = chain_enter(node);
    const std::string_view saved_note = global_note_;
    const bool note_set = global_note_.empty() && ast_.at(node.a).kind != NodeKind::Array;
    if (note_set) global_note_ = text(node);
    const bool receiver_ok = reg_any(node.a, object);
    if (note_set) global_note_ = saved_note;
    chain_next_ = nullptr;
    if (!receiver_ok) return false;
    const NodeKind r = ast_.at(node.a).kind;  // CP12b: a call result or literal: textual wording (L43)
    const bool value_receiver = r != NodeKind::Ident && r != NodeKind::Member && r != NodeKind::Index;
    if (location.empty() && (value_receiver || receiver_text_wording(node, chain)))
        location = std::string(text(ast_.at(node.a)));
    const Atom key = atom(text(ast_.at(node.b)));
    if (key.id > 0xffff) return fail(node, "limit:atoms");
    const bool return_error = return_error_attribution();
    fb_->set_offset(return_error && deferred_.return_statement != 0 ? deferred_.return_statement : site(node));
    const auto saved_flags = fb_->flags();
    if (return_error) fb_->set_flags(static_cast<std::uint8_t>(saved_flags | kInstrReturnError));
    const std::size_t at = fb_->emit(OpCode::GetMember, dst, object, static_cast<std::uint16_t>(key.id));
    fb_->set_flags(saved_flags);
    if (!location.empty()) fb_->note(at, std::move(location));
    fb_->release_to(mark);
    return true;
}
} // namespace nift::rt::detail
