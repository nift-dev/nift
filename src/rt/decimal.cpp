#include "rt/decimal.h"

#include <charconv>
#include <cmath>
#include <limits>

namespace nift::rt {

namespace {

// Exponents in valid input are far below int64 limits; text exponents that
// would overflow are rejected as unparseable rather than saturated.
bool parse_exponent(std::string_view text, std::int64_t& exponent) {
    if (text.empty()) return false;
    std::size_t at = 0;
    bool negative = false;
    if (text[0] == '+' || text[0] == '-') { negative = text[0] == '-'; ++at; }
    if (at == text.size()) return false;
    std::int64_t value = 0;
    for (; at < text.size(); ++at) {
        if (text[at] < '0' || text[at] > '9') return false;
        if (value > (std::numeric_limits<std::int64_t>::max() / 4) / 10) return false;
        value = value * 10 + (text[at] - '0');
    }
    exponent = negative ? -value : value;
    return true;
}

} // namespace

bool parse_decimal(std::string_view text, DecimalForm& out) {
    std::size_t at = 0;
    int sign = 1;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) sign = text[at++] == '-' ? -1 : 1;
    const std::size_t integer_begin = at;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
    if (at == integer_begin) return false;
    std::string coefficient(text.substr(integer_begin, at - integer_begin));
    std::int64_t exponent = 0;
    if (at < text.size() && text[at] == '.') {
        const std::size_t fraction_begin = ++at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
        if (at == fraction_begin) return false;
        coefficient.append(text.substr(fraction_begin, at - fraction_begin));
        exponent -= static_cast<std::int64_t>(at - fraction_begin);
    }
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
        std::int64_t written = 0;
        if (!parse_exponent(text.substr(at + 1), written)) return false;
        exponent += written;
    } else if (at != text.size()) return false;
    const std::size_t first = coefficient.find_first_not_of('0');
    if (first == std::string::npos) { out = DecimalForm{}; return true; }
    coefficient.erase(0, first);
    while (coefficient.size() > 1 && coefficient.back() == '0') { coefficient.pop_back(); ++exponent; }
    out.cls = NumericClass::Finite;
    out.sign = sign;
    out.coefficient = std::move(coefficient);
    out.exponent = exponent;
    return true;
}

DecimalForm decimal_form_of_double(double d) {
    DecimalForm form;
    if (std::isnan(d)) { form.cls = NumericClass::NaN; return form; }
    if (std::isinf(d)) {
        form.cls = d < 0 ? NumericClass::NegativeInfinity : NumericClass::PositiveInfinity;
        return form;
    }
    char buffer[64];
    const auto r = std::to_chars(buffer, buffer + sizeof buffer, d, std::chars_format::general);
    parse_decimal(std::string_view(buffer, static_cast<std::size_t>(r.ptr - buffer)), form);
    return form;
}

DecimalForm decimal_form_of_int(std::int64_t i) {
    char buffer[32];
    const auto r = std::to_chars(buffer, buffer + sizeof buffer, i);
    DecimalForm form;
    parse_decimal(std::string_view(buffer, static_cast<std::size_t>(r.ptr - buffer)), form);
    return form;
}

int compare_decimal_forms(const DecimalForm& left, const DecimalForm& right) {
    if (left.cls != right.cls) return static_cast<int>(left.cls) < static_cast<int>(right.cls) ? -1 : 1;
    if (left.cls != NumericClass::Finite) return 0;
    if (left.sign != right.sign) return left.sign < right.sign ? -1 : 1;
    if (left.sign == 0) return 0;
    // Scientific exponent = exponent + digits - 1 decides magnitude first.
    const std::int64_t ls = left.exponent + static_cast<std::int64_t>(left.coefficient.size()) - 1;
    const std::int64_t rs = right.exponent + static_cast<std::int64_t>(right.coefficient.size()) - 1;
    int magnitude = 0;
    if (ls != rs) magnitude = ls < rs ? -1 : 1;
    else {
        const std::size_t n = std::max(left.coefficient.size(), right.coefficient.size());
        for (std::size_t i = 0; i < n && magnitude == 0; ++i) {
            const char a = i < left.coefficient.size() ? left.coefficient[i] : '0';
            const char b = i < right.coefficient.size() ? right.coefficient[i] : '0';
            if (a != b) magnitude = a < b ? -1 : 1;
        }
    }
    return left.sign < 0 ? -magnitude : magnitude;
}

bool decimal_integer_digits(const DecimalForm& form, std::size_t limit, std::string& digits) {
    if (form.cls != NumericClass::Finite) return false;
    if (form.sign == 0) { digits = "0"; return true; }
    if (form.exponent < 0) return false;
    if (static_cast<std::uint64_t>(form.exponent) > limit || form.coefficient.size() > limit ||
        static_cast<std::size_t>(form.exponent) > limit - form.coefficient.size()) return false;
    digits = form.coefficient;
    digits.append(static_cast<std::size_t>(form.exponent), '0');
    return true;
}

std::string decimal_fingerprint(const DecimalForm& form) {
    if (form.cls == NumericClass::NaN) return "nan";
    if (form.cls == NumericClass::NegativeInfinity) return "-inf";
    if (form.cls == NumericClass::PositiveInfinity) return "+inf";
    if (form.sign == 0) return "0";
    return std::string(form.sign < 0 ? "-" : "+") + form.coefficient + "e" + std::to_string(form.exponent);
}

} // namespace nift::rt
