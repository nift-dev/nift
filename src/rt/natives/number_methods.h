#pragma once
// CP15 family N1: number methods (receiver args[0] is Int, Double or
// Decimal): to_int, abs, floor, ceil, round. Semantics are legacy's canonical
// number family (ParserExpression.cpp "value family 2"), with the differences
// listed in docs/runtime-2/checkpoints/cp15-builtins-design.md.
#include "rt/natives/builtin.h"

#include <string>

namespace nift::rt::natives {

Status num_to_int(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status num_abs(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status num_floor(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status num_ceil(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status num_round(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

} // namespace nift::rt::natives
