#pragma once
// VM natives that write or edit a managed file buffer.
#include "rt/natives/host_ops.h"

namespace nift::rt::natives {

Status file_append(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status file_prepend(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status file_insert(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status file_insert_before(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status file_insert_after(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status file_replace(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status file_replace_once(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status file_write_val(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_write(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_write_line(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

} // namespace nift::rt::natives
