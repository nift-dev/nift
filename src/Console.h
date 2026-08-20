#pragma once

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <algorithm>
#include <cctype>
#include <string_view>

#if defined(_WIN32)
    #include <io.h>
    #define NIFT_ISATTY _isatty
    #define NIFT_FILENO _fileno
#else
    #include <unistd.h>
    #define NIFT_ISATTY isatty
    #define NIFT_FILENO fileno
#endif

namespace console {
inline std::mutex output_mutex;
inline std::atomic<bool> plain_output{false};

inline bool stdout_is_tty() {
    return !plain_output.load(std::memory_order_relaxed) && NIFT_ISATTY(NIFT_FILENO(stdout)) != 0;
}

inline bool stderr_is_tty() {
    return !plain_output.load(std::memory_order_relaxed) && NIFT_ISATTY(NIFT_FILENO(stderr)) != 0;
}

class ScopedPlainOutput {
public:
    ScopedPlainOutput()
        : previous_(plain_output.exchange(true, std::memory_order_relaxed)) {}

    ~ScopedPlainOutput() {
        plain_output.store(previous_, std::memory_order_relaxed);
    }

    ScopedPlainOutput(const ScopedPlainOutput&) = delete;
    ScopedPlainOutput& operator=(const ScopedPlainOutput&) = delete;

private:
    bool previous_;
};

inline std::string paint(const std::string& text, const char* code, bool enabled) {
    return enabled ? std::string("\033[") + code + "m" + text + "\033[0m" : text;
}

inline std::string path(const std::string& text, bool stderr_stream = false) {
    return paint(text, "36", stderr_stream ? stderr_is_tty() : stdout_is_tty());
}

inline std::string diagnostic_directive(const std::string& text, bool enabled = stderr_is_tty()) {
    return paint(text, "1;35", enabled);
}

inline std::string diagnostic_offender(const std::string& text, bool enabled = stderr_is_tty()) {
    return paint(text, "1;31", enabled);
}

inline std::size_t display_width(std::string_view text, std::size_t tab_width = 8) {
    std::size_t columns = 0;
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '\t') {
            const std::size_t step = tab_width ? tab_width - (columns % tab_width) : 1;
            columns += step;
            ++i;
            continue;
        }
        if (c < 0x20 || c == 0x7f) { ++i; continue; }
        if (c < 0x80) { ++columns; ++i; continue; }
        ++columns;
        ++i;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) ++i;
    }
    return columns;
}

inline std::string expand_tabs(std::string_view text, std::size_t tab_width = 8) {
    std::string out;
    out.reserve(text.size());
    std::size_t columns = 0;
    for (char c : text) {
        if (c == '\t') {
            const std::size_t step = tab_width ? tab_width - (columns % tab_width) : 1;
            out.append(step, ' ');
            columns += step;
        } else {
            out.push_back(c);
            if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f &&
                (static_cast<unsigned char>(c) & 0xc0) != 0x80) ++columns;
        }
    }
    return out;
}


inline bool nift_function_char(unsigned char c) {
    return c >= 'a' && c <= 'z';
}

inline bool known_nift_function(std::string_view name) {
    // Keep this aligned with Parser's public @function surface. The diagnostic
    // lexer intentionally does not colour arbitrary @words such as CSS @media,
    // email addresses, or prose mentions that are not Nift functions.
    static constexpr std::string_view functions[] = {
        "content", "pathtopage",
        "filter", "map", "sort", "slice", "find", "some", "every",
        "distinct", "reverse", "sum", "prod", "min", "max", "reduce",
        "substr", "join", "input", "pathto", "pathtofile", "getenv",
        "ent", "json", "dep",
        "if", "for", "item", "paginate"
    };
    for (const auto function : functions) if (function == name) return true;
    return false;
}

inline std::size_t nift_function_token_end(std::string_view text, std::size_t at) {
    if (at >= text.size() || text[at] != '@' || at + 1 >= text.size() ||
        !nift_function_char(static_cast<unsigned char>(text[at + 1]))) return at;
    std::size_t end = at + 2;
    while (end < text.size() && nift_function_char(static_cast<unsigned char>(text[end]))) ++end;
    const std::string_view name = text.substr(at + 1, end - at - 1);
    return known_nift_function(name) ? end : at;
}

inline std::string diagnostic_value(const std::string& text, bool enabled = stderr_is_tty()) {
    return paint(text, "32", enabled);
}

inline std::string diagnostic_value_expr(const std::string& text, bool enabled = stderr_is_tty()) {
    return paint(text, "1;36", enabled);
}


inline std::string highlight_diagnostic_message(const std::string& message, bool enabled = stderr_is_tty()) {
    if (!enabled) return message;
    std::string out;
    out.reserve(message.size() + 64);
    const std::size_t detail = message.rfind(": ");
    for (std::size_t i = 0; i < message.size();) {
        const std::size_t function_end = nift_function_token_end(message, i);
        if (function_end != i) {
            out += diagnostic_directive(message.substr(i, function_end - i), true);
            i = function_end;
            continue;
        }
        if (detail != std::string::npos && i == detail + 2 && i < message.size()) {
            out += diagnostic_offender(message.substr(i), true);
            break;
        }
        out.push_back(message[i++]);
    }
    return out;
}

inline std::string highlight_nift_source(const std::string& line,
                                         std::size_t error_byte_start,
                                         std::size_t error_byte_length,
                                         bool enabled = stderr_is_tty()) {
    if (!enabled) return line;
    const std::size_t error_end = std::min(line.size(), error_byte_start + error_byte_length);
    std::string out;
    out.reserve(line.size() + 96);
    std::size_t nift_call_depth = 0;
    bool awaiting_nift_call_open = false;

    auto in_error = [&](std::size_t pos) {
        return pos >= error_byte_start && pos < error_end;
    };

    for (std::size_t i = 0; i < line.size();) {
        // Nift function names are recognized from the parser's current public
        // function surface, not from arbitrary lowercase @words. That keeps
        // CSS @media and ordinary email/prose syntax untouched.
        const std::size_t function_end = nift_function_token_end(line, i);
        if (function_end != i) {
            out += diagnostic_directive(line.substr(i, function_end - i), true);
            i = function_end;
            awaiting_nift_call_open = true;
            continue;
        }

        if (awaiting_nift_call_open) {
            if (line[i] == '(') {
                if (in_error(i)) out += diagnostic_offender("(", true);
                else out.push_back('(');
                ++i;
                ++nift_call_depth;
                awaiting_nift_call_open = false;
                continue;
            }
            // Current Nift calls use immediate parentheses; @content and
            // control tokens without a call opener simply leave this state.
            awaiting_nift_call_open = false;
        }

        // Once inside a Nift call, track nested parentheses so strings remain
        // Nift values throughout collection/condition expressions.
        if (nift_call_depth && line[i] == '(') {
            if (in_error(i)) out += diagnostic_offender("(", true);
            else out.push_back('(');
            ++nift_call_depth;
            ++i;
            continue;
        }
        if (nift_call_depth && line[i] == ')') {
            if (in_error(i)) out += diagnostic_offender(")", true);
            else out.push_back(')');
            --nift_call_depth;
            ++i;
            continue;
        }

        // Value expressions are first-class Nift syntax in both HTML and Nift
        // call arguments. Preserve their colour unless the whole token is the
        // offending span, where error colour intentionally wins.
        if (line[i] == '$' && i + 1 < line.size() && line[i + 1] == '[') {
            const auto close = line.find(']', i + 2);
            const std::size_t end = close == std::string::npos ? i + 2 : close + 1;
            if (i >= error_byte_start && end <= error_end)
                out += diagnostic_offender(line.substr(i, end - i), true);
            else
                out += diagnostic_value_expr(line.substr(i, end - i), true);
            i = end;
            continue;
        }

        // Colour quotes only while lexically inside a Nift @function call.
        // This deliberately leaves ordinary HTML attributes such as class="x"
        // alone while still highlighting @input('x') / @pathto("x").
        if (nift_call_depth && (line[i] == '\'' || line[i] == '"')) {
            const char quote = line[i];
            std::size_t end = i + 1;
            bool escaped = false;
            bool closed = false;
            while (end < line.size()) {
                const char c = line[end++];
                if (escaped) { escaped = false; continue; }
                if (c == '\\') { escaped = true; continue; }
                if (c == quote) { closed = true; break; }
            }
            if (!closed) {
                if (in_error(i)) out += diagnostic_offender(std::string(1, line[i]), true);
                else out.push_back(line[i]);
                ++i;
                continue;
            }
            if (in_error(i) || (end > i && in_error(end - 1)))
                out += diagnostic_offender(line.substr(i, end - i), true);
            else
                out += diagnostic_value(line.substr(i, end - i), true);
            i = end;
            continue;
        }

        if (in_error(i)) {
            std::size_t end = i + 1;
            while (end < error_end) {
                if (nift_function_token_end(line, end) != end) break;
                if (line[end] == '$' && end + 1 < line.size() && line[end + 1] == '[') break;
                if (nift_call_depth && (line[end] == '\'' || line[end] == '"' ||
                                        line[end] == '(' || line[end] == ')')) break;
                ++end;
            }
            out += diagnostic_offender(line.substr(i, end - i), true);
            i = end;
            continue;
        }

        out.push_back(line[i++]);
    }
    return out;
}
inline std::string good(const std::string& text) { return paint(text, "32", stdout_is_tty()); }
inline std::string dim(const std::string& text) { return paint(text, "2", stdout_is_tty()); }
inline std::string heading(const std::string& text) { return paint(text, "1;35", stdout_is_tty()); }
inline std::string json_key(const std::string& text) { return paint(text, "1;34", stdout_is_tty()); }
inline std::string json_string(const std::string& text) { return paint(text, "32", stdout_is_tty()); }
inline std::string json_number(const std::string& text) { return paint(text, "36", stdout_is_tty()); }
inline std::string json_literal(const std::string& text) { return paint(text, "35", stdout_is_tty()); }
inline std::string error_label() { return paint("error:", "1;31", stderr_is_tty()); }
inline std::string warning_label() { return paint("warning:", "1;33", stderr_is_tty()); }

inline void error(const std::string& message) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cerr << error_label() << ' ' << message << '\n';
}
} // namespace console
