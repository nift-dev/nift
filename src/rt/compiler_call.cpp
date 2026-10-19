#include "rt/compiler_call.h"

#include "rt/alias.h"
#include "rt/atom.h"
#include "rt/builtin_bind.h"
#include "rt/native.h"
#include "rt/prepared_parse.h"

// Calls (user functions, natives), methods, member and index reads (CP11,
// CP12). Errors are reported at the failing expression (D-018c).

namespace nift::rt::detail {

bool UnitCompiler::call(const Node& node, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    if (callee.kind == NodeKind::Member) return method_call(node, dst);
    if (callee.kind != NodeKind::Ident) return fail(callee, "callee");
    if (!plain_postfix(node) || !no_trailing_comma(node)) return false;
    const std::string_view name = text(callee);
    if (scopes_.find(name)) return fail(callee, "call-binding");
    const auto fn = plan_.index.find(name);
    NativeInfo native{};
    const bool is_native = fn == plan_.index.end() && lookup_native(name, native);
    // CP14: a free built-in (`range`, `min`, `html_escape`, `type`, ...). A
    // user function or binding of that name is refused before this point
    // (reserved names, D-009#3).
    const natives::BuiltinSpec* builtin = fn == plan_.index.end() && !is_native ? bind_function(name) : nullptr;
    if (fn == plan_.index.end() && !is_native && !builtin) return fail(callee, "callable:" + std::string(name));
    const std::uint16_t arity =
        builtin ? 0 : is_native ? native.arity : ast_.at(plan_.functions[fn->second - 1]).count;
    // When `dst` is the newest register nothing above it is live, so the call
    // window (arguments, then the callee frame) starts at `dst` and the result
    // lands in place; otherwise it starts at the first free register.
    const bool in_place = dst + 1u == fb_->top();
    const std::uint16_t base = in_place ? dst : fb_->top();
    const NodeId* args = ast_.list(node);
    if (!builtin && node.count != arity) return fail(node, "static-error:arity");
    const std::size_t f = is_native || builtin ? 0 : fn->second - 1u;
    // Legacy evaluates a call with an argument its prepared parser rejects on
    // the textual path, arguments and callee included (Ast.cpp Kind::Call,
    // legacy_with_span): such a call is not loop-evaluator code, which the
    // context refusals (D-018) and the VM statistics' loop evidence follow.
    const bool saved_textual = textual_statement_;
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
    if (builtin) return builtin_call(node, *builtin, kNoNode, dst);
    if (!is_native) ++call_args_;
    for (std::uint32_t k = 0; k < node.count; ++k) {
        std::uint16_t reg = base;
        if (ast_.at(args[k]).kind == NodeKind::Spread) return fail(ast_.at(args[k]), "spread");
        // A place argument is a legacy location reference (review D6).
        if (!is_native && is_place_argument(ast_, args[k]) && f < plan_.escaping.size() &&
            k < plan_.escaping[f].size() && plan_.escaping[f][k])
            return fail(ast_.at(args[k]), "alias-argument");
        if (is_native && !kinds_within(args[k], kKindScalar, ast_.at(args[k]), "type:print")) return false;
        if (!(in_place && k == 0) && !temp(reg)) return false;
        if (!expr_to(args[k], reg)) return false;
    }
    if (!is_native) --call_args_;
    if (!is_native && loop_reachable()) {
        // Legacy's prepared call types a parameter from the value only, the
        // canonical one from the argument's text and value (P5): refused where
        // the callee can observe a difference (D-018).
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
        const std::size_t at =
            fb_->emit(OpCode::Call, base, static_cast<std::uint16_t>(f + 1), static_cast<std::uint16_t>(node.count));
        fb_->note(at, std::string(name));
    }
    std::uint16_t slot = base;
    if (node.count == 0 && !in_place && !temp(slot)) return false;  // the result slot exists even without arguments
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(in_place ? static_cast<std::uint16_t>(dst + 1) : base);
    return builder_ok();
}

// CP14 (W2, W4): an argument legacy may skip (lazy validation, Q11) must be
// one whose evaluation cannot be observed: a literal or a local read.
bool UnitCompiler::unobservable_argument(NodeId id) {
    const Node& node = ast_.at(id);
    if (node.kind == NodeKind::Ident) return scopes_.find(text(node)) != nullptr;
    Value value;
    bool is_constant = false;
    return constant_of(node, value, is_constant) && is_constant;
}

// CP14: a call of a pure built-in (rt/natives) over a fresh register window:
// the receiver (methods), then the arguments, evaluated once, left to right
// (D-018). The native's result replaces the window's first register.
bool UnitCompiler::builtin_call(const Node& node, const natives::BuiltinSpec& spec, NodeId receiver,
                                std::uint16_t dst) {
    const NodeId* args = ast_.list(node);
    // W1: legacy checks arity before or between argument evaluations (Q10).
    if (node.count < spec.min_args || (spec.max_args != natives::kVariadic && node.count > spec.max_args))
        return fail(node, "builtin-arity:" + std::string(spec.name));
    // W2 / W4: legacy validates lazily and may never evaluate later arguments.
    if (spec.may_fail)
        for (std::uint32_t k = 1; k < node.count; ++k)
            if (!unobservable_argument(args[k]))
                return fail(ast_.at(args[k]), spec.id == natives::BuiltinId::ObjGet
                                                  ? "builtin-default:get"
                                                  : "builtin-lazy-args:" + std::string(spec.name));
    const std::uint32_t window = node.count + (receiver != kNoNode ? 1u : 0u);
    if (window > 0x7fff) return fail(node, "limit:arguments");
    const std::uint16_t mark = fb_->top();
    std::uint16_t base = 0;
    bool first = true;
    const auto next = [&](NodeId value) {
        if (ast_.at(value).kind == NodeKind::Spread) return fail(ast_.at(value), "spread");
        std::uint16_t reg = 0;
        if (!temp(reg)) return false;
        if (first) { base = reg; first = false; }
        return expr_to(value, reg);
    };
    if (receiver != kNoNode && !next(receiver)) return false;
    for (std::uint32_t k = 0; k < node.count; ++k)
        if (!next(args[k])) return false;
    if (first && !temp(base)) return false;  // no receiver and no arguments: the result slot
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::CallNative, base,
                                     static_cast<std::uint16_t>(kFirstBuiltin + static_cast<std::uint16_t>(spec.id)),
                                     static_cast<std::uint16_t>(window));
    fb_->note(at, std::string(spec.name));
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(mark);
    return builder_ok();
}

bool UnitCompiler::method_call(const Node& node, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    if (!plain_postfix(node) || !plain_postfix(callee) || !no_trailing_comma(node)) return false;
    const std::string_view name = text(ast_.at(callee.b));
    // `length()`/`size()` keep the CP11/CP12 Length opcode (the fast path,
    // with its own kind checks and the context:object-length refusal, Q3).
    const bool length_opcode =
        (name == "length" || name == "size") && node.count == 0;  // rt-lint: allow-string-compare compile time
    if (!length_opcode) {
        // CP14: every other method is a built-in native or refused.
        const std::uint8_t k = kinds(callee.a);
        const natives::BuiltinSpec* spec = nullptr;
        switch (bind_method(k, name, spec)) {
            case MethodBinding::Bound: case MethodBinding::Dead: break;
            case MethodBinding::NotBuiltin: return fail(callee, "method:" + std::string(name));
            case MethodBinding::Receiver:
                // Probe mode only: Unknown defers to the underlying refusal.
                if (!unknown_defers(k) || bind_method(0, name, spec) != MethodBinding::Dead)
                    return fail(node, "builtin-receiver:" + std::string(name));
                break;
        }
        // W6 (Q14): legacy composes a chain of presentation methods over the
        // original value outside loop code; each call serializes its receiver
        // in loop code. Refused in every context (one meaning, D-018).
        const Node& receiver = ast_.at(callee.a);
        if (presentation_builtin(*spec) && receiver.kind == NodeKind::Call &&
            ast_.at(receiver.a).kind == NodeKind::Member &&
            natives::find_builtin(natives::Receiver::Any, text(ast_.at(ast_.at(receiver.a).b))))
            return fail(node, "builtin-presentation-chain");
        return builtin_call(node, *spec, callee.a, dst);
    }
    const bool size = name == "size";  // rt-lint: allow-string-compare compile time
    if (!kinds_within(callee.a, size ? kKindAggregate : static_cast<std::uint8_t>(kKindAggregate | kKindString), node,
                      "type:length"))
        return false;
    // Legacy's prepared `length()` counts an object's members; the canonical
    // one rejects an object (D-018).
    if (!size && loop_reachable() && (kinds(callee.a) & (kKindObject | kKindUnknown)))
        return fail(node, "context:object-length");
    const std::uint16_t mark = fb_->top();
    std::uint16_t receiver = 0;
    if (!reg_any(callee.a, receiver)) return false;
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::Length, dst, receiver, size ? 1 : 0);
    fb_->note(at, std::string(text(node)));
    fb_->release_to(mark);
    return true;
}

// Member and index reads can fail at run time (missing member or key, index
// out of range). Their legacy diagnostics are reproduced where the shape is
// known: not inside user-call arguments (legacy resolves such paths through
// its location machinery, whose messages quote the raw argument text) nor
// inside another index's brackets; there they are refused.
bool UnitCompiler::member(const Node& node, std::uint16_t dst) {
    if (!plain_postfix(node)) return false;
    if (call_args_ > 0) return fail(node, "path-in-call-argument");
    if (!kinds_within(node.a, kKindObject, node, "type:member")) return false;
    const std::uint16_t mark = fb_->top();
    std::uint16_t object = 0;
    if (!reg_any(node.a, object)) return false;
    const Atom key = atom(text(ast_.at(node.b)));
    if (key.id > 0xffff) return fail(node, "limit:atoms");
    fb_->set_offset(site(node));
    fb_->emit(OpCode::GetMember, dst, object, static_cast<std::uint16_t>(key.id));
    fb_->release_to(mark);
    return true;
}

bool UnitCompiler::index(const Node& node, std::uint16_t dst) {
    if (!plain_postfix(node)) return false;
    if (call_args_ > 0) return fail(node, "path-in-call-argument");
    if (index_depth_ > 0) return fail(node, "path-in-index");
    const std::uint8_t object_kinds = kinds(node.a), key_kinds = kinds(node.b);
    const bool array = (object_kinds & ~kKindArray) == 0 && (key_kinds & ~kKindNumber) == 0;
    const bool object = (object_kinds & ~kKindObject) == 0 && (key_kinds & ~kKindString) == 0;
    if (!array && !object && !unknown_defers(object_kinds | key_kinds))  // Unknown is neither (review D1)
        return fail(node, "type:index");
    const std::uint16_t mark = fb_->top();
    std::uint16_t object_reg = 0, key = 0;
    if (!reg_any(node.a, object_reg)) return false;
    ++index_depth_;
    const bool key_ok = expr_any(node.b, key);
    --index_depth_;
    if (!key_ok) return false;
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::GetIndex, dst, object_reg, key);
    fb_->note(at, std::string(text(node)));
    fb_->release_to(mark);
    return true;
}

} // namespace nift::rt::detail
