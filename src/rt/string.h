#pragma once
// Strings and other immutable scalar heap kinds (CP09): text, bytes, decimals.
#include "rt/value.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {

Value make_string(std::string_view text);
Value make_string(std::string&& text);
inline Value make_string(const char* text) { return make_string(std::string_view(text)); }
inline Value make_string(const std::string& text) { return make_string(std::string_view(text)); }
// Valid only while `value` (a String) is alive and unchanged.
std::string_view string_view_of(const Value& value);

Value make_bytes(std::vector<std::uint8_t> data);
const std::vector<std::uint8_t>& bytes_of(const Value& value);

// Exact decimal from its spelling (must be a finite JSON number) and nearest
// double; see DecimalObject.
Value make_decimal(std::string spelling, double approx);
std::string_view decimal_spelling(const Value& value);
double decimal_approx(const Value& value);

} // namespace nift::rt
