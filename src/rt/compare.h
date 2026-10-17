#pragma once
// Equality, ordering, truthiness and hashing (CP09), matching legacy
// runtime_equal / runtime_compare_numbers_relational / runtime_truthy:
//   * numbers compare by exact decimal value across Int, Double and Decimal
//     (1 == 1.0 == "1.00" as a JSON decimal); NaN equals nothing;
//   * strings and bytes compare bytewise; arrays elementwise; objects as
//     unordered sets of (key, value) entries; errors structurally;
//   * functions, structs and handles compare by identity;
//   * values of different kinds are unequal (`"1" == 1` is false).
// hash(a) == hash(b) whenever equal(a, b).
#include "rt/value.h"

#include <cstdint>
#include <string>

namespace nift::rt {

bool equal(const Value& left, const Value& right);
// Relational order for two numbers or two strings; false when not comparable
// (other kinds, or NaN), in which case `<`, `<=`, `>`, `>=` are all false.
bool compare_relational(const Value& left, const Value& right, int& comparison);
bool truthy(const Value& value);
std::uint64_t hash(const Value& value);

} // namespace nift::rt
