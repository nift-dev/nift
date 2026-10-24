#pragma once
// CP17 VM stream handle natives (ifstream/ofstream reads, writes and close).
#include "rt/natives/host_ops.h"

namespace nift::rt::natives {

Status stream_read(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status stream_read_line(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status stream_read_all(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status stream_eof(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status stream_write(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status stream_write_line(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status stream_flush(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status stream_close(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
handles::CleanupOutcome cleanup_stream(handles::CleanupEvent& event);

} // namespace nift::rt::natives
