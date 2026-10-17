#include "rt/text_type.h"

#include <cstdlib>
#include <string>

// rt-lint: cold-file compile-time port of the legacy text inference

namespace nift::rt {

namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool is_alnum(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

std::string_view trimmed(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && is_space(s[b])) ++b;
    while (e > b && is_space(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Strips parentheses that enclose the whole text, as legacy does.
std::string_view unwrap(std::string_view t) {
    bool wrapped = true;
    while (wrapped && t.size() >= 2 && t.front() == '(' && t.back() == ')') {
        wrapped = false;
        int depth = 0;
        bool enclosed = true;
        for (std::size_t i = 0; i < t.size(); ++i) {
            if (t[i] == '(') {
                ++depth;
            } else if (t[i] == ')') {
                --depth;
                if ((depth == 0 && i + 1 != t.size()) || depth < 0) { enclosed = false; break; }
            }
        }
        if (enclosed && depth == 0) {
            t = t.substr(1, t.size() - 2);
            wrapped = true;
        }
    }
    return t;
}

// Rightmost top-level operator from `ops` outside quotes, brackets and
// parentheses; a `+`/`-` directly after another operator character is unary.
std::size_t top_level_binary(std::string_view t, std::string_view ops) {
    bool quoted = false;
    char quote = 0;
    int parens = 0, brackets = 0;
    for (std::size_t i = t.size(); i-- > 0;) {
        const char c = t[i];
        if (quoted) {
            if (c == quote && (i == 0 || t[i - 1] != '\\')) quoted = false;
            continue;
        }
        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if (c == ']') { ++brackets; continue; }
        if (c == '[') { if (brackets) --brackets; continue; }
        if (brackets) continue;
        if (c == ')') { ++parens; continue; }
        if (c == '(') { if (parens) --parens; continue; }
        if (parens || ops.find(c) == std::string_view::npos) continue;
        if ((c == '+' || c == '-') &&
            (i == 0 || std::string_view("+-*/%(<>=!&|?:,").find(t[i - 1]) != std::string_view::npos))
            continue;
        return i;
    }
    return std::string_view::npos;
}

TextType known(int type) {
    TextType t;
    t.known = type;
    return t;
}

TextType infer(std::string_view source) {
    std::string_view t = trimmed(source);
    if (t.size() > 2048) return known(-1);
    t = unwrap(t);
    if (t.empty()) return known(-1);
    const std::string copy(t);
    char* end = nullptr;
    std::strtod(copy.c_str(), &end);
    if (end && *end == '\0') return known(copy.find_first_of(".eE") != std::string::npos ? 3 : 2);
    std::size_t pos = top_level_binary(t, "+-");
    if (pos == std::string_view::npos) pos = top_level_binary(t, "*/%");
    if (pos != std::string_view::npos) {
        TextType left = infer(t.substr(0, pos));
        TextType right = infer(t.substr(pos + 1));
        if (left.kind == TextType::Kind::Known && right.kind == TextType::Kind::Known)
            return known(meet_text_types(left.known, right.known));
        if ((left.kind == TextType::Kind::Known && left.known == 3) ||
            (right.kind == TextType::Kind::Known && right.known == 3))
            return known(3);
        TextType meet;
        meet.kind = TextType::Kind::Meet;
        meet.left = std::make_shared<const TextType>(std::move(left));
        meet.right = std::make_shared<const TextType>(std::move(right));
        return meet;
    }
    std::size_t root = 0;
    while (root < t.size() && (is_alnum(t[root]) || t[root] == '_')) ++root;
    if (root == 0 || root != t.size()) return known(-1);  // a member/index path is untyped (no structs here)
    TextType binding;
    binding.kind = TextType::Kind::Binding;
    binding.binding = t;
    return binding;
}

} // namespace

TextType infer_text_type(std::string_view text) { return infer(text); }

bool refers_to_binding(const TextType& type) {
    if (type.kind == TextType::Kind::Binding) return true;
    if (type.kind == TextType::Kind::Meet) return refers_to_binding(*type.left) || refers_to_binding(*type.right);
    return false;
}

} // namespace nift::rt
