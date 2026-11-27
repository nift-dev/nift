#include "rt/compiler_rules.h"

#include "rt/compiler_native_args.h"
#include "rt/prepared_parse.h"
#include "rt/text_type.h"

#include <algorithm>

namespace nift::rt::detail {

namespace {

bool is_comparison(const Node& n) {
    if (n.kind != NodeKind::Binary) return false;
    switch (n.op) {
        case Op::Eq: case Op::Ne: case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: return true;
        default: return false;
    }
}

// Receivers a member, index or method link may follow. CP12 phase B adds call
// results and array/object/string literals: CP10 admits a chain on them only
// in the shapes legacy dispatches (links from a plain name, links on array
// and object literals, known methods on other receivers; everything else is
// Opaque), and legacy evaluates those shapes like any other receiver, in every
// context (cp12b-resolver.md, "Chained postfix").
bool is_receiver(NodeKind kind) {
    switch (kind) {
        case NodeKind::Ident: case NodeKind::Member: case NodeKind::Index:
        case NodeKind::Call: case NodeKind::Array: case NodeKind::Object: case NodeKind::String: return true;
        default: return false;
    }
}

} // namespace

// CP10 accepts some postfix spellings legacy reads differently (whitespace
// before `.`/`[`/`(`, postfix on literals, calls on non-name callees,
// trailing commas). Admit irregular spellings only where their shape and
// legacy behavior are established.
bool UnitCompiler::plain_postfix(const Node& node) {
    const std::string_view src = ast_.source();
    const Node& left = ast_.at(node.a);
    const char expected = node.kind == NodeKind::Member ? '.' : node.kind == NodeKind::Index ? '[' : '(';
    // The parser returns the inner AST node for a grouped receiver. Its
    // span ends before the closing parentheses, even when the dot is
    // adjacent to the group. Admit only verified receiver/method pairs.
    if (node.kind == NodeKind::Member && node.begin < left.begin &&
        left.end < ast_.at(node.b).begin) {
        const Node& member = ast_.at(node.b);
        const auto name = ast_.text(member);
        const auto opening = src.substr(node.begin, left.begin - node.begin);
        const auto closing = src.substr(left.end, member.begin - left.end - 1);
        const bool string_method = kinds(node.a) == kKindString &&
            (name == "split" || name == "stringify");  // rt-lint: allow-string-compare compile time
        // A numeric group `(a / b).floor()` is legacy's ordinary postfix chain.
        const KindSet grouped = kinds(node.a);
        const bool numeric_method = (grouped == 0 || (grouped & (kKindNumber | kKindUnknown)) != 0) &&
            (name == "floor" || name == "ceil" || name == "round" || name == "abs" ||  // rt-lint: allow-string-compare
             name == "to_int" || name == "to_string");  // rt-lint: allow-string-compare compile time
        bool integer_remainder = false;
        if (left.kind == NodeKind::Binary && left.op == Op::Mod &&
            ast_.at(left.a).kind == NodeKind::Ident &&
            kinds(left.a) == kKindInt && kinds(left.b) == kKindInt &&
            name == "to_string") {  // rt-lint: allow-string-compare compile time
            const Node& divisor = ast_.at(left.b);
            const auto spelling = text(divisor);
            // A nonzero literal avoids context-dependent modulo fault reports.
            integer_remainder = divisor.kind == NodeKind::Number && !spelling.empty() &&
                spelling.front() >= '1' && spelling.front() <= '9' &&
                spelling.find_first_not_of("0123456789") == std::string_view::npos;
        }
        if ((string_method || numeric_method || integer_remainder) &&
            opening.size() == closing.size() &&
            opening.find_first_not_of('(') == std::string_view::npos &&
            closing.find_first_not_of(')') == std::string_view::npos &&
            src[member.begin - 1] == '.' && node.end < src.size() && src[node.end] == '(')
            return true;
    }
    if (left.end >= src.size() || src[left.end] != expected) {
        if (node.kind == NodeKind::Call && left.kind == NodeKind::Member &&
            ast_.at(left.a).kind == NodeKind::Ident && left.begin == ast_.at(left.a).begin &&
            kinds(left.a) == kKindString && node.count == 1 &&
            text(ast_.at(left.b)) == "split") {  // rt-lint: allow-string-compare compile time
            const auto open = src.find_first_not_of(" \t", left.end);
            const Node& receiver = ast_.at(left.a);
            const auto name = text(receiver);
            const Binding* binding = find_binding(name);
            // A String kind can still contain a legacy runtime tag. Reuse
            // the plain-literal proof and reject any possible reassignment.
            // An effectful delimiter can be retried by legacy's loop evaluator.
            const Node& delimiter = ast_.at(ast_.list(node)[0]);
            if (open < node.end && src[open] == '(' && binding &&
                delimiter.kind == NodeKind::String && delimiter.count == 0 &&
                plain_readonly_arity_string(ast_, receiver, binding) && !assigned_.count(name) &&
                std::none_of(assigned_fn_.begin(), assigned_fn_.end(),
                             [&](const auto& names) { return names.count(name); }))
                return true;
        }
        return fail(node, "postfix-spelling");
    }
    if (node.kind == NodeKind::Member && ast_.at(node.b).begin != left.end + 1) return fail(node, "postfix-spelling");
    if (node.kind != NodeKind::Call && !is_receiver(left.kind)) {
        const bool scalar = left.kind == NodeKind::Number || left.kind == NodeKind::True ||
                            left.kind == NodeKind::False || left.kind == NodeKind::Null;
        if (!scalar || node.kind != NodeKind::Member ||
            src.substr(left.end, 12) != ".to_string()")  // rt-lint: allow-string-compare compile time
            return fail(node, "postfix-receiver");
    }
    // A member read (not a method's callee: no `(` follows) on a call result
    // or literal: legacy's missing-member message depends on the context
    // ("JSON value 'f()' has no member 'k'" in textual code, "value has no
    // member: k" in loop code, L43). CP12b: compiled with the textual
    // wording (member()); in loop code, where legacy words it differently,
    // refused (no context-dependent wording, owner decision).
    const bool path = left.kind == NodeKind::Ident || left.kind == NodeKind::Member || left.kind == NodeKind::Index;
    if (node.kind == NodeKind::Member && !path && (node.end >= src.size() || src[node.end] != '(') &&
        (loop_region() || loop_type_error_ambiguous()))
        return fail(node, "postfix-receiver:member");
    return true;
}

bool UnitCompiler::no_trailing_comma(const Node& node) {
    const std::string_view src = ast_.source();
    std::uint32_t p = node.end - 1;  // closing bracket
    while (p > node.begin && (src[p - 1] == ' ' || src[p - 1] == '\t' || src[p - 1] == '\n' || src[p - 1] == '\r')) --p;
    if (p > node.begin && src[p - 1] == ',') return fail(node, "trailing-comma");
    return true;
}

// Source text legacy infers a type from: the expression plus closing
// parentheses CP10 leaves outside the node's span (`(1) + (2.5)`).
std::string_view UnitCompiler::type_text(const Node& node) const {
    const std::string_view src = ast_.source();
    std::uint32_t end = node.end;
    int open = 0;
    bool quoted = false;
    char quote = 0;
    for (std::uint32_t i = node.begin; i < end; ++i) {
        const char c = src[i];
        if (quoted) { if (c == '\\') ++i; else if (c == quote) quoted = false; continue; }
        if (c == '"' || c == '\'') { quoted = true; quote = c; continue; }
        open += c == '(' ? 1 : c == ')' ? -1 : 0;
    }
    while (open > 0) {
        std::uint32_t p = end;
        while (p < src.size() && (src[p] == ' ' || src[p] == '\t')) ++p;
        if (p >= src.size() || src[p] != ')') break;
        end = p + 1;
        --open;
    }
    return src.substr(node.begin, end - node.begin);
}

bool UnitCompiler::text_type_of(const TextType& type, const Node& at, std::uint16_t& operand, bool allow_unbound) {
    if (type.kind == TextType::Kind::Known)
        return fb_->constant(Value::integer(type.known), operand) || builder_ok();
    if (type.kind == TextType::Kind::Binding) {
        if (const Binding* b = find_binding(type.binding)) {
            // An uncaptured catch binding has no runtime type register: its
            // fixed CP13B Error type must not be read from the Error value.
            if (b->readonly && b->decl_kinds == kKindError)
                return fb_->constant(Value::integer(9), operand) || builder_ok();
            if (b->cell || b->upvalue) return fb_->constant(Value::integer(-1), operand) || builder_ok();
            if (b->global) return global_read(b->type_reg, operand);  // CP12b
            operand = b->type_reg;
            return true;
        }
        if (type.binding == "true" || type.binding == "false" ||  // rt-lint: allow-string-compare compile time
            type.binding == "null" || plan_.index.count(type.binding))  // rt-lint: allow-string-compare compile time
            return fb_->constant(Value::integer(-1), operand) || builder_ok();
        if (allow_unbound)
            return fb_->constant(Value::integer(-1), operand) || builder_ok();
        return fail(at, "text-type-name");
    }
    std::uint16_t left = 0, right = 0;
    if (!text_type_of(*type.left, at, left, allow_unbound) ||
        !text_type_of(*type.right, at, right, allow_unbound) || !temp(operand)) return false;
    fb_->emit(OpCode::TypeMeet, operand, left, right);
    return true;
}

bool UnitCompiler::text_type_operand(NodeId value, std::uint16_t& operand) {
    return text_type_of(text_type_at(value), ast_.at(value), operand);
}

// A textual call: legacy's bind_call_param types each parameter from the
// argument's text (P5). For every parameter whose type the callee can
// observe, the caller leaves that text type in the callee's type register
// (frame register nparams + k, just above the arguments); the callee's
// BindType combines it with the value exactly as legacy does.
bool UnitCompiler::pass_param_types(std::size_t f, std::uint32_t argc, const NodeId* args, std::uint16_t base) {
    bool any = false;
    for (std::uint32_t k = 0; k < argc; ++k) any = any || observable_[f][k];
    if (!any) return true;
    while (fb_->top() < base + 2u * argc) {
        std::uint16_t reg = 0;
        if (!temp(reg)) return false;
    }
    for (std::uint32_t k = 0; k < argc; ++k) {
        if (!observable_[f][k]) continue;
        const Node& arg = ast_.at(args[k]);
        const std::uint16_t mark = fb_->top();
        std::uint16_t operand = 0;
        if (!text_type_of(text_type_at(args[k]), arg, operand)) return false;
        const auto dst = static_cast<std::uint16_t>(base + argc + k);
        if (operand & kConstantBit) fb_->emit(OpCode::LoadK, dst, static_cast<std::uint16_t>(operand & ~kConstantBit));
        else if (operand != dst) fb_->emit(OpCode::Move, dst, operand);
        fb_->release_to(mark);
    }
    return builder_ok();
}

void UnitCompiler::update_flags() {
    if (!fb_) return;
    // Evidence only (VM stats, D-018 difference policies): code legacy runs
    // on its prepared evaluator. It does not change what the VM computes.
    fb_->set_flags(loop_region() ? kInstrLoopPath : 0);
}



bool UnitCompiler::print_argument_ok(NodeId id, const Node& arg) {
    // Unknown/empty kinds are not assumed scalar and are not a licence to
    // admit blindly: they defer to the VM's checked native print, which now
    // raises legacy's ordinary print Error (never an internal failure).
    // CP25: a value proved (or possibly) non-renderable is no longer refused:
    // the VM's checked native print raises legacy's fatal "print: value is not
    // directly renderable" Error at the print statement, after the output of
    // every earlier statement, exactly where legacy raises it.
    static_cast<void>(id);
    static_cast<void>(arg);
    return true;
}

// CP25: an error legacy raises when it reaches this point (never catchable,
// after the output of every earlier statement) is compiled as that Error
// instead of refusing the whole unit. Undefined with b == 1 raises the note.
bool UnitCompiler::legacy_fatal(const Node& at, std::string message, std::uint16_t dst) {
    fb_->set_offset(site(at));
    fb_->note(fb_->emit(OpCode::Undefined, dst, 1), std::move(message));
    fb_->emit(OpCode::LoadNull, dst);
    return builder_ok();
}

bool UnitCompiler::kinds_within(NodeId id, KindSet allowed, const Node& at, const char* reason) {
    if ((kinds(id) & ~allowed) == 0 || unknown_defers(kinds(id))) return true;  // Unknown: never within (D1)
    return fail(at, reason);
}

// Legacy applies a prefix `!` to the rest of the evaluation unit (`!x + 1`
// is `!(x + 1)`), which CP10 (before its fix) parses as `(!x) + 1`. A `!`
// that ends the left side of an infix operator is therefore not compiled.
bool UnitCompiler::ends_with_not(NodeId id) const {
    const Node& n = ast_.at(id);
    if (n.kind == NodeKind::Unary && n.op == Op::Not) return true;
    if (n.kind == NodeKind::Binary || n.kind == NodeKind::Logical || n.kind == NodeKind::Coalesce)
        return ends_with_not(n.b);
    if (n.kind == NodeKind::Ternary) return ends_with_not(n.c);
    return false;
}

bool UnitCompiler::infix_left_ok(const Node& node) {
    if (!ends_with_not(node.a)) return true;
    if (node.kind == NodeKind::Logical && in_function_ && !session_ && boolean_method_condition(node_id(node)))
        return true;
    return fail(node, "prefix-not-precedence");
}

// A comparison whose operand is itself a comparison (`1 != 1 == 1`,
// `(a < b) == c`, any order and context): the CP10 tree groups `==`/`!=`
// right-associatively while legacy's textual evaluator splits at the first
// `==`, and relational chains differ again; none is compiled (review round
// 3, F2). Parentheses leave no node, so a parenthesised comparison operand is
// refused as well (conservative).
bool UnitCompiler::comparison_chain_ok(const Node& node) {
    if (!is_comparison(node)) return true;
    const Node& left = ast_.at(node.a);
    const Node& right = ast_.at(node.b);
    // The v4.8 parity witness compares the admitted `same` Boolean binding
    // with one grouped equality. General chains keep their refusal.
    if (node.op == Op::Ne && left.kind == NodeKind::Ident && kinds(node.a) == kKindBool &&
        text(left) == "same" &&  // rt-lint: allow-string-compare compile time
        !session_ && !in_function_ && !in_lambda_ && !loop_reachable() &&
        is_comparison(right) && (right.op == Op::Eq || right.op == Op::Ne)) {
        const auto src = ast_.source();
        const auto gap = src.substr(left.end, right.begin - left.end);
        const auto last = gap.find_last_not_of(" \t");
        if (last != std::string_view::npos && gap[last] == '(' &&
            right.end < src.size() && src[right.end] == ')')
            return true;
    }
    if (is_comparison(ast_.at(node.a)) || is_comparison(ast_.at(node.b))) return fail(node, "comparison-chain");
    return true;
}

} // namespace nift::rt::detail
