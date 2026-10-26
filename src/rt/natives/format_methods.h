#pragma once
// Family F6 of the CP14 built-ins: conversion and formatting. to_string on
// numbers, bools and null; the presentation methods stringify, prettify and
// highlight on any plain value; the free functions html_escape, attr_escape,
// url_encode, type and the is_* predicates. Semantics are legacy's
// (ParserExpression.cpp: serialize_value, public_type, the escape family),
// with the differences listed in docs/runtime-2/checkpoints/cp14-builtins-design.md.
#include "rt/natives/builtin.h"

#include <string>

namespace nift::rt::natives {

Status scalar_to_string(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fmt_stringify(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fmt_prettify(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fmt_highlight(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_html_escape(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_attr_escape(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_url_encode(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_type(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_null(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_bool(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_number(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_int(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_float(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_string(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_bytes(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_array(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_object(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_struct(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_function(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_collection(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_is_enum(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

// Legacy serialize_value for plain values (null, bool, numbers, strings,
// arrays, objects and Errors; bytes and functions are serialization errors).
// False with `error` set on failure; unsupported runtime kinds (structs and
// handles) are reported through `internal`.
bool serialize(const Value& value, bool pretty, std::string& out, std::string& error, bool& internal);

// Legacy public_type for the value kinds rt has ("int"/"float" by value).
const char* public_type_name(const Value& value);

} // namespace nift::rt::natives
