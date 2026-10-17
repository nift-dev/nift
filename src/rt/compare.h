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

#include <cstddef>
#include <cstdint>
#include <string>

namespace nift::rt {

bool equal(const Value& left, const Value& right);
// Depth-bounded equality for callers that must not decide beyond a nesting
// depth (the VM bails there; legacy's recursive equality crashes on deep
// enough values). TooDeep: two aggregates at `max_depth` still had to be
// compared. equal() is unbounded; both are iterative (no C++ recursion).
enum class Equality : std::uint8_t { Unequal, Equal, TooDeep };
Equality equal_within(const Value& left, const Value& right, std::size_t max_depth);
// Relational order for two numbers or two strings; false when not comparable
// (other kinds, or NaN), in which case `<`, `<=`, `>`, `>=` are all false.
bool compare_relational(const Value& left, const Value& right, int& comparison);
bool truthy(const Value& value);
std::uint64_t hash(const Value& value);

} // namespace nift::rt
