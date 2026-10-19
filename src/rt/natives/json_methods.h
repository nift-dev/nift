#pragma once
// CP15 family J1: JSON values. json_parse (the strict JSON reader every
// legacy JSON input uses: structured literals, inject, @json, read_val),
// validate(schema, value), copy(value) and deepcopy(value). Semantics are
// legacy's (ParserHelpers.cpp parse_runtime_json, RuntimeValue.cpp
// runtime_from_json / runtime_to_json, ParserExpression.cpp `validate(`,
// `deepcopy(`), with the differences listed in
// docs/runtime-2/checkpoints/cp15-builtins-design.md.
#include "rt/natives/builtin.h"

#include <string>
#include <string_view>

namespace nift::rt::natives {

// Code of the catchable Error legacy raises when validate rejects an instance.
inline constexpr std::string_view kSchemaRejectedCode = "schema.rejected";

// Strict JSON (RFC 8259; duplicate object keys rejected; nesting <= 512) into
// a Value: numbers that need their exact spelling (more than 15 integer
// digits, zero with a fraction or sign, exponent magnitude >= 300) become
// Decimals, others Doubles. `error` is the parser's message (callers add
// their own prefix, as legacy does). Not a script built-in: used by the
// compiler for structured literals and by CP16's inject/@json/read_val.
Status json_parse(const Context& context, std::string_view text, Value& out, std::string& error);

Status fn_validate(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_copy(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

} // namespace nift::rt::natives
