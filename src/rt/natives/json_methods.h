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
// Decimals, others Doubles. `error` is the parser's message. Not a script
// built-in. Callers: CP16's inject/@json/read_val (which add legacy's
// "inject: " etc. prefix) and the compiler for `{...}` literals, which runs
// it at compile time: a literal that does not parse as JSON is refused before
// execution (legacy then falls back to its Nift literal evaluator; wiring rule
// W9), so a json_parse error never surfaces at run time for a literal.
Status json_parse(const Context& context, std::string_view text, Value& out, std::string& error);

Status fn_validate(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_copy(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

} // namespace nift::rt::natives
