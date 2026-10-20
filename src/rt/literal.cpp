#include "rt/literal.h"

#include "rt/number.h"
#include "rt/string.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace nift::rt {

namespace {

bool all_digits(std::string_view text) {
    if (text.empty()) return false;
    for (char c : text)
        if (c < '0' || c > '9') return false;
    return true;
}

// Significant digits of the mantissa (leading zeros ignored) and the
// exponent value; false when the spelling is not [digits][.digits][e[+-]digits].
bool scan_decimal(std::string_view text, int& significant, long& exponent) {
    std::size_t p = 0;
    bool digits = false, leading = true;
    significant = 0;
    exponent = 0;
    for (; p < text.size() && ((text[p] >= '0' && text[p] <= '9') || text[p] == '.'); ++p) {
        if (text[p] == '.') continue;
        digits = true;
        if (text[p] != '0') leading = false;
        if (!leading) ++significant;
    }
    if (!digits) return false;
    if (p == text.size()) return true;
    if (text[p] != 'e' && text[p] != 'E') return false;
    ++p;
    bool negative = false;
    if (p < text.size() && (text[p] == '+' || text[p] == '-')) negative = text[p++] == '-';
    if (p >= text.size() || !all_digits(text.substr(p)) || text.size() - p > 3) return false;
    exponent = std::strtol(std::string(text.substr(p)).c_str(), nullptr, 10);
    if (negative) exponent = -exponent;
    return true;
}

// JSON's number grammar, which is also the shape legacy keeps as StrNumber.
bool json_number(std::string_view text) {
    std::size_t p = 0;
    if (p < text.size() && text[p] == '-') ++p;
    if (p >= text.size()) return false;
    if (text[p] == '0') {
        ++p;
        if (p < text.size() && text[p] >= '0' && text[p] <= '9') return false;
    } else if (text[p] >= '1' && text[p] <= '9') {
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') ++p;
    } else {
        return false;
    }
    if (p < text.size() && text[p] == '.') {
        ++p;
        const std::size_t start = p;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') ++p;
        if (p == start) return false;
    }
    if (p < text.size() && (text[p] == 'e' || text[p] == 'E')) {
        ++p;
        if (p < text.size() && (text[p] == '+' || text[p] == '-')) ++p;
        const std::size_t start = p;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9') ++p;
        if (p == start) return false;
    }
    return p == text.size();
}

bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        std::uint32_t code = 0;
        if (lead < 0x80) { ++i; continue; }
        if (lead >= 0xc2 && lead <= 0xdf) { extra = 1; code = lead & 0x1fu; }
        else if (lead >= 0xe0 && lead <= 0xef) { extra = 2; code = lead & 0x0fu; }
        else if (lead >= 0xf0 && lead <= 0xf4) { extra = 3; code = lead & 0x07u; }
        else return false;
        if (i + extra >= text.size()) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xc0u) != 0x80u) return false;
            code = (code << 6) | (next & 0x3fu);
        }
        const bool surrogate = code >= 0xd800 && code <= 0xdfff;
        if ((extra == 2 && (code < 0x800 || surrogate)) || (extra == 3 && (code < 0x10000 || code > 0x10ffff)))
            return false;
        i += extra + 1;
    }
    return true;
}

} // namespace

bool decode_number_literal(std::string_view text, Value& out) {
    if (all_digits(text)) {
        if (text.size() > 1 && text.front() == '0') return false;
        std::int64_t value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()) return false;
        out = Value::integer(value);
        return true;
    }
    if (text.find('.') == std::string_view::npos && text.find_first_of("eE") == std::string_view::npos) return false;
    int significant = 0;
    long exponent = 0;
    // Legacy parses a decimal literal as JSON (jsonic json.h, number scan) and
    // keeps its spelling as an exact StrNumber when a zero value has a
    // fraction (`0.0`, `0.00`, `0.0e5` print as written; review D1) or the
    // exponent magnitude is 300 or more. Other finite spellings have the same
    // value as a double.
    if (!scan_decimal(text, significant, exponent)) return false;
    const double value = std::strtod(std::string(text).c_str(), nullptr);
    if (!std::isfinite(value)) return false;  // includes 1e309 (L55)
    const bool exact = (value == 0.0 && text.find('.') != std::string_view::npos) ||
                       exponent >= 300 || exponent <= -300;
    if (exact) {
        if (!json_number(text)) return false;  // non-JSON spellings stay refused
        out = make_decimal(std::string(text), value);
    } else {
        out = Value::number(value);
    }
    return true;
}

NegativeLiteral decode_negative_number_literal(std::string_view text, Value& out) {
    if (text == "9223372036854775808") {  // rt-lint: allow-string-compare compile time
        out = Value::integer(std::numeric_limits<std::int64_t>::min());
        return NegativeLiteral::Decoded;
    }
    Value value;
    if (!decode_number_literal(text, value)) return NegativeLiteral::Invalid;
    if (value.is(Kind::Int)) {
        if (value.as_int() == 0) return NegativeLiteral::Zero;
        out = Value::integer(-value.as_int());
        return NegativeLiteral::Decoded;
    }
    if (value.is(Kind::Double)) {
        // An exponent-only negative zero (`-0e5`) keeps a distinct legacy
        // spelling and is outside this decoder's admitted forms.
        if (value.as_double() == 0) return NegativeLiteral::Invalid;
        out = Value::number(-value.as_double());
        return NegativeLiteral::Decoded;
    }
    if (value.is(Kind::Decimal)) {
        out = negate(value);
        return NegativeLiteral::Decoded;
    }
    return NegativeLiteral::Invalid;
}

bool decode_string_literal(std::string_view text, Value& out) {
    if (text.size() < 2 || (text.front() != '"' && text.front() != '\'') || text.back() != text.front()) return false;
    const std::string_view body = text.substr(1, text.size() - 2);
    std::string decoded;
    decoded.reserve(body.size());
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '\\') {
            if (++i >= body.size()) return false;  // trailing backslash
            switch (body[i]) {
                case 'n': decoded.push_back('\n'); break;
                case 't': decoded.push_back('\t'); break;
                case 'r': decoded.push_back('\r'); break;
                case '\\': decoded.push_back('\\'); break;
                case '"': decoded.push_back('"'); break;
                case '\'': decoded.push_back('\''); break;
                default:
#if defined(NIFT_INTERP_MUTANT) && NIFT_INTERP_MUTANT == 2
                    break;  // negative control: drop the escape payload
#else
                    decoded.push_back(body[i]);  // legacy drops the backslash
                    break;
#endif
            }
            continue;
        }
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f) return false;  // raw control/NUL: later class
        if (c == '$' && i + 1 < body.size() && body[i + 1] == '[') return false;  // parser emits parts
        if (c == text.front()) return false;
        decoded.push_back(c);
    }
    if (!valid_utf8(decoded)) return false;  // length() counts code points
    out = make_string(static_cast<std::string&&>(decoded));
    return true;
}

} // namespace nift::rt
