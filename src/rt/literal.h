#pragma once
// Literal decoding for the Runtime 2.0 compiler (CP11): number and string
// literal text to values, restricted to spellings whose legacy value is the
// same on the top-level and loop (prepared) evaluation paths. Anything else is
// reported as unsupported so the unit stays on the legacy engine.
#include "rt/value.h"

#include <string_view>

namespace nift::rt {

// Integers without a leading zero in signed 64-bit range become Int; other
// spellings with a fraction or exponent, at most 15 significant digits and a
// magnitude below 2^53 become Double. Returns false otherwise (decimal-exact
// legacy values, octal-looking spellings, huge or tiny magnitudes).
bool decode_number_literal(std::string_view text, Value& out);

enum class NegativeLiteral { Decoded, Zero, Invalid };
// The magnitude text of a unary-negated number literal. INT64_MIN's magnitude
// (9223372036854775808) is decoded exactly; zero means the runtime negation
// must remain (-0); Invalid is a number-literal refusal (including -0.0).
NegativeLiteral decode_negative_number_literal(std::string_view text, Value& out);

// `text` includes its quotes. Decodes legacy escapes (`\n`, `\t`, `\r`, `\\`,
// quoted quote); an unknown escape drops the backslash; a `$` that does not
// start `$[` is data. Raw control/NUL bytes and `$[` are refused (the parser
// emits StringParts for interpolation).
bool decode_string_literal(std::string_view text, Value& out);

} // namespace nift::rt
