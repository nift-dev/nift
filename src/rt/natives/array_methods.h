#pragma once
// Family F2 of the CP14 built-ins: read-only array methods (receiver args[0]
// is an Array) and the free collection functions min, max and range.
// Semantics are legacy's canonical array family (ParserExpression.cpp), with
// the differences listed in docs/runtime-2/checkpoints/cp14-builtins-design.md.
#include "rt/natives/builtin.h"

namespace nift::rt::natives {

Status arr_size(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_length(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_empty(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_first(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_last(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_index_of(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_contains(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_join(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_slice(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_unique(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_flatten(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_sum(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_min(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_max(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_take(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_drop(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_chunk(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status arr_from_entries(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

Status fn_min(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_max(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_range(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

} // namespace nift::rt::natives
