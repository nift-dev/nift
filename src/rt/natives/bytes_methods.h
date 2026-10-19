#pragma once
// CP15 family B1: bytes values. The constructor bytes([..]), string
// encode("utf-8"), the bytes methods length, size, empty, slice,
// decode("utf-8"), and sha256 of a string or bytes value. Semantics are
// legacy's (ParserExpression.cpp: the bytes family, `call_args("bytes"`,
// `call_args("sha256"`), with the differences listed in
// docs/runtime-2/checkpoints/cp15-builtins-design.md.
#include "rt/natives/builtin.h"

#include <string>
#include <string_view>

namespace nift::rt::natives {

Status fn_bytes(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status str_encode(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status bytes_length(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status bytes_size(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status bytes_empty(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status bytes_slice(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status bytes_decode(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status fn_sha256(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

// Legacy runtime_valid_utf8: strict UTF-8 (no overlong forms, surrogates or
// code points above U+10FFFF). Stricter than utf8_sequence (support.h), which
// length/split use (design record B-Q1).
bool strict_utf8(std::string_view text);

} // namespace nift::rt::natives
