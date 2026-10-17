#include "rt/number.h"

#include "rt/decimal.h"
#include "rt/string.h"

#include <charconv>
#include <cmath>
#include <limits>

namespace nift::rt {

double number_approx(const Value& number) {
    switch (number.kind()) {
        case Kind::Int: return static_cast<double>(number.as_int());
        case Kind::Double: return number.as_double();
        case Kind::Decimal: return decimal_approx(number);
        default: return 0.0;
    }
}

DecimalForm number_form(const Value& number) {
    switch (number.kind()) {
        case Kind::Int: return decimal_form_of_int(number.as_int());
        case Kind::Double: return decimal_form_of_double(number.as_double());
        default: {
            DecimalForm form;
            if (!parse_decimal(decimal_spelling(number), form)) return decimal_form_of_double(decimal_approx(number));
            return form;
        }
    }
}

bool number_is_zero(const Value& number) {
    switch (number.kind()) {
        case Kind::Int: return number.as_int() == 0;
        case Kind::Double: return number.as_double() == 0.0;
        case Kind::Decimal: {
            const DecimalForm form = number_form(number);
            return form.cls == NumericClass::Finite && form.sign == 0;
        }
        default: return false;
    }
}

bool number_is_integer(const Value& number) {
    switch (number.kind()) {
        case Kind::Int: return true;
        case Kind::Double: {
            const double d = number.as_double();
            return std::isfinite(d) && std::trunc(d) == d;
        }
        case Kind::Decimal: {
            const DecimalForm form = number_form(number);
            return form.cls == NumericClass::Finite && (form.sign == 0 || form.exponent >= 0);
        }
        default: return false;
    }
}

bool number_to_i64(const Value& number, std::int64_t& out) {
    switch (number.kind()) {
        case Kind::Int: out = number.as_int(); return true;
        case Kind::Double: {
            const double d = number.as_double();
            const double bound = 9223372036854775808.0;  // 2^63
            if (!std::isfinite(d) || std::trunc(d) != d || d < -bound || d >= bound) return false;
            out = static_cast<std::int64_t>(d);
            return true;
        }
        case Kind::Decimal: {
            const DecimalForm form = number_form(number);
            std::string digits;
            if (!decimal_integer_digits(form, 19, digits)) return false;
            const std::string_view maximum = form.sign < 0 ? "9223372036854775808" : "9223372036854775807";
            if (digits.size() > maximum.size() || (digits.size() == maximum.size() && digits > maximum)) return false;
            std::uint64_t magnitude = 0;
            std::from_chars(digits.data(), digits.data() + digits.size(), magnitude);
            if (form.sign >= 0) out = static_cast<std::int64_t>(magnitude);
            else out = magnitude == (std::uint64_t{1} << 63) ? std::numeric_limits<std::int64_t>::min()
                                                           : -static_cast<std::int64_t>(magnitude);
            return true;
        }
        default: return false;
    }
}

namespace {

bool integer_arithmetic(ArithOp op, std::int64_t a, std::int64_t b, Value& out, std::string& error) {
    std::int64_t r = 0;
    bool overflow = false;
    switch (op) {
        case ArithOp::Add: overflow = __builtin_add_overflow(a, b, &r); break;
        case ArithOp::Sub: overflow = __builtin_sub_overflow(a, b, &r); break;
        case ArithOp::Mul: overflow = __builtin_mul_overflow(a, b, &r); break;
        case ArithOp::Mod:
            if (b == 0) { error = "modulo by zero"; return false; }
            r = (a == std::numeric_limits<std::int64_t>::min() && b == -1) ? 0 : a % b;
            break;
        case ArithOp::Div: return false;
    }
    if (overflow) { error = "signed 64-bit integer overflow"; return false; }
    out = Value::integer(r);
    return true;
}

bool double_arithmetic(ArithOp op, const Value& left, const Value& right, Value& out, std::string& error) {
    const double l = number_approx(left), r = number_approx(right);
    double result = 0;
    switch (op) {
        case ArithOp::Add: result = l + r; break;
        case ArithOp::Sub: result = l - r; break;
        case ArithOp::Mul: result = l * r; break;
        case ArithOp::Div:
            if (number_is_zero(right)) { error = "division by zero"; return false; }
            result = l / r;
            break;
        case ArithOp::Mod:
            if (number_is_zero(right)) { error = "modulo by zero"; return false; }
            if (!number_is_integer(left) || !number_is_integer(right)) {
                error = "modulo requires integer-valued operands";
                return false;
            }
            result = std::fmod(l, r);
            break;
    }
    if (!std::isfinite(result)) { error = "arithmetic result is not finite"; return false; }
    out = Value::number(result);
    return true;
}

} // namespace

bool arithmetic(ArithOp op, const Value& left, const Value& right, Value& out, std::string& error) {
    if (op != ArithOp::Div) {
        if (left.is(Kind::Int) && right.is(Kind::Int))  // fast path
            return integer_arithmetic(op, left.as_int(), right.as_int(), out, error);
        std::int64_t a = 0, b = 0;
        if (number_to_i64(left, a) && number_to_i64(right, b)) return integer_arithmetic(op, a, b, out, error);
    }
    return double_arithmetic(op, left, right, out, error);
}

// Negation flips the sign exactly; INT64_MIN negates to an exact Decimal
// (legacy flips the StrNumber spelling the same way).
Value negate(const Value& number) {
    switch (number.kind()) {
        case Kind::Int:
            if (number.as_int() == std::numeric_limits<std::int64_t>::min())
                return make_decimal("9223372036854775808", 9223372036854775808.0);
            return Value::integer(-number.as_int());
        case Kind::Double: return Value::number(-number.as_double());
        case Kind::Decimal: {
            const std::string_view spelling = decimal_spelling(number);
            std::string flipped = !spelling.empty() && spelling.front() == '-' ? std::string(spelling.substr(1))
                                                                               : "-" + std::string(spelling);
            return make_decimal(std::move(flipped), -decimal_approx(number));
        }
        default: return number;
    }
}

} // namespace nift::rt
