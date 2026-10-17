#include "rt/prepared_parse.h"

// rt-lint: cold-file compile-time port of legacy's prepared parser (accepts only)

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <string>

namespace nift::rt {

namespace {

bool space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
bool alpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool digit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool alnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// Mirror of struct P in src/Ast.cpp: every function returns false exactly
// where the original returns an empty node.
struct Recogniser {
    const std::string& s;
    std::size_t p = 0;
    int depth = 0;

    struct Guard {
        int& d;
        explicit Guard(int& x) : d(x) { ++d; }
        ~Guard() { --d; }
    };

    void ws() { while (p < s.size() && space(s[p])) ++p; }
    bool take(std::string_view x) {
        ws();
        if (s.compare(p, x.size(), x) == 0) { p += x.size(); return true; }
        return false;
    }
    // Quote-aware scan of a balanced (...) group starting at s[p] == '('.
    bool call_group() {
        int d = 0;
        bool q = false;
        char qc = 0;
        do {
            const char ch = s[p++];
            if (q) {
                if (ch == '\\' && p < s.size()) ++p;
                else if (ch == qc) q = false;
            } else if (ch == '\'' || ch == '"') {
                q = true;
                qc = ch;
            } else if (ch == '(') {
                ++d;
            } else if (ch == ')') {
                --d;
            }
        } while (p < s.size() && d > 0);
        return d == 0;
    }
    bool identifier_tail() {
        ++p;
        while (p < s.size() && (alnum(s[p]) || s[p] == '_')) ++p;
        return true;
    }
    bool member_name() {
        ws();
        if (p >= s.size() || !(alpha(s[p]) || s[p] == '_')) return false;
        return identifier_tail();
    }

    bool array_literal() {
        ++p;
        ws();
        if (p < s.size() && s[p] == ']') { ++p; return true; }
        for (;;) {
            if (!coalesce()) return false;
            ws();
            if (p < s.size() && s[p] == ',') { ++p; continue; }
            if (p < s.size() && s[p] == ']') { ++p; return true; }
            return false;
        }
    }
    bool object_literal() {
        ++p;
        ws();
        if (p < s.size() && s[p] == '}') { ++p; return true; }
        for (;;) {
            ws();
            if (p >= s.size() || s[p] != '"') return false;
            ++p;
            bool escaped = false;
            while (p < s.size() && s[p] != '"') {
                if (s[p] == '\\') escaped = true;
                ++p;
            }
            if (p >= s.size()) return false;
            ++p;
            if (escaped) return false;
            ws();
            if (p >= s.size() || s[p] != ':') return false;
            ++p;
            ws();
            if (p < s.size() && (s[p] == '"' || s[p] == '\'')) return false;
            if (!coalesce()) return false;
            ws();
            if (p < s.size() && s[p] == ',') { ++p; continue; }
            if (p < s.size() && s[p] == '}') { ++p; return true; }
            return false;
        }
    }
    bool string_literal() {
        const char q = s[p++];
        while (p < s.size() && s[p] != q) {
            if (s[p] == '\\' && p + 1 < s.size()) ++p;
            ++p;
        }
        if (p >= s.size()) return false;
        ++p;
        return true;
    }
    bool number(std::size_t b) {
        char* e = nullptr;
        std::strtod(s.c_str() + p, &e);
        if (e == s.c_str() + p) return false;
        p = static_cast<std::size_t>(e - s.c_str());
        bool integral = digit(s[b]) || (s[b] == '-' && b + 1 < s.size() && digit(s[b + 1]));
        if (!integral) return true;
        for (std::size_t z = s[b] == '-' ? b + 1 : b; z < p; ++z)
            if (!digit(s[z])) return true;
        errno = 0;
        char* le = nullptr;
        const long long lv = std::strtoll(s.c_str() + b, &le, 10);
        return !(errno == ERANGE || (le == e && (lv > 9007199254740992LL || lv < -9007199254740992LL)));
    }
    bool primary() {
        Guard g(depth);
        if (depth > 128) return false;
        ws();
        const std::size_t b = p;
        if (p >= s.size()) return false;
        if (s[p] == '(') {
            ++p;
            if (!logical_or()) return false;
            ws();
            if (p >= s.size() || s[p] != ')') return false;
            ++p;
            return true;
        }
        if (s[p] == '[') return array_literal();
        if (s[p] == '{') return object_literal();
        if (s[p] == '\'' || s[p] == '"') return string_literal();
        if (digit(s[p]) || (s[p] == '.' && p + 1 < s.size() && digit(s[p + 1]))) return number(b);
        if (alpha(s[p]) || s[p] == '_') {
            identifier_tail();
            ws();
            if (p < s.size() && s[p] == '(') return call_group();
            return true;
        }
        return false;
    }
    bool postfix() {
        if (!primary()) return false;
        for (;;) {
            ws();
            if (p + 1 < s.size() && s[p] == '?' && s[p + 1] == '.') {
                p += 2;
                if (!member_name()) return false;
                continue;
            }
            if (p + 1 < s.size() && s[p] == '?' && s[p + 1] == '[') {
                p += 2;
                const bool index = coalesce();
                ws();
                if (!index || p >= s.size() || s[p] != ']') return false;
                ++p;
                continue;
            }
            if (p < s.size() && s[p] == '.') {
                ++p;
                if (!member_name()) return false;
                continue;
            }
            if (p < s.size() && s[p] == '[') {
                ++p;
                const bool index = coalesce();
                ws();
                if (!index || p >= s.size() || s[p] != ']') return false;
                ++p;
                continue;
            }
            if (p < s.size() && s[p] == '(') {
                if (!call_group()) return false;
                continue;
            }
            return true;
        }
    }
    bool unary() {
        Guard g(depth);
        if (depth > 128) return false;
        ws();
        if (take("!") || take("-") || take("+")) return unary();
        return postfix();
    }
    template <bool (Recogniser::*Next)()> bool chain(std::initializer_list<std::string_view> ops) {
        if (!(this->*Next)()) return false;
        for (;;) {
            ws();
            std::size_t length = 0;
            for (const auto op : ops)
                if (s.compare(p, op.size(), op) == 0) { length = op.size(); break; }
            if (!length) return true;
            p += length;
            if (!(this->*Next)()) return false;
        }
    }
    bool mul() { return chain<&Recogniser::unary>({"*", "/", "%"}); }
    bool add() { return chain<&Recogniser::mul>({"+", "-"}); }
    // One comparison at most: `a < b == c` is rejected (legacy parses no chains).
    bool compare() {
        if (!add()) return false;
        ws();
        for (const std::string_view op : {"==", "!=", "<=", ">=", "<", ">"})
            if (s.compare(p, op.size(), op) == 0) { p += op.size(); return add(); }
        return true;
    }
    bool logical_and() {
        if (!compare()) return false;
        while (take("&&"))
            if (!compare()) return false;
        return true;
    }
    bool logical_or() {
        if (!logical_and()) return false;
        while (take("||"))
            if (!logical_and()) return false;
        return true;
    }
    bool coalesce() {
        if (!logical_or()) return false;
        if (take("??")) return coalesce();
        return true;
    }
};

std::string trimmed(std::string_view x) {
    const auto b = x.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    const auto e = x.find_last_not_of(" \t\r\n");
    return std::string(x.substr(b, e - b + 1));
}

// parse_bounded_expression's size/nesting bound (non-payload form).
bool within_bounds(const std::string& source) {
    std::size_t depth = 0, max_depth = 0;
    bool q = false;
    char qc = 0;
    for (std::size_t i = 0; i < source.size(); ++i) {
        const char c = source[i];
        if (q) {
            if (c == '\\') ++i;
            else if (c == qc) q = false;
            continue;
        }
        if (c == '\'' || c == '"') { q = true; qc = c; continue; }
        if (c == '(' || c == '[' || c == '{') {
            if (++depth > max_depth) max_depth = depth;
        } else if ((c == ')' || c == ']' || c == '}') && depth) {
            --depth;
        }
    }
    return source.size() <= 8192 && max_depth <= 256;
}

} // namespace

bool legacy_prepared_parse_accepts(std::string_view text) {
    const std::string source(text);
    if (!within_bounds(source)) return false;
    const auto arrow = source.find("=>");
    if (arrow != std::string::npos) {
        const std::string lhs = trimmed(std::string_view(source).substr(0, arrow));
        if (lhs.size() >= 2 && lhs.front() == '(' && lhs.back() == ')') return true;  // a lambda node
    }
    Recogniser r{source};
    if (!r.coalesce()) return false;
    r.ws();
    return r.p == source.size();
}

} // namespace nift::rt
