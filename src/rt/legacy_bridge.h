#pragma once
// Conversions between rt::Value and the legacy nift::RuntimeValue (CP09), for
// whole-unit fallback and bridging while both engines coexist.
//
// legacy -> rt: Number -> Double (exact bits, including -0.0); StrNumber ->
// Decimal; String -> String (legacy "\x1fnift:" handle strings stay strings:
// typed handles arrive with CP17); Array, Object; Bytes; Timer ->
// Handle(Timer, instance, owner); Error -> Error (cause chain kept).
// A legacy object with a duplicate key fails to convert: legacy treats such an
// object as a multiset (size, iteration and equality count both entries, lookup
// sees the first), which a unique-key rt object cannot represent. No script
// path builds one (JSON parsing, object literals, front matter, from_entries
// and index_by all reject duplicates); only host C++ that appends to
// RuntimeValue::object directly can.
// rt -> legacy: Int -> legacy integer (double, or exact StrNumber beyond 2^53,
// as legacy `runtime_integer`); Function, Struct and non-timer handles have no
// legacy representation here and fail.
#include "RuntimeValue.h"
#include "rt/value.h"

#include <cstddef>
#include <string>

namespace nift::rt {

// Both directions recurse once per nesting level and fail (error "value
// nesting exceeds N levels") at kMaxBridgeDepth instead of exhausting the C++
// stack (CP11 review round 2, R6). Neither is reachable from the VM.
inline constexpr std::size_t kMaxBridgeDepth = 512;

bool from_legacy(const nift::RuntimeValue& legacy, Value& out, std::string& error);
bool to_legacy(const Value& value, nift::RuntimeValue& out, std::string& error);

} // namespace nift::rt
