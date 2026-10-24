#pragma once
// CP17 VM file/stream handle natives. The W13 compiler commit registers these
// as file()/ifstream()/ofstream() and their methods; until then they are
// exercised by the native unit tests and the VM host layer directly.
#include "rt/natives/host_ops.h"

namespace nift::rt::natives {

Status file_factory(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status ifstream_factory(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status ofstream_factory(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

Status file_open(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_close(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_read(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_read_all(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_read_line(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_write(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_write_line(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_flush(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_save(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_revert(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_path(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_exists(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_modified(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_tell(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_seek(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_eof(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_copy(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_move(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status file_remove(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

// Cleanup hooks (registered by the VM's table).
handles::CleanupOutcome cleanup_file(handles::CleanupEvent& event);

} // namespace nift::rt::natives
