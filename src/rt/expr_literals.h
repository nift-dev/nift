#pragma once
// Primaries of the expression layer: names, numbers, strings with `$[...]`
// interpolation, arrays, objects and lambdas.
#include "rt/expr_parser.h"

#include <string_view>

namespace nift::rt::detail {

// `true`, `false` and `null` are literals; every other word is a name.
NodeKind keyword_kind(std::string_view word);

// A string literal that is a whole array element or object value is stored
// verbatim by legacy (no interpolation; JSON or parameter unescaping), unlike
// the same literal elsewhere: regular only without `$[` and backslashes.
bool verbatim_literal_ok(const Ast& ast, NodeId item);

bool condition_object_is_json(std::string_view source, std::uint32_t begin, std::uint32_t end);

} // namespace nift::rt::detail
