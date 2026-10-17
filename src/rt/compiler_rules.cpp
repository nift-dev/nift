#include "rt/compiler_rules.h"

#include "rt/text_type.h"

namespace nift::rt::detail {

namespace {

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

// Legacy types a callee parameter from the argument's text; the VM requires
// that to agree with the value's own type (else it bails).
bool UnitCompiler::param_check(NodeId arg, std::uint16_t reg) {
    const Node& node = ast_.at(arg);
    const TextType type = infer_text_type(type_text(node));
    if (type.kind == TextType::Kind::Known && (type.known == -1 || type.known == 2)) return true;
    const std::uint16_t mark = fb_->top();
    std::uint16_t operand = 0;
    if (!text_type_of(type, node, operand)) return false;
    fb_->emit(OpCode::CheckParam, reg, 0, operand);
    fb_->release_to(mark);
    return true;
}

} // namespace nift::rt::detail
