#include "rt/name_scan.h"

// rt-lint: cold-file compile-time name scan (slot 3)

namespace nift::rt {
namespace {

bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool space_char(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// The index of the last non-whitespace character before `end`, or npos.
std::size_t before_space(std::string_view text, std::size_t end) {
    while (end > 0 && space_char(text[end - 1])) --end;
    return end == 0 ? std::string_view::npos : end - 1;
}

// A declaration head before the name at `at`: `fn(`, with any whitespace (newlines included) between `fn`, `(` and
// the name, or an attribute list ending in `]` (`fn[async](`, `fn [async] (`). Review of C134, F1: an exact-text
// match missed `fn( nope())`. Over-broad on purpose: a comment next to the head (`/`) also counts.
bool declaration_head(std::string_view text, std::size_t at) {
#if defined(NIFT_NAME_SCAN_MUTANT) && NIFT_NAME_SCAN_MUTANT == 1  // test-only: fn declarations missed
    static_cast<void>(text); static_cast<void>(at); static_cast<void>(&before_space);
    return false;
#elif defined(NIFT_NAME_SCAN_MUTANT) && NIFT_NAME_SCAN_MUTANT == 3  // test-only: the exact-text heads of C134
    static_cast<void>(&before_space);
    const auto ends = [&](std::string_view s) { return at >= s.size() && text.substr(at - s.size(), s.size()) == s; };
    return ends("fn(") || ends("fn (") || ends("](");
#else
    std::size_t k = before_space(text, at);
    if (k == std::string_view::npos) return false;
    if (text[k] == '/') return true;
    if (text[k] != '(') return false;
    k = before_space(text, k);
    if (k == std::string_view::npos) return false;
    if (text[k] == '/' || text[k] == ']') return true;
    return k >= 1 && text[k] == 'n' && text[k - 1] == 'f' && (k < 2 || !ident_char(text[k - 2]));
#endif
}

} // namespace

bool name_unbound(std::string_view program, std::string_view name) {
    if (name.empty()) return false;
    for (std::size_t at = program.find(name); at != std::string_view::npos; at = program.find(name, at + 1)) {
        const std::size_t end = at + name.size();
        if (at > 0 && ident_char(program[at - 1])) continue;  // part of a longer identifier
        if (end < program.size() && ident_char(program[end])) continue;
        if (at > 0 && program[at - 1] == '.') continue;  // a member name: binds nothing
        const bool call = end < program.size() && program[end] == '(';
#if defined(NIFT_NAME_SCAN_MUTANT) && NIFT_NAME_SCAN_MUTANT == 2  // test-only: assignments (struct fields) missed
        std::size_t k = end;
        while (k < program.size() && (program[k] == ' ' || program[k] == '\t')) ++k;
        if (k < program.size() && (program[k] == ':' || program[k] == '=')) continue;
#endif
        if (call && !declaration_head(program, at)) continue;  // a plain call: binds nothing
        return false;  // every other occurrence may bind the name
    }
    return true;
}

} // namespace nift::rt
