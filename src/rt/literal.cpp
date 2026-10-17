#include "rt/literal.h"

#include "rt/string.h"

#include <cmath>
#include <cstdlib>
#include <string>

namespace nift::rt {

namespace {

constexpr std::int64_t kSafeInteger = 9007199254740992;  // 2^53

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
        if (text.size() > 16) return false;
        const std::int64_t value = std::strtoll(std::string(text).c_str(), nullptr, 10);
        if (value >= kSafeInteger) return false;
        out = Value::integer(value);
        return true;
    }
    if (text.find('.') == std::string_view::npos && text.find_first_of("eE") == std::string_view::npos) return false;
    int significant = 0;
    long exponent = 0;
    if (!scan_decimal(text, significant, exponent) || significant > 15 || exponent > 300 || exponent < -300)
        return false;
    const double value = std::strtod(std::string(text).c_str(), nullptr);
    if (!std::isfinite(value) || std::fabs(value) >= static_cast<double>(kSafeInteger)) return false;
    if (value == 0 && significant > 0) return false;  // underflow
    out = Value::number(value);
    return true;
}

bool decode_string_literal(std::string_view text, Value& out) {
    if (text.size() < 2 || (text.front() != '"' && text.front() != '\'') || text.back() != text.front()) return false;
    const std::string_view body = text.substr(1, text.size() - 2);
    for (char c : body) {
        const auto byte = static_cast<unsigned char>(c);
        if (c == '\\' || c == '$' || byte < 0x20 || byte == 0x7f) return false;
        if (c == text.front()) return false;
    }
    if (!valid_utf8(body)) return false;  // length() counts code points
    out = make_string(body);
    return true;
}

} // namespace nift::rt
