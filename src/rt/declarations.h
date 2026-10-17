#pragma once
// Declaration statements of the statement layer: fn, enum, struct, export,
// import and their template (`@fn`, `@struct`, `@import`) forms. The
// StatementParser members are defined in declarations.cpp.
#include "rt/statement_parser.h"

#include <cstdint>

namespace nift::rt::detail {

// Mirrors the @fn( handler (src/ParserTemplate.cpp) and
// parse_callable_parameters (src/ParserHelpers.cpp): fills the name and
// parameters of `fn` from the signature in src[open + 1, close), or returns an
// Opaque node over `fn` (legacy reports a malformed signature only when the
// declaration runs).
NodeId fn_signature(Ast& ast, NodeId fn, std::uint32_t open, std::uint32_t close, bool method);

} // namespace nift::rt::detail
