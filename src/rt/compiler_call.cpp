#include "rt/compiler_call.h"
#include <algorithm>
#include "rt/alias.h"
#include "rt/atom.h"
#include "rt/builtin_bind.h"
#include "rt/native.h"
#include "rt/prepared_parse.h"
#include "rt/source_scan.h"
#include "rt/vm_mutants.h"
// Calls (user functions, natives), methods, member and index reads (CP11,
// CP12). Errors are reported at the failing expression (D-018c).
namespace nift::rt::detail {
bool UnitCompiler::call(const Node& node, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    if (callee.kind == NodeKind::Member) return method_call(node, dst);
    if (callee.kind != NodeKind::Ident) return fail(callee, "callee");
    if (!plain_postfix(node) || !no_trailing_comma(node)) return false;
    const std::string_view name = text(callee);
    if (const Binding* binding = find_binding(name); binding != nullptr) return call_value(node, node.a, false, dst);
    if (const int r = struct_callee(node, name, dst); r >= 0) return r == 1;  // CP12b structs
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
    const NodeId saved_path = path_arg_, saved_chain = path_chain_;
    const Node* const saved_print = print_arg_;
    for (std::uint32_t k = 0; k < node.count; ++k) {
        // The argument itself may be a scalar path (argument_path); a path
        // anywhere else in it, or in an index key, is not.
        path_arg_ = is_native || index_depth_ > 0 || !bare_argument(node, k) ? kNoNode : args[k];
        path_chain_ = kNoNode;
        print_arg_ = is_native ? &ast_.at(args[k]) : nullptr;
        std::uint16_t reg = base;
        if (ast_.at(args[k]).kind == NodeKind::Spread) return fail(ast_.at(args[k]), "spread");
        // A place argument is a legacy location reference (review D6).
        if (!is_native && is_place_argument(ast_, args[k]) && f < plan_.escaping.size() &&
            k < plan_.escaping[f].size() && plan_.escaping[f][k])
            return fail(ast_.at(args[k]), "alias-argument");
        if (is_native && !print_argument_ok(args[k], ast_.at(args[k]))) return false;
        if (!(in_place && k == 0) && !temp(reg)) return false;
        if (!expr_to(args[k], reg)) return false;
    }
    path_arg_ = saved_path;
    path_chain_ = saved_chain;
    print_arg_ = saved_print;
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
// Can evaluating `id` stop the program: a member or index read, a division
// or modulo, a call (D2 ruling).
bool UnitCompiler::may_fault(NodeId id) const {
    if (id == kNoNode) return false;
    const Node& n = ast_.at(id);
    if (n.kind == NodeKind::Member || n.kind == NodeKind::Index || n.kind == NodeKind::Call) return true;
    if (n.kind == NodeKind::Binary && (n.op == Op::Div || n.op == Op::Mod)) return true;
    if (may_fault(n.a) || may_fault(n.b) || may_fault(n.c)) return true;
    const NodeId* list = ast_.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k)
        if (may_fault(list[k])) return true;
    return false;
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
    // CP15 W8: no bytes kind in CP12's inference; bytes built-ins are refused.
    if (bytes_builtin(spec)) return fail(node, "builtin-bytes:" + std::string(spec.name));
    if (plain_arguments_only(spec))
        for (std::uint32_t k = 0; k < node.count; ++k) {
            const Node& arg = ast_.at(args[k]);
            // CP15 W10 (J-Q2): legacy strips the quotes of a string-literal
            // validate argument at top level (and not in loop code).
            if (spec.id == natives::BuiltinId::FnValidate && arg.kind == NodeKind::String)
                return fail(arg, "builtin-literal-arg:validate");
            // CP15 W11: provably plain values only (until CP17's structs).
            const KindSet k_arg = kinds(args[k]);
            if ((k_arg & kKindUnknown) && !unknown_defers(k_arg))
                return fail(arg, "builtin-plain-arg:" + std::string(spec.name));
        }
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
    // D2 ruling (VM subset; register L56): in textual code legacy swallows a
    // fault while evaluating a checked method argument and reports the
    // method's type message instead (`s.contains(o.zz)`: "contains: argument
    // must be a string"), in loop code the fault itself. Refused there when
    // the argument can fault; later arguments are unobservable (above), and
    // methods that accept any value (may_fail false) report the fault too.
    if (receiver != kNoNode && spec.may_fail && !loop_reachable() && node.count > 0 && may_fault(args[0]))
        return fail(ast_.at(args[0]), "builtin-arg-fault-context");
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
    bool is_this = false;  // CP12b: a struct method
    if (const int s = struct_of_receiver(callee.a, is_this); s >= 0) return struct_method(node, s, dst);
    if (!plain_postfix(node) || !plain_postfix(callee) || !no_trailing_comma(node)) return false;
    const std::string_view name = text(ast_.at(callee.b));
    // `length()`/`size()` keep the CP11/CP12 Length opcode (the fast path,
    // with its own kind checks and the context:object-length refusal, Q3).
    const bool length_opcode =
        (name == "length" || name == "size") && node.count == 0;  // rt-lint: allow-string-compare compile time
    if (!length_opcode) {
        // CP14: every other method is a built-in native or refused.
        const KindSet k = kinds(callee.a);
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
    if (!kinds_within(callee.a, size ? kKindAggregate : static_cast<KindSet>(kKindAggregate | kKindString), node,
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

// A member or index path inside user-call arguments (coordinator ruling,
// VM subset, records20k): legacy binds the parameter to the path as a
// location. For a scalar value that is observably a copy (writes and
// rebinding of the root do not reach the parameter, and the binding type is
// the value's), but an aggregate is shared, so only an argument that is
// itself a path whose kinds are certainly scalar is admitted (not empty, no
// aggregate, not Unknown); every other path in call arguments is refused.
// Its failures use legacy's location wording: the whole argument text, at
// its start (`f(a[1].k.b)`: `JSON value 'a[1].k.b' has no member 'k'`, see
// get_member; index messages quote the same text). Member on a non-object and index on a
// non-container cannot occur: those receivers are refused (type:member,
// type:index).
// Assumption: no write outside this unit changes an element's or member's
// kind. That holds because a unit with import, export or module bindings is
// refused as a whole (statement:Import, statement:Export); it must still
// hold when live exports arrive (CP12B, D-046).
bool UnitCompiler::argument_path(const Node& node, std::string& note) {
    if (call_args_ == 0) return true;
    const bool top = path_arg_ != kNoNode && &node == &ast_.at(path_arg_);
    if (!top && (path_chain_ == kNoNode || &node != &ast_.at(path_chain_)))
        return fail(node, "path-in-call-argument");
    const KindSet k = kinds(path_arg_);
    bool scalar = k != 0 && (k & ~kKindScalar) == 0;
    MUTATE_PATH_SCALAR(scalar);
    if (top && !scalar) return fail(node, "path-in-call-argument");
    if (module_names_ != nullptr && !module_names_->empty()) {
        NodeId root = path_arg_;  // records20k: a path rooted at a module's binding is its location
        while (ast_.at(root).kind == NodeKind::Member || ast_.at(root).kind == NodeKind::Index) root = ast_.at(root).a;
        const Node& base = ast_.at(root);
        if (base.kind == NodeKind::Ident &&
            std::find(module_names_->begin(), module_names_->end(), text(base)) != module_names_->end())
            return fail(node, "path-in-call-argument:imported");
    }
    note = std::string(text(ast_.at(path_arg_)));
    path_chain_ = node.a;
    return true;
}

// Argument k written without parentheses of its own: legacy quotes the
// argument's text, `(o.k)` included, which is not the path's.
bool UnitCompiler::bare_argument(const Node& call, std::uint32_t k) const {
    const std::string_view src = ast_.source();
    std::uint32_t at = ast_.at(ast_.list(call)[k]).begin;
    while (at > 0 && (src[at - 1] == ' ' || src[at - 1] == '\t' || src[at - 1] == '\n' || src[at - 1] == '\r')) --at;
    if (at == 0) return false;
    if (k > 0) return src[at - 1] == ',';
    std::uint32_t open = ast_.at(call.a).end;
    while (open < src.size() && src[open] != '(') ++open;
    return at - 1 == open;
}

// The outermost member/index chain `node` belongs to (D1, path wording):
// legacy resolves a postfix path operand as one text and quotes all of it in
// an index message (`a[5].y`: 'a[5].y'); a method call or parentheses end the
// chain. Sets the chain for `node`'s receiver, compiled next.
const Node& UnitCompiler::chain_enter(const Node& node) {
    const Node& top = chain_next_ == &node ? *chain_top_ : node;
    const std::string_view src = ast_.source();
    std::uint32_t at = ast_.at(node.a).end;
    while (at < src.size() && (src[at] == ' ' || src[at] == '\t' || src[at] == '\n' || src[at] == '\r')) ++at;
    const char joint = node.kind == NodeKind::Member ? '.' : '[';
    chain_top_ = &top;
    chain_next_ = at < src.size() && src[at] == joint ? &ast_.at(node.a) : nullptr;
    return top;
}

// D3 (path wording): legacy's textual evaluator resolves a path whose
// receiver has an index other than plain digits (`a[j].k`, `w["y"].k`,
// `a[ 1 ].k`) as one text, and a missing member is reported as `JSON value
// '<receiver>' has no member 'k'`; with digit indexes only, or on its
// prepared evaluator (loop code, functions called from it), as `value has no
// member: k`, except for a path that is a print argument by itself, which it
// resolves as text there too.
bool UnitCompiler::receiver_text_wording(const Node& node, const Node& chain) const {
    if (loop_reachable() && print_arg_ != &chain) return false;
    const std::string_view src = ast_.source();
    for (const Node* n = &ast_.at(node.a); n->kind == NodeKind::Member || n->kind == NodeKind::Index;
         n = &ast_.at(n->a)) {
        if (n->kind != NodeKind::Index) continue;
        const std::uint32_t open = detail::find_in(src, "[", ast_.at(n->a).end, n->end);
        if (open + 1 >= n->end) return true;
        const std::string_view key = src.substr(open + 1, n->end - 1 - (open + 1));
        if (key.empty() || key.find_first_not_of("0123456789") != std::string_view::npos) return true;
    }
    return false;
}

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
    if (!kinds_within(node.a, kKindObject, node, "type:member")) return false;
    const std::uint16_t mark = fb_->top();
    std::uint16_t object = 0;
    const Node& chain = chain_enter(node);
    const std::string_view saved_note = global_note_;
    global_note_ = text(node);
    const bool receiver_ok = reg_any(node.a, object);
    global_note_ = saved_note;
    chain_next_ = nullptr;
    if (!receiver_ok) return false;
    const NodeKind r = ast_.at(node.a).kind;  // CP12b: a call result or literal: textual wording (L43)
    const bool value_receiver = r != NodeKind::Ident && r != NodeKind::Member && r != NodeKind::Index;
    if (location.empty() && (value_receiver || receiver_text_wording(node, chain)))
        location = std::string(text(ast_.at(node.a)));
    const Atom key = atom(text(ast_.at(node.b)));
    if (key.id > 0xffff) return fail(node, "limit:atoms");
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::GetMember, dst, object, static_cast<std::uint16_t>(key.id));
    if (!location.empty()) fb_->note(at, std::move(location));
    fb_->release_to(mark);
    return true;
}

bool UnitCompiler::index(const Node& node, std::uint16_t dst) {
    if (!plain_postfix(node)) return false;
    std::string location;
    if (!argument_path(node, location)) return false;
    if (index_depth_ > 0) return fail(node, "path-in-index");
    const KindSet object_kinds = kinds(node.a), key_kinds = kinds(node.b);
    const bool array = (object_kinds & ~kKindArray) == 0 && (key_kinds & ~kKindNumber) == 0;
    const bool object = (object_kinds & ~kKindObject) == 0 && (key_kinds & ~kKindString) == 0;
    if (!array && !object && !unknown_defers(object_kinds | key_kinds))  // Unknown is neither (review D1)
        return fail(node, "type:index");
    const std::uint16_t mark = fb_->top();
    std::uint16_t object_reg = 0, key = 0;
    const Node& chain = chain_enter(node);
    const std::string_view saved_note = global_note_;
    global_note_ = text(node);
    const bool receiver_ok = reg_any(node.a, object_reg);
    global_note_ = saved_note;
    chain_next_ = nullptr;
    if (!receiver_ok) return false;
    ++index_depth_;
    const bool key_ok = expr_any(node.b, key);
    --index_depth_;
    if (!key_ok) return false;
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::GetIndex, dst, object_reg, key);
    fb_->note(at, location.empty() ? std::string(text(chain)) : std::move(location));
    fb_->release_to(mark);
    return true;
}

} // namespace nift::rt::detail
