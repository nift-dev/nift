#include "rt/compiler_call.h"

#include "rt/alias.h"
#include "rt/atom.h"
#include "rt/native.h"
#include "rt/prepared_parse.h"

// Calls (user functions by variant, natives), methods, member and index reads
// (CP11, CP12).

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
    if (fn == plan_.index.end() && !is_native) return fail(callee, "callable:" + std::string(name));
    const std::uint16_t arity = is_native ? native.arity : ast_.at(plan_.functions[fn->second - 1]).count;
    // When `dst` is the newest register nothing above it is live, so the call
    // window (arguments, then the callee frame) starts at `dst` and the result
    // lands in place; otherwise it starts at the first free register.
    const bool in_place = dst + 1u == fb_->top();
    const std::uint16_t base = in_place ? dst : fb_->top();
    const NodeId* args = ast_.list(node);
    if (node.count != arity) return fail(node, "static-error:arity");
    const std::size_t f = is_native ? 0 : fn->second - 1u;
    // print(path) on the prepared path: legacy resolves the path as a location
    // (its messages for a missing element are the textual ones).
    if (is_native && node.count == 1 && prepared()) {
        const NodeKind arg = ast_.at(args[0]).kind;
        if (arg == NodeKind::Index || arg == NodeKind::Member) {
            // When the location is missing, legacy resolves the whole path again
            // on its textual path (expression_statement compiles that code).
            if (!print_statement_) return fail(ast_.at(args[0]), "print-path-nested");
            print_path_ = args[0];
        }
    }
    // Legacy evaluates a call with an argument its prepared parser rejects on
    // the textual path, arguments and callee included (Ast.cpp Kind::Call,
    // legacy_with_span): such a call is compiled as textual code.
    const bool saved_textual = textual_statement_;
    if (prepared())
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
    print_path_ = kNoNode;
    const Variant variant = prepared() ? Variant::Prepared : Variant::Textual;
    if (!is_native && variant == Variant::Textual && !pass_param_types(f, node.count, args, base)) return false;
    fb_->set_offset(site(node));
    if (is_native) {
        fb_->emit(OpCode::CallNative, base, static_cast<std::uint16_t>(native.id),
                  static_cast<std::uint16_t>(node.count));
    } else {
        const std::size_t at =
            fb_->emit(OpCode::Call, base, proto_for(f, variant), static_cast<std::uint16_t>(node.count));
        fb_->note(at, std::string(name));
    }
    std::uint16_t slot = base;
    if (node.count == 0 && !in_place && !temp(slot)) return false;  // the result slot exists even without arguments
    if (dst != base) fb_->emit(OpCode::Move, dst, base);
    fb_->release_to(in_place ? static_cast<std::uint16_t>(dst + 1) : base);
    return builder_ok();
}

bool UnitCompiler::method_call(const Node& node, std::uint16_t dst) {
    const Node& callee = ast_.at(node.a);
    if (!plain_postfix(node) || !plain_postfix(callee) || !no_trailing_comma(node)) return false;
    const std::string_view name = text(ast_.at(callee.b));
    if (name != "length" && name != "size")  // rt-lint: allow-string-compare compile time
        return fail(callee, "method:" + std::string(name));
    if (node.count != 0) return fail(node, "method-arity");
    const bool size = name == "size";  // rt-lint: allow-string-compare compile time
    if (!kinds_within(callee.a, size ? kKindAggregate : static_cast<std::uint8_t>(kKindAggregate | kKindString), node,
                      "type:length"))
        return false;
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
    const bool path = print_path_ == id_of(node);
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::GetMember, dst, object, static_cast<std::uint16_t>(key.id));
    if (path) {
        fb_->at(at).flags |= kInstrPathText;
        fb_->note(at, std::string());
    }
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
    if (!array && !object && !((object_kinds | key_kinds) & kKindUnknown)) return fail(node, "type:index");
    const std::uint16_t mark = fb_->top();
    std::uint16_t object_reg = 0, key = 0;
    if (!reg_any(node.a, object_reg)) return false;
    // Textual: an error inside the brackets is reported at the index expression.
    const std::uint32_t saved_site = index_site_;
    if (!prepared()) index_site_ = site(node);
    ++index_depth_;
    const bool key_ok = expr_any(node.b, key);
    --index_depth_;
    index_site_ = saved_site;
    if (!key_ok) return false;
    const bool path = print_path_ == id_of(node);
    fb_->set_offset(site(node));
    const std::size_t at = fb_->emit(OpCode::GetIndex, dst, object_reg, key);
    fb_->note(at, std::string(text(node)));
    if (path) fb_->at(at).flags |= kInstrPathText;
    fb_->release_to(mark);
    return true;
}

} // namespace nift::rt::detail
