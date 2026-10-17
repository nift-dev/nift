#include "rt/decimal.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace nift::rt {

namespace {

// Signed decimal integers as text ("0", "123", "-45"; no leading zeros). Only
// exponents beyond DecimalForm::kSmallExponentLimit use this (cold path).
struct BigInt {
    int sign = 0;
    std::string digits = "0";
};

BigInt big_of(std::int64_t value) {
    BigInt out;
    if (value == 0) return out;
    out.sign = value < 0 ? -1 : 1;
    const std::uint64_t bits = static_cast<std::uint64_t>(value);
    const std::uint64_t magnitude = value < 0 ? 0 - bits : bits;
    out.digits = std::to_string(magnitude);
    return out;
}

int compare_magnitude(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return a.size() < b.size() ? -1 : 1;
    const int c = a.compare(b);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

std::string add_magnitude(const std::string& a, const std::string& b) {
    std::string out;
    int carry = 0;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const int x = i < a.size() ? a[a.size() - 1 - i] - '0' : 0;
        const int y = i < b.size() ? b[b.size() - 1 - i] - '0' : 0;
        out.push_back(static_cast<char>('0' + (x + y + carry) % 10));
        carry = (x + y + carry) / 10;
    }
    if (carry) out.push_back('1');
    return std::string(out.rbegin(), out.rend());
}

std::string subtract_magnitude(const std::string& larger, const std::string& smaller) {
    std::string out;
    int borrow = 0;
    for (std::size_t i = 0; i < larger.size(); ++i) {
        int x = larger[larger.size() - 1 - i] - '0' - borrow;
        const int y = i < smaller.size() ? smaller[smaller.size() - 1 - i] - '0' : 0;
        borrow = x < y ? 1 : 0;
        out.push_back(static_cast<char>('0' + x + borrow * 10 - y));
    }
    while (out.size() > 1 && out.back() == '0') out.pop_back();
    return std::string(out.rbegin(), out.rend());
}

BigInt big_add(BigInt a, const BigInt& b) {
    if (a.sign == 0) return b;
    if (b.sign == 0) return a;
    if (a.sign == b.sign) { a.digits = add_magnitude(a.digits, b.digits); return a; }
    const int c = compare_magnitude(a.digits, b.digits);
    if (c == 0) return BigInt{};
    if (c > 0) a.digits = subtract_magnitude(a.digits, b.digits);
    else { a.digits = subtract_magnitude(b.digits, a.digits); a.sign = b.sign; }
    return a;
}

int big_compare(const BigInt& a, const BigInt& b) {
    if (a.sign != b.sign) return a.sign < b.sign ? -1 : 1;
    if (a.sign == 0) return 0;
    const int c = compare_magnitude(a.digits, b.digits);
    return a.sign < 0 ? -c : c;
}

std::string big_text(const BigInt& value) { return value.sign < 0 ? "-" + value.digits : value.digits; }

BigInt exponent_of(const DecimalForm& form) {
    if (form.big_exponent.empty()) return big_of(form.exponent);
    BigInt out;
    out.sign = form.big_exponent.front() == '-' ? -1 : 1;
    out.digits = form.big_exponent.substr(out.sign < 0 ? 1 : 0);
    return out;
}

// Stores `value` in the canonical small/big split.
void set_exponent(DecimalForm& form, const BigInt& value) {
    const std::string limit = std::to_string(DecimalForm::kSmallExponentLimit);
    if (compare_magnitude(value.digits, limit) <= 0) {
        std::int64_t small = 0;
        std::from_chars(value.digits.data(), value.digits.data() + value.digits.size(), small);
        form.exponent = value.sign < 0 ? -small : small;
        form.big_exponent.clear();
        return;
    }
    form.exponent = value.sign * (DecimalForm::kSmallExponentLimit + 1);
    form.big_exponent = big_text(value);
}

// Exponent text after 'e': optional sign, one or more digits (any count).
bool parse_exponent(std::string_view text, BigInt& exponent) {
    std::size_t at = 0;
    int sign = 1;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) sign = text[at++] == '-' ? -1 : 1;
    if (at == text.size()) return false;
    for (std::size_t i = at; i < text.size(); ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    while (at + 1 < text.size() && text[at] == '0') ++at;
    exponent.digits = std::string(text.substr(at));
    exponent.sign = exponent.digits.size() == 1 && exponent.digits[0] == '0' ? 0 : sign;
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
    std::int64_t adjust = 0;  // bounded by the text length
    if (at < text.size() && text[at] == '.') {
        const std::size_t fraction_begin = ++at;
        while (at < text.size() && text[at] >= '0' && text[at] <= '9') ++at;
        if (at == fraction_begin) return false;
        coefficient.append(text.substr(fraction_begin, at - fraction_begin));
        adjust -= static_cast<std::int64_t>(at - fraction_begin);
    }
    BigInt written;
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
        if (!parse_exponent(text.substr(at + 1), written)) return false;
    } else if (at != text.size()) return false;
    const std::size_t first = coefficient.find_first_not_of('0');
    if (first == std::string::npos) { out = DecimalForm{}; return true; }  // zero, whatever the exponent
    coefficient.erase(0, first);
    while (coefficient.size() > 1 && coefficient.back() == '0') { coefficient.pop_back(); ++adjust; }
    out.cls = NumericClass::Finite;
    out.sign = sign;
    out.coefficient = std::move(coefficient);
    if (written.digits.size() <= 18) {  // fast path: |written| < 10^18 fits int64 with the adjustment
        std::int64_t small = 0;
        std::from_chars(written.digits.data(), written.digits.data() + written.digits.size(), small);
        const std::int64_t exponent = written.sign * small + adjust;
        if (exponent >= -DecimalForm::kSmallExponentLimit && exponent <= DecimalForm::kSmallExponentLimit) {
            out.exponent = exponent;
            out.big_exponent.clear();
            return true;
        }
    }
    set_exponent(out, big_add(written, big_of(adjust)));
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
    int magnitude = 0;
    if (left.big_exponent.empty() && right.big_exponent.empty()) {
        const std::int64_t ls = left.exponent + static_cast<std::int64_t>(left.coefficient.size()) - 1;
        const std::int64_t rs = right.exponent + static_cast<std::int64_t>(right.coefficient.size()) - 1;
        if (ls != rs) magnitude = ls < rs ? -1 : 1;
    } else {
        const BigInt ls = big_add(exponent_of(left), big_of(static_cast<std::int64_t>(left.coefficient.size()) - 1));
        const BigInt rs = big_add(exponent_of(right), big_of(static_cast<std::int64_t>(right.coefficient.size()) - 1));
        magnitude = big_compare(ls, rs);
    }
    if (magnitude == 0) {
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
    if (form.exponent < 0 || !form.big_exponent.empty()) return false;  // fraction, or too many digits
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
    const std::string exponent = form.big_exponent.empty() ? std::to_string(form.exponent) : form.big_exponent;
    return std::string(form.sign < 0 ? "-" : "+") + form.coefficient + "e" + exponent;
}

} // namespace nift::rt
