#pragma once
// Conversions between rt::Value and the legacy nift::RuntimeValue (CP09), for
// whole-unit fallback and bridging while both engines coexist.
//
// legacy -> rt: Number -> Double (exact bits, including -0.0); StrNumber ->
// Decimal; String -> String (legacy "\x1fnift:" handle strings stay strings:
// typed handles arrive with CP17); Array, Object (duplicate keys: first
// wins, as legacy lookup does); Bytes; Timer -> Handle(Timer, instance,
// owner); Error -> Error (cause chain kept).
// rt -> legacy: Int -> legacy integer (double, or exact StrNumber beyond 2^53,
// as legacy `runtime_integer`); Function, Struct and non-timer handles have no
// legacy representation here and fail.
#include "RuntimeValue.h"
#include "rt/value.h"

#include <string>

namespace nift::rt {

bool from_legacy(const nift::RuntimeValue& legacy, Value& out, std::string& error);
bool to_legacy(const Value& value, nift::RuntimeValue& out, std::string& error);

} // namespace nift::rt
