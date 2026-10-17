// Byte-level scanning for the Runtime 2.0 parser (see source_scan.h).
#include "rt/source_scan.h"

#include "rt/lexer.h"

#include <algorithm>

namespace nift::rt::detail {

bool is_space(char c) { return is_ascii_space(c); }

std::uint32_t skip_space(std::string_view src, std::uint32_t p, std::uint32_t end) {
    while (p < end && is_space(src[p])) ++p;
    return p;
}

std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool valid_identifier(std::string_view text) {
    if (text.empty() || !is_ident_start(text[0])) return false;
    return std::all_of(text.begin() + 1, text.end(), is_ident_char);
}

std::uint32_t find_in(std::string_view src, std::string_view what, std::uint32_t from, std::uint32_t end) {
    if (from > end) return end;
    const auto at = src.substr(0, end).find(what, from);
    return at == std::string_view::npos ? end : static_cast<std::uint32_t>(at);
}

bool starts(std::string_view src, std::uint32_t p, std::uint32_t end, std::string_view what) {
    return p + what.size() <= end && src.compare(p, what.size(), what) == 0;
}

// Parser::find_balanced (src/Parser.cpp) restricted to [open, end).
bool find_balanced(std::string_view src, std::uint32_t open, std::uint32_t end,
                   char open_char, char close_char, std::uint32_t& close) {
    if (open >= end || src[open] != open_char) return false;
    std::size_t depth = 0;
    bool quoted = false;
    char quote = 0;
    for (std::uint32_t i = open; i < end; ++i) {
        const char c = src[i];
        if (quoted) {
            if (c == '\\' && i + 1 < end) ++i;
            else if (c == quote) quoted = false;
            continue;
        }
        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if (c == '`') {
            std::uint32_t run = 1;
            while (i + run < end && src[i + run] == '`') ++run;
            const std::uint32_t span_end = find_in(src, src.substr(i, run), i + run, end);
            if (span_end != end) i = span_end + run - 1;
            continue;
        }
        if (c == '@' && starts(src, i, end, "@/*")) {
            const std::uint32_t comment_end = find_in(src, "*/", i + 3, end);
            if (comment_end == end) return false;
            i = comment_end + 1;
            continue;
        }
        if (c == '<' && starts(src, i, end, "<!--")) {
            const std::uint32_t comment_end = find_in(src, "-->", i + 4, end);
            if (comment_end == end) return false;
            i = comment_end + 2;
            continue;
        }
        if (c == '@' && starts(src, i, end, "@//")) {
            const std::uint32_t line_end = find_in(src, "\n", i, end);
            if (line_end == end) return false;
            i = line_end;
            continue;
        }
        if (c == open_char) ++depth;
        else if (c == close_char && --depth == 0) { close = i; return true; }
    }
    return false;
}

std::uint32_t statement_end(std::string_view src, std::uint32_t from, std::uint32_t end) {
    bool quoted = false;
    char quote = 0;
    int par = 0, br = 0, bc = 0;
    std::uint32_t i = from;
    for (; i < end; ++i) {
        const char c = src[i];
        if (quoted) { if (c == '\\' && i + 1 < end) ++i; else if (c == quote) quoted = false; continue; }
        if (c == '\'' || c == '"') { quoted = true; quote = c; continue; }
        if (c == '(') ++par; else if (c == ')') --par; else if (c == '[') ++br; else if (c == ']') --br;
        else if (c == '{') ++bc; else if (c == '}') --bc;
        if (!par && !br && !bc && (c == ';' || c == '\n')) break;
    }
    return i;
}

} // namespace nift::rt::detail
