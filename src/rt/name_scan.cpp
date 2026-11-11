#include "rt/name_scan.h"

// rt-lint: cold-file compile-time name scan (slot 3)

namespace nift::rt {
namespace {

bool ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

bool ends_with(std::string_view text, std::size_t end, std::string_view suffix) {
    return end >= suffix.size() && text.substr(end - suffix.size(), suffix.size()) == suffix;
}

// A declaration head: `fn(name(`, `fn (name(`, `fn[async](name(` and the like.
bool declaration_head(std::string_view text, std::size_t at) {
#if defined(NIFT_NAME_SCAN_MUTANT) && NIFT_NAME_SCAN_MUTANT == 1  // test-only: fn declarations missed
    static_cast<void>(text); static_cast<void>(at);
    return false;
#else
    return ends_with(text, at, "fn(") || ends_with(text, at, "fn (") || ends_with(text, at, "](");
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
