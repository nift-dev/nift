#pragma once
// Acceptance test of legacy's PREPARED expression parser (CP11 review round
// 2, M2): nift::ast::parse_expression (src/Ast.cpp, parse_bounded_expression
// and struct P), ported as a pure recogniser. Legacy prepares loop bodies and
// the functions called from them with this parser; when it rejects the
// condition of an `if`/`while` or a `return` expression of a function body,
// the preparation of that function fails and legacy re-runs the whole call on
// its textual evaluator after the arguments were already evaluated (they are
// evaluated twice). The compiler refuses such functions where they can be
// called from a loop (rt/prepared.h).
//
// The port follows the original branch by branch (same character classes:
// <cctype> in the "C" locale, same limits: 8,192 bytes, bracket depth 256,
// recursion depth 128), so it accepts exactly the texts legacy accepts. Call
// arguments are not inspected: an argument the prepared parser rejects makes
// legacy evaluate that one call textually, which is not a failure.
#include <string_view>

namespace nift::rt {

bool legacy_prepared_parse_accepts(std::string_view source);

} // namespace nift::rt
