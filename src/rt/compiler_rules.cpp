#include "rt/compiler_rules.h"

#include "rt/prepared_parse.h"
#include "rt/text_type.h"

namespace nift::rt::detail {

namespace {

bool is_comparison(const Node& n) {
    if (n.kind != NodeKind::Binary) return false;
    switch (n.op) {
        case Op::Eq: case Op::Ne: case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: return true;
        default: return false;
    }
}

bool is_receiver(NodeKind kind) {
    return kind == NodeKind::Ident || kind == NodeKind::Member || kind == NodeKind::Index;
}

} // namespace

// CP10 accepts some postfix spellings legacy reads differently (whitespace
// before `.`/`[`/`(`, postfix on literals, calls on non-name callees,
// trailing commas); only the plain spellings are compiled.
bool UnitCompiler::plain_postfix(const Node& node) {
    const std::string_view src = ast_.source();
    const Node& left = ast_.at(node.a);
    const char expected = node.kind == NodeKind::Member ? '.' : node.kind == NodeKind::Index ? '[' : '(';
    if (left.end >= src.size() || src[left.end] != expected) return fail(node, "postfix-spelling");
    if (node.kind == NodeKind::Member && ast_.at(node.b).begin != left.end + 1) return fail(node, "postfix-spelling");
    if (node.kind != NodeKind::Call && !is_receiver(left.kind)) return fail(node, "postfix-receiver");
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

bool UnitCompiler::text_type_of(const TextType& type, const Node& at, std::uint16_t& operand) {
    if (type.kind == TextType::Kind::Known)
        return fb_->constant(Value::integer(type.known), operand) || builder_ok();
    if (type.kind == TextType::Kind::Binding) {
        if (const Binding* b = scopes_.find(type.binding)) { operand = b->type_reg; return true; }
        if (type.binding == "true" || type.binding == "false" ||  // rt-lint: allow-string-compare compile time
            type.binding == "null" || plan_.index.count(type.binding))  // rt-lint: allow-string-compare compile time
            return fb_->constant(Value::integer(-1), operand) || builder_ok();
        return fail(at, "text-type-name");
    }
    std::uint16_t left = 0, right = 0;
    if (!text_type_of(*type.left, at, left) || !text_type_of(*type.right, at, right) || !temp(operand)) return false;
    fb_->emit(OpCode::TypeMeet, operand, left, right);
    return true;
}

bool UnitCompiler::text_type_operand(NodeId value, std::uint16_t& operand) {
    const Node& node = ast_.at(value);
    return text_type_of(infer_text_type(type_text(node)), node, operand);
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
        if (!text_type_of(infer_text_type(type_text(arg)), arg, operand)) return false;
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

namespace {

bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool word(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// [open, close] enclose a balanced text (quotes respected).
bool balanced_between(std::string_view src, std::uint32_t open, std::uint32_t close) {
    int depth = 0;
    char quote = 0;
    for (std::uint32_t i = open + 1; i < close; ++i) {
        const char c = src[i];
        if (quote) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
            continue;
        }
        if (c == '"' || c == '\'') quote = c;
        else if (c == '(') ++depth;
        else if (c == ')' && --depth < 0) return false;
    }
    return depth == 0 && !quote;
}

struct Span {
    std::uint32_t begin, end;
};

// Legacy's prepared parser gives a parenthesised expression the span of its
// parentheses, and an operator node the span of its operands.
Span legacy_span(const Ast& ast, const Node& n) {
    Span s{n.begin, n.end};
    switch (n.kind) {
        case NodeKind::Binary: case NodeKind::Logical: case NodeKind::Coalesce:
            s.begin = legacy_span(ast, ast.at(n.a)).begin;
            s.end = legacy_span(ast, ast.at(n.b)).end;
            break;
        case NodeKind::Ternary:
            s.begin = legacy_span(ast, ast.at(n.a)).begin;
            s.end = legacy_span(ast, ast.at(n.c)).end;
            break;
        case NodeKind::Index: case NodeKind::Member: s.begin = legacy_span(ast, ast.at(n.a)).begin; break;
        case NodeKind::Call:
            if (ast.at(n.a).kind == NodeKind::Member) s.begin = legacy_span(ast, ast.at(ast.at(n.a).a)).begin;
            break;
        case NodeKind::Unary: s.end = legacy_span(ast, ast.at(n.a)).end; break;
        default: break;
    }
    const std::string_view src = ast.source();
    while (true) {
        std::uint32_t p = s.begin;
        while (p > 0 && space(src[p - 1])) --p;
        if (p == 0 || src[p - 1] != '(') break;
        const std::uint32_t open = p - 1;
        std::uint32_t q = open;
        while (q > 0 && space(src[q - 1])) --q;
        if (q > 0 && (word(src[q - 1]) || src[q - 1] == ')' || src[q - 1] == ']')) {
            // A call, `if (`/`while (`: not a group; `return (` is one.
            const std::string_view before = q >= 6 ? src.substr(q - 6, 6) : std::string_view();
            const bool keyword = before == "return";  // rt-lint: allow-string-compare compile time
            const bool after_return = keyword && (q == 6 || !word(src[q - 7]));
            if (!after_return) break;
        }
        std::uint32_t r = s.end;
        while (r < src.size() && space(src[r])) ++r;
        if (r >= src.size() || src[r] != ')' || !balanced_between(src, open, r)) break;
        s.begin = open;
        s.end = r + 1;
    }
    return s;
}

} // namespace

std::uint32_t UnitCompiler::prepared_begin(const Node& node) const { return legacy_span(ast_, node).begin; }
std::uint32_t UnitCompiler::prepared_end(const Node& node) const { return legacy_span(ast_, node).end; }

// Does legacy's prepared evaluator run this statement (parse_statement
// accepts it), or its textual one (StmtKind::Legacy)?
bool UnitCompiler::statement_preparable(const Node& node) const {
    const auto accepts = [this](NodeId id) {
        if (id == kNoNode) return true;
        const Node& n = ast_.at(id);
        const std::uint32_t b = prepared_begin(n);
        return legacy_prepared_parse_accepts(ast_.source().substr(b, prepared_end(n) - b));
    };
    switch (node.kind) {
        case NodeKind::Declare: case NodeKind::Assign: case NodeKind::CompoundAssign: return accepts(node.b);
        case NodeKind::If: case NodeKind::While: return accepts(node.a);
        case NodeKind::Return: return (node.flags & kFlagBare) || accepts(node.a);
        case NodeKind::ExprStmt: {
            const Node& e = ast_.at(node.a);
            if (!accepts(node.a)) return false;
            if (e.kind != NodeKind::Call) return true;
            const NodeId* args = ast_.list(e);
            // A rejected argument sends the call to the textual path.
            for (std::uint32_t k = 0; k < e.count; ++k)
                if (!accepts(args[k])) return false;
            return true;
        }
        default: return true;
    }
}

bool UnitCompiler::kinds_within(NodeId id, std::uint8_t allowed, const Node& at, const char* reason) {
    if ((kinds(id) & kKindUnknown) || (kinds(id) & ~allowed) == 0) return true;
    return fail(at, reason);
}

// Legacy binds a declaration, assignment, return, element or literal item
// written as a place (name, member or index) to an aggregate as a location
// reference (P8); the VM copies. Refused when the place can hold an array or
// object.
bool UnitCompiler::no_alias(NodeId value, const Node& at) {
    const NodeKind kind = ast_.at(value).kind;
    if (kind != NodeKind::Ident && kind != NodeKind::Member && kind != NodeKind::Index) return true;
    if ((kinds(value) & kKindUnknown) || (kinds(value) & kKindAggregate) == 0) return true;
    return fail(at, "aliasing");
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
    return !ends_with_not(node.a) || fail(node, "prefix-not-precedence");
}

// A comparison whose operand is itself a comparison (`1 != 1 == 1`,
// `(a < b) == c`, any order and context): the CP10 tree groups `==`/`!=`
// right-associatively while legacy's textual evaluator splits at the first
// `==`, and relational chains differ again; none is compiled (review round
// 3, F2). Parentheses leave no node, so a parenthesised comparison operand is
// refused as well (conservative).
bool UnitCompiler::comparison_chain_ok(const Node& node) {
    if (!is_comparison(node)) return true;
    if (is_comparison(ast_.at(node.a)) || is_comparison(ast_.at(node.b))) return fail(node, "comparison-chain");
    return true;
}

// Element assignment `a[i] = v`: legacy's textual evaluator treats a target
// whose text contains `.` as a member path (`a[a.length() - 1] = 9`,
// `a[o.k] = 1`, `a[1.0] = 1` fail there), so only a plain local name or an
// unsigned integer literal is compiled as the index (review round 3, F1).
bool UnitCompiler::element_target_ok(const Node& target) {
    const Node& key = ast_.at(target.b);
    bool plain = key.kind == NodeKind::Ident;
    if (key.kind == NodeKind::Number) {
        plain = true;
        for (char c : text(key))
            if (c < '0' || c > '9') plain = false;
    }
    // The bracket holds exactly that token (no parentheses around it).
    std::string_view inside = ast_.source().substr(ast_.at(target.a).end, target.end - ast_.at(target.a).end);
    while (!inside.empty() && (inside.front() == '[' || inside.front() == ' ' || inside.front() == '\t'))
        inside.remove_prefix(1);
    while (!inside.empty() && (inside.back() == ']' || inside.back() == ' ' || inside.back() == '\t'))
        inside.remove_suffix(1);
    if (!plain || inside != text(key) || text(target).find('.') != std::string_view::npos)
        return fail(target, "index-target");
    return true;
}

} // namespace nift::rt::detail
