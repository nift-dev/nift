#pragma once
// Output-order repair (D-072 #1, D-073 #2-#3; design review-ha DESIGN-OUTPUT-ORDER.md): recognise a path that names
// the process's own stdout or stderr object, and hold std-stream output streams still open at the end of a run until
// the VM output and the error message are written.
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

namespace nift::rt::natives {

struct Context;

// True when `path` names the same object as file descriptor 1 or 2 (any alias: /dev/stdout, /dev/fd/1,
// /proc/self/fd/1, a symlink, the redirect target under its own name). POSIX: stat(path), following links,
// against fstat(1) and fstat(2) (st_dev, st_ino); the path is never opened, so a FIFO cannot block. Windows: the
// console names CON and CONOUT$, or the file identity against GetStdHandle (CI-verified only). A path that does not
// exist is not a std stream.
bool std_stream_target(const std::filesystem::path& path);

// Before opening `path` (an output stream, or file().open, which reads it): when it is a std stream, publish the
// program's pending output, or, in a thread() or async() task, refuse (D-073 #2): returns false with `error` set and
// the caller returns Recoverable; nothing is opened or truncated. `std_target` records the identity for the handle.
bool std_stream_open(const Context& context, const std::filesystem::path& path, std::string_view name,
                     bool& std_target, std::string& error);

// cat's bytes are program output in program order (D-072 #1): the VM's output sink, or stdout without a VM.
void emit_program_output(const Context& context, const std::string& data);

// End of run: a std-stream output stream still open is kept (not flushed) by its cleanup, and flushed and closed by
// release_std_stream_tail() after the VM output and the error message, as legacy flushes it at process exit.
void hold_std_stream(std::shared_ptr<std::ofstream> stream);
void release_std_stream_tail();

} // namespace nift::rt::natives
