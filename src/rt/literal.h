#pragma once
// Literal decoding for the Runtime 2.0 compiler (CP11): number and string
// literal text to values, restricted to spellings whose legacy value is the
// same on the top-level and loop (prepared) evaluation paths. Anything else is
// reported as unsupported so the unit stays on the legacy engine.
#include "rt/value.h"

#include <string_view>

namespace nift::rt {

// Integers without a leading zero below 2^53 become Int; other spellings with
// a fraction or exponent, at most 15 significant digits and a magnitude below
// 2^53 become Double. Returns false otherwise (decimal-exact legacy values,
// octal-looking spellings, huge or tiny magnitudes).
bool decode_number_literal(std::string_view text, Value& out);

// `text` includes its quotes. Legacy decodes escapes differently on the loop
// path (a backslash in a loop-body literal is not an escape there), and `$`
// starts interpolation in some positions, so only literals without `\`, `$`
// and control bytes are accepted.
bool decode_string_literal(std::string_view text, Value& out);

} // namespace nift::rt
