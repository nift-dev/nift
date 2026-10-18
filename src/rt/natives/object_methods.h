#pragma once
// Family F3 of the CP14 built-ins: object methods (receiver args[0] is an
// Object). Semantics are legacy's canonical object family (ParserExpression.cpp
// "value family 3"), with the differences listed in
// docs/runtime-2/checkpoints/cp14-builtins-design.md.
#include "rt/natives/builtin.h"

#include <string_view>

namespace nift::rt::natives {

Status obj_size(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status obj_empty(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status obj_keys(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status obj_values(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status obj_entries(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status obj_has(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status obj_get(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status obj_merge(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status obj_merge_deep(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status obj_pick(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status obj_omit(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

// Legacy glob_has_magic / glob_component_match (ParserHelpers.cpp), used by
// pick and omit for key patterns.
bool glob_magic(std::string_view pattern);
bool glob_match(std::string_view pattern, std::string_view name);

} // namespace nift::rt::natives
