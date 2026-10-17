#pragma once
// Exact decimal forms (CP09): the canonical comparison domain for numbers.
// Ported from legacy RuntimeValue.cpp (NumericForm) so equality, ordering and
// hashing agree with the legacy engine exactly, including its rule that a
// double compares by its shortest round-trip decimal spelling.
#include <cstdint>
#include <string>
#include <string_view>

namespace nift::rt {

enum class NumericClass : std::uint8_t { NegativeInfinity, Finite, PositiveInfinity, NaN };

// sign * coefficient * 10^exponent, normalised: the coefficient has no
// leading or trailing zeros; zero has sign 0, coefficient "0", exponent 0.
//
// Exponents are exact at any size, as in legacy (whose exponent is an
// arbitrary-precision decimal). |exponent| <= kSmallExponentLimit is stored in
// `exponent` with `big_exponent` empty. A larger exponent (only reachable from
// a JSON spelling such as "1e-99999999999999999999") is stored exactly as
// signed decimal text in `big_exponent`, and `exponent` then holds
// +/-(kSmallExponentLimit + 1) so that sign tests on `exponent` stay correct.
// The split is canonical (one representation per value), so fingerprints of
// equal forms are identical.
struct DecimalForm {
    static constexpr std::int64_t kSmallExponentLimit = 1000000000000000000;  // 10^18
    NumericClass cls = NumericClass::Finite;
    int sign = 0;
    std::string coefficient = "0";
    std::int64_t exponent = 0;
    std::string big_exponent;  // empty unless |exponent| > kSmallExponentLimit
};

bool parse_decimal(std::string_view text, DecimalForm& out);  // finite JSON-number syntax only
DecimalForm decimal_form_of_double(double d);
DecimalForm decimal_form_of_int(std::int64_t i);
int compare_decimal_forms(const DecimalForm& left, const DecimalForm& right);
// Integer digits of an integral finite form if it has at most `limit` digits.
bool decimal_integer_digits(const DecimalForm& form, std::size_t limit, std::string& digits);
// Stable text of the form ("0", "+12e-1", "nan", ...) used for hashing.
std::string decimal_fingerprint(const DecimalForm& form);

} // namespace nift::rt
