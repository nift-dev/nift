#include "rt/refusal_names.h"

#include "rt/legacy_names.h"
#include "rt/native.h"

#include <cctype>
#include <unordered_set>

// rt-lint: cold-file test tooling (refusal inventory), never on an execution path

namespace nift::rt::inventory {

namespace {

bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

std::string_view ident_at(std::string_view text, std::size_t& p) {
    while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) ++p;
    const std::size_t s = p;
    while (p < text.size() && ident_char(text[p])) ++p;
    return text.substr(s, p - s);
}

bool eat(std::string_view text, std::size_t& p, std::string_view word) {
    std::size_t q = p;
    while (q < text.size() && (text[q] == ' ' || text[q] == '\t' || text[q] == '\n')) ++q;
    if (text.substr(q, word.size()) != word) return false;
    p = q + word.size();
    return true;
}

} // namespace

std::vector<std::string_view> declared_names_in_text(std::string_view text) {
    std::vector<std::string_view> out;
    std::size_t p = 0;
    eat(text, p, "export");
    const std::size_t start = p;
    if (eat(text, p, "@fn") || eat(text, p, "fn")) {
        eat(text, p, "[async]");
        if (eat(text, p, "(")) {
            const std::string_view name = ident_at(text, p);
            if (!name.empty()) out.push_back(name);
        }
        return out;
    }
    p = start;
    if (eat(text, p, "@struct") || eat(text, p, "struct")) {
        if (eat(text, p, "(")) {
            const std::string_view name = ident_at(text, p);
            if (!name.empty()) out.push_back(name);
        }
        return out;
    }
    p = start;
    if (eat(text, p, "enum ")) {
        const std::string_view name = ident_at(text, p);
        if (!name.empty()) out.push_back(name);
        return out;
    }
    p = start;
    if (!eat(text, p, "const ")) eat(text, p, "immut ");
    const std::string_view name = ident_at(text, p);
    if (!name.empty() && eat(text, p, ":=")) out.push_back(name);
    return out;
}

// An identifier of the source that the compiler accepts as a binding name:
// not reserved and not a function's name.
std::pair<std::uint32_t, std::uint32_t> spill_name(const Ast& ast, NodeId root) {
    const std::string_view src = ast.source();
    std::unordered_set<std::string_view> functions;
    const Node& program = ast.at(root);
    for (std::uint32_t k = 0; k < program.count; ++k) {
        const Node& fn = ast.at(ast.list(program)[k]);
        if (fn.kind == NodeKind::FnDecl && fn.a != kNoNode) functions.insert(ast.text(ast.at(fn.a)));
    }
    for (std::uint32_t i = 0; i < src.size();) {
        const char c = src[i];
        if (!(std::isalpha(static_cast<unsigned char>(c)) || c == '_')) { ++i; continue; }
        std::uint32_t j = i;
        while (j < src.size() && (std::isalnum(static_cast<unsigned char>(src[j])) || src[j] == '_')) ++j;
        const std::string_view word = src.substr(i, j - i);
        if (!reserved_name(word) && !legacy_special_name(word) && !functions.count(word)) {
            return {i, j};
        }
        i = j;
    }
    return {0, 0};
}

} // namespace nift::rt::inventory
