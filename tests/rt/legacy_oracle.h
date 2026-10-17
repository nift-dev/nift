#pragma once
// Legacy semantic oracle for CP09 differential tests: the canonical numeric
// operator of the legacy engine, copied from `numeric_binary` in
// src/ParserExpression.cpp (minus atomic-handle unwrapping) so the rt numeric
// tower can be compared against it value by value.
#include "RuntimeValue.h"

#include <cmath>
#include <limits>
#include <string>

namespace legacy_oracle {

inline bool numeric_binary(const nift::RuntimeValue& left, const nift::RuntimeValue& right, char op,
                           nift::RuntimeValue& result, std::string& error) {
    std::int64_t a = 0, b = 0;
    if (nift::runtime_number_to_i64(left, a) && nift::runtime_number_to_i64(right, b) && op != '/') {
        std::int64_t r = 0;
        bool overflow = false;
        if (op == '+') overflow = __builtin_add_overflow(a, b, &r);
        else if (op == '-') overflow = __builtin_sub_overflow(a, b, &r);
        else if (op == '*') overflow = __builtin_mul_overflow(a, b, &r);
        else if (op == '%') {
            if (b == 0) { error = "modulo by zero"; return false; }
            if (a == std::numeric_limits<std::int64_t>::min() && b == -1) r = 0;
            else r = a % b;
        } else return false;
        if (overflow) { error = "signed 64-bit integer overflow"; return false; }
        result = nift::runtime_integer(r);
        return true;
    }
    if (!left.is_number() || !right.is_number()) { error = "arithmetic operators require numeric operands"; return false; }
    double r = 0;
    if (op == '+') r = left.num + right.num;
    else if (op == '-') r = left.num - right.num;
    else if (op == '*') r = left.num * right.num;
    else if (op == '/') {
        if (nift::runtime_number_is_zero(right)) { error = "division by zero"; return false; }
        r = left.num / right.num;
    } else if (op == '%') {
        if (nift::runtime_number_is_zero(right)) { error = "modulo by zero"; return false; }
        if (!nift::runtime_number_is_integer(left) || !nift::runtime_number_is_integer(right)) {
            error = "modulo requires integer-valued operands";
            return false;
        }
        r = std::fmod(left.num, right.num);
    }
    if (!std::isfinite(r)) { error = "arithmetic result is not finite"; return false; }
    result = nift::RuntimeValue(r);
    return true;
}

} // namespace legacy_oracle
