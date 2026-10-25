#include "rt/natives/host_file_handles.h"
// rt-lint: cold-file handle factory and methods, per-call host I/O

#include "rt/natives/host_file_handles_internal.h"

namespace nift::rt::natives {
using namespace host_handle_detail;

Status file_factory(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 1) { error = "file: expected string path"; return Status::Error; }
    fs::path path;
    Status status = Status::Ok;
    if (!resolve_path(context, "file", args[0], path, error, status)) return status;
    auto file = std::make_shared<FileResource>();
    file->path = std::move(path);
    return create_handle(context, "file", HandleKind::File, file, out, error);
}

Status ifstream_factory(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                        std::string& error) {
    if (argc != 1) { error = "ifstream: expected string path"; return Status::Error; }
    fs::path path;
    Status status = Status::Ok;
    if (!resolve_path(context, "ifstream", args[0], path, error, status)) return status;
    auto stream = std::make_shared<StreamResource>();
    stream->kind = StreamResource::Kind::Input;
    stream->path = path;
    stream->input = std::make_shared<std::ifstream>(path, std::ios::binary);
    if (!*stream->input) {
        error = "stream.open_failed: cannot open path";
        if (context.host.error_code) *context.host.error_code = "stream.open_failed";
        return Status::Recoverable;
    }
    stream->open = true;
    HostCall call(context.host, host::HostOp::StreamOpen, "ifstream");
    call.adopt(path);
    if (call.begin() != host::HostStatus::Ok) { error = call.error().message; return Status::Error; }
    call.succeed();
    return create_handle(context, "ifstream", HandleKind::Stream, stream, out, error);
}

Status ofstream_factory(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                        std::string& error) {
    if (argc != 1) { error = "ofstream: expected string path"; return Status::Error; }
    fs::path path;
    Status status = Status::Ok;
    if (!resolve_path(context, "ofstream", args[0], path, error, status)) return status;
    auto stream = std::make_shared<StreamResource>();
    stream->kind = StreamResource::Kind::Output;
    stream->path = path;
    stream->output = std::make_shared<std::ofstream>(path, std::ios::binary | std::ios::trunc);
    if (!*stream->output) {
        error = "stream.open_failed: cannot open path";
        if (context.host.error_code) *context.host.error_code = "stream.open_failed";
        return Status::Recoverable;
    }
    stream->open = true;
    HostCall call(context.host, host::HostOp::StreamOpen, "ofstream");
    call.adopt(path);
    call.set_output_stream();
    if (call.begin() != host::HostStatus::Ok) { error = call.error().message; return Status::Error; }
    call.succeed();
    return create_handle(context, "ofstream", HandleKind::Stream, stream, out, error);
}

Status file_open(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "open", file, error, status)) return status;
    if (file->open) { error = "open: file is already open"; return Status::Error; }
    if (argc > 2) { error = "open: expected zero or one mode"; return Status::Error; }
    std::string mode = "r";
    if (argc == 2 && !string_arg(args[1], mode)) { error = "open: expected string argument"; return Status::Error; }
    if (mode != "r" && mode != "w" && mode != "a" && mode != "rw") {
        error = "open: mode must be r, w, a or rw";
        return Status::Error;
    }
    HostCall call(context.host, host::HostOp::Open, "open");
    call.adopt(file->path);
    call.set_open_mode(mode);
    call.set_pre_result(mode);
    std::error_code ec;
    const bool exists = fs::exists(file->path, ec) && !ec;
    if ((mode == "r" || mode == "rw") && !exists) {
        if (call.begin() != host::HostStatus::Ok) { error = call.error().message; return Status::Error; }
        call.fail(host::HostStatus::Recoverable, host_error("io.open_failed", "open: file does not exist"));
        error = "open: file does not exist";
        if (context.host.error_code) *context.host.error_code = "io.open_failed";
        return Status::Recoverable;
    }
    if (exists && fs::is_directory(file->path, ec)) {
        if (call.begin() != host::HostStatus::Ok) { error = call.error().message; return Status::Error; }
        error = "open: path is a directory";
        call.fail(host::HostStatus::Fatal, host_error("runtime.invalid_operation", error));
        return Status::Error;
    }
    std::string data;
    if (exists) {
        std::string read_error;
        Status read_status = Status::Ok;
        if (mode == "r" || mode == "rw") {
            // Legacy reads the existing file before its open line is emitted.
            HostCall read(context.host, host::HostOp::Read, "open");
            read.adopt(file->path);
            if (read.begin() != host::HostStatus::Ok) { error = read.error().message; return Status::Error; }
            if (!read_whole_path(context, read, "open", data, read_error, read_status)) {
                error = read_error;
                return read_status;
            }
            if (read.succeed(std::to_string(data.size())) != host::HostStatus::Ok) return Status::Error;
        } else if (!read_whole_path(context, call, "open", data, read_error, read_status)) {
            error = read_error;
            return read_status;
        }
    }
    if (call.begin() != host::HostStatus::Ok) { error = call.error().message; return Status::Error; }
    host::Observation obs;
    obs.bytes = data;
    obs.open_mode = mode;
    if (call.succeed(mode, obs) != host::HostStatus::Ok) return Status::Error;
    file->mode = mode;
    file->existed_at_open = exists;
    if (mode == "w") {
        file->saved = std::move(data);
        file->working.clear();
        file->saved_is_working = false;
    } else {
        file->working = std::move(data);
        file->saved.clear();
        file->saved_is_working = true;
    }
    file->cursor = mode == "a" ? file->working.size() : 0;
    file->dirty = mode == "w" || (mode == "a" && !exists);
    file->open = true;
    out = Value::null();
    return Status::Ok;
}

Status file_close(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "close", file, error, status)) {
        // D-039: close on an already closed handle is Ok.
        if (status == Status::Recoverable) {
            const HandleStatus h = handles::native_handle_close(context.handles, "close", args[0],
                                                                HandleKind::File, error);
            if (h == HandleStatus::Ok) { out = Value::null(); return Status::Ok; }
            return map_handle(h);
        }
        return status;
    }
    if (!no_args(argc, "close", error)) return Status::Error;
    if (!file->open) { error = "close: file is not open"; return Status::Error; }
    if (file->dirty) { error = "close: file has unsaved changes; save() or revert() first"; return Status::Error; }
    file->open = false;
    file->mode.clear();
    file->working.clear();
    file->saved.clear();
    file->saved_is_working = false;
    file->cursor = 0;
    const HandleStatus h = handles::native_handle_close(context.handles, "close", args[0], HandleKind::File, error);
    if (h != HandleStatus::Ok) return map_handle(h);
    out = Value::null();
    return Status::Ok;
}

Status file_read(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "read", file, error, status)) return status;
    if (!file->open) { error = "read: file is not open"; return Status::Error; }
    if (file->mode != "r" && file->mode != "rw") { error = "read: file is not open for reading"; return Status::Error; }
    const std::size_t base = std::min(file->cursor, file->working.size());
    std::size_t count = file->working.size() - base;
    if (argc == 2) {
        std::int64_t requested = 0;
        if (!number_to_i64(args[1], requested) || requested < 0) {
            error = "read: invalid byte count";
            return Status::Error;
        }
        count = std::min(count, static_cast<std::size_t>(requested));
    } else if (argc > 2) { error = "read: expected zero or one byte count"; return Status::Error; }
    out = make_string(file->working.substr(base, count));
    file->cursor = base + count;
    return Status::Ok;
}

Status file_read_all(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "read_all", file, error, status)) return status;
    if (!file->open) { error = "read_all: file is not open"; return Status::Error; }
    if (file->mode != "r" && file->mode != "rw") {
        error = "read_all: file is not open for reading";
        return Status::Error;
    }
    if (!no_args(argc, "read_all", error)) return Status::Error;
    const std::size_t base = std::min(file->cursor, file->working.size());
    out = make_string(file->working.substr(base));
    file->cursor = file->working.size();
    return Status::Ok;
}

Status file_read_line(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "read_line", file, error, status)) return status;
    if (!file->open) { error = "read_line: file is not open"; return Status::Error; }
    if (file->mode != "r" && file->mode != "rw") {
        error = "read_line: file is not open for reading";
        return Status::Error;
    }
    if (!no_args(argc, "read_line", error)) return Status::Error;
    if (file->cursor >= file->working.size()) { out = Value::null(); return Status::Ok; }
    const auto end = file->working.find('\n', file->cursor);
    const std::size_t stop = end == std::string::npos ? file->working.size() : end;
    std::string line = file->working.substr(file->cursor, stop - file->cursor);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    file->cursor = end == std::string::npos ? file->working.size() : end + 1;
    out = make_string(line);
    return Status::Ok;
}

Status file_write(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "write", file, error, status)) return status;
    if (!file->open) { error = "write: file is not open"; return Status::Error; }
    if (file->mode == "r") { error = "write: file is not open for writing"; return Status::Error; }
    if (argc != 2) { error = "write: expected one value"; return Status::Error; }
    std::string data;
    if (!render_arg(args[1], data)) { error = "write: value is not directly renderable"; return Status::Error; }
    const std::size_t base = std::min(file->cursor, file->working.size());
    const std::size_t overlap = std::min(data.size(), file->working.size() - base);
    if (overlap != data.size() || file->working.compare(base, overlap, data) != 0) file->prepare_mutation();
    file->working.replace(base, overlap, data);
    file->cursor = base + data.size();
    file->update_dirty();
    out = Value::null();
    return Status::Ok;
}

Status file_write_line(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                       std::string& error) {
    if (argc != 2) { error = "write_line: expected one value"; return Status::Error; }
    Value newline = make_string("\n");
    Value armed[2] = {args[0], args[1]};
    Status status = file_write(context, armed, 2, out, error);
    if (status != Status::Ok) return status;
    Value args2[2] = {args[0], newline};
    return file_write(context, args2, 2, out, error);
}

Status file_flush(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "flush", file, error, status)) return status;
    if (!file->open) { error = "flush: file is not open"; return Status::Error; }
    if (!no_args(argc, "flush", error)) return Status::Error;
    out = Value::null();
    return Status::Ok;
}

Status file_revert(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "revert", file, error, status)) return status;
    if (!file->open) { error = "revert: file is not open"; return Status::Error; }
    if (!no_args(argc, "revert", error)) return Status::Error;
    if (!file->saved_is_working) file->working = file->saved;
    file->saved_is_working = true;
    file->saved.clear();
    file->cursor = 0;
    file->dirty = false;
    out = Value::null();
    return Status::Ok;
}

Status file_path(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "path", file, error, status)) return status;
    if (!no_args(argc, "path", error)) return Status::Error;
    out = make_string(file->path.generic_string());
    return Status::Ok;
}

Status file_exists(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "exists", file, error, status)) return status;
    if (!no_args(argc, "exists", error)) return Status::Error;
    std::error_code ec;
    out = Value::boolean(fs::exists(file->path, ec) && !ec);
    return Status::Ok;
}

Status file_modified(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "modified", file, error, status)) return status;
    if (!file->open) { error = "modified: file is not open"; return Status::Error; }
    if (!no_args(argc, "modified", error)) return Status::Error;
    out = Value::boolean(file->dirty);
    return Status::Ok;
}

Status file_tell(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "tell", file, error, status)) return status;
    if (!file->open) { error = "tell: file is not open"; return Status::Error; }
    if (!no_args(argc, "tell", error)) return Status::Error;
    out = Value::integer(static_cast<std::int64_t>(file->cursor));
    return Status::Ok;
}

Status file_seek(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "seek", file, error, status)) return status;
    if (!file->open) { error = "seek: file is not open"; return Status::Error; }
    std::int64_t position = 0;
    if (argc != 2 || !number_to_i64(args[1], position) || position < 0 ||
        static_cast<std::uint64_t>(position) > file->working.size()) {
        error = "seek: byte position out of range";
        return Status::Error;
    }
    file->cursor = static_cast<std::size_t>(position);
    out = Value::null();
    return Status::Ok;
}

Status file_eof(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<FileResource> file;
    Status status = Status::Ok;
    if (!resource_arg(context, args, argc, "eof", file, error, status)) return status;
    if (!file->open) { error = "eof: file is not open"; return Status::Error; }
    if (file->mode != "r" && file->mode != "rw") { error = "eof: file is not open for reading"; return Status::Error; }
    if (!no_args(argc, "eof", error)) return Status::Error;
    out = Value::boolean(file->cursor >= file->working.size());
    return Status::Ok;
}

handles::CleanupOutcome cleanup_file(handles::CleanupEvent& event) {
    auto file = std::static_pointer_cast<FileResource>(event.resource);
    if (!file) return handles::CleanupOutcome::completed();
    const bool open = file->open;
    const bool dirty = open && file->dirty;
    const std::string path = file->path.generic_string();
    file->open = false;
    if (open) {
        std::string message = "managed file left open: " + path;
        if (dirty) message += " (unsaved changes discarded)";
        return handles::CleanupOutcome::failure(dirty ? "io.write_failed" : "runtime.invalid_operation",
                                                std::move(message));
    }
    return handles::CleanupOutcome::completed();
}

} // namespace nift::rt::natives
