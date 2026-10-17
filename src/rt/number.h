#pragma once
// Numeric tower (CP09): Int (int64), Double, Decimal (exact spelling).
//
// Arithmetic follows the legacy canonical (textual) evaluator exactly
// (`numeric_binary` in ParserExpression.cpp; semantics.md "Integers",
// "Division"):
//   * `+ - * %` with both operands exactly representable as int64 (Int, an
//     integral Double in [-2^63, 2^63), or an integral Decimal in range) use
//     checked int64 arithmetic: overflow is the error
//     "signed 64-bit integer overflow"; `x % 0` is "modulo by zero";
//     INT64_MIN % -1 is 0.
//   * otherwise, and always for `/`, the operands' nearest doubles are used;
//     `/` and `%` by zero are errors; `%` needs integer-valued operands; a
//     non-finite result is "arithmetic result is not finite".
// Note: the legacy *prepared* (loop) AST path computes `+ - *` in doubles
// without overflow checks; Runtime 2.0 follows the canonical path (D-009).
#include "rt/decimal.h"
#include "rt/value.h"

#include <cstdint>
#include <string>

namespace nift::rt {

enum class ArithOp : std::uint8_t { Add, Sub, Mul, Div, Mod };

// Operands must both be numbers (callers report the non-numeric errors).
bool arithmetic(ArithOp op, const Value& left, const Value& right, Value& out, std::string& error);
Value negate(const Value& number);

double number_approx(const Value& number);
// Exact decimal form (legacy numeric_form): Int exactly, Double by its
// shortest round-trip spelling, Decimal by its spelling at any exponent size
// (a spelling that is not a finite JSON number falls back to its double).
DecimalForm number_form(const Value& number);
bool number_is_zero(const Value& number);
bool number_is_integer(const Value& number);
// Exact conversion (legacy runtime_number_to_i64); false if not an integer in
// int64 range.
bool number_to_i64(const Value& number, std::int64_t& out);

} // namespace nift::rt
