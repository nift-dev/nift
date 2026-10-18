#pragma once
// Family F1 of the CP14 built-ins: string methods (receiver args[0] is a
// String). Semantics are legacy's (ParserExpression.cpp: the shared read-only
// method layer `complete_readonly_method` and the canonical string family),
// with the differences listed in docs/runtime-2/checkpoints/cp14-builtins-design.md.
#include "rt/natives/builtin.h"

namespace nift::rt::natives {

Status str_length(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_empty(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_split(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_trim(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_trim_start(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_trim_end(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_to_upper(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_to_lower(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_index_of(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_last_index_of(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_contains(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_starts_with(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_ends_with(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_replace(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_substr(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_to_string(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_to_int(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_to_double(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

} // namespace nift::rt::natives
