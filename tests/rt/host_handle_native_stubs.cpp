// Link-only stubs for the isolated built-ins bench (test-build isolation).
// The real CP17 file/stream natives are Host natives: they render scalar
// values and resolve handle resources, so they are intentionally excluded
// from RT_ISOLATION_SOURCES. The built-in table still names them; these stubs
// satisfy the table without pulling the render/legacy value bridge into the
// pure-builtins link.
#include "rt/natives/host_file_handles.h"
#include "rt/natives/host_stream_handles.h"

namespace nift::rt::natives {

namespace {
Status unlinked(const char*, std::string& error) {
    error = "host handle native is not linked in the isolated built-ins bench";
    return Status::Internal;
}
} // namespace

#define NIFT_HANDLE_STUB(name) \
    Status name(const Context&, const Value*, std::uint32_t, Value&, std::string& error) { return unlinked(#name, error); }

NIFT_HANDLE_STUB(handle_close)
NIFT_HANDLE_STUB(file_factory)
NIFT_HANDLE_STUB(ifstream_factory)
NIFT_HANDLE_STUB(ofstream_factory)
NIFT_HANDLE_STUB(file_open)
NIFT_HANDLE_STUB(file_close)
NIFT_HANDLE_STUB(file_read)
NIFT_HANDLE_STUB(file_read_all)
NIFT_HANDLE_STUB(file_read_line)
NIFT_HANDLE_STUB(file_write)
NIFT_HANDLE_STUB(file_write_line)
NIFT_HANDLE_STUB(file_flush)
NIFT_HANDLE_STUB(file_save)
NIFT_HANDLE_STUB(file_revert)
NIFT_HANDLE_STUB(file_path)
NIFT_HANDLE_STUB(file_exists)
NIFT_HANDLE_STUB(file_modified)
NIFT_HANDLE_STUB(file_tell)
NIFT_HANDLE_STUB(file_seek)
NIFT_HANDLE_STUB(file_eof)
NIFT_HANDLE_STUB(file_copy)
NIFT_HANDLE_STUB(file_move)
NIFT_HANDLE_STUB(file_remove)
NIFT_HANDLE_STUB(stream_close)
NIFT_HANDLE_STUB(stream_read)
NIFT_HANDLE_STUB(stream_read_all)
NIFT_HANDLE_STUB(stream_read_line)
NIFT_HANDLE_STUB(stream_write)
NIFT_HANDLE_STUB(stream_write_line)
NIFT_HANDLE_STUB(stream_flush)
NIFT_HANDLE_STUB(stream_eof)

#undef NIFT_HANDLE_STUB

} // namespace nift::rt::natives
