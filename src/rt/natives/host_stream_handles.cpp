#include "rt/natives/host_stream_handles.h"
// rt-lint: cold-file stream methods, per-call host I/O

#include "rt/natives/host_file_handles_internal.h"

namespace nift::rt::natives {
using namespace host_handle_detail;

Status stream_read(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<StreamResource> stream;
    Status status = Status::Ok;
    if (!stream_arg(context, args, argc, "read", stream, error, status)) return status;
    if (stream->kind != StreamResource::Kind::Input || !stream->open || !stream->input) {
        error = "read: stream is not open for reading";
        return Status::Error;
    }
    std::size_t count = std::numeric_limits<std::size_t>::max();
    if (argc == 2) {
        std::int64_t requested = 0;
        if (!number_to_i64(args[1], requested) || requested < 0) {
            error = "read: invalid byte count";
            return Status::Error;
        }
        count = static_cast<std::size_t>(requested);
    } else if (argc > 2) { error = "read: expected zero or one byte count"; return Status::Error; }
    std::string data;
    if (count == std::numeric_limits<std::size_t>::max()) {
        data.assign(std::istreambuf_iterator<char>(*stream->input), std::istreambuf_iterator<char>());
    } else {
        data.resize(count);
        stream->input->read(data.data(), static_cast<std::streamsize>(count));
        data.resize(static_cast<std::size_t>(stream->input->gcount()));
    }
    out = make_string(data);
    return Status::Ok;
}

Status stream_read_all(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                       std::string& error) {
    if (!no_args(argc, "read_all", error)) return Status::Error;
    return stream_read(context, args, 1, out, error);
}

Status stream_read_line(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                        std::string& error) {
    std::shared_ptr<StreamResource> stream;
    Status status = Status::Ok;
    if (!stream_arg(context, args, argc, "read_line", stream, error, status)) return status;
    if (stream->kind != StreamResource::Kind::Input || !stream->open || !stream->input) {
        error = "read_line: stream is not open for reading";
        return Status::Error;
    }
    if (!no_args(argc, "read_line", error)) return Status::Error;
    std::string line;
    if (!std::getline(*stream->input, line)) { out = Value::null(); return Status::Ok; }
    out = make_string(line);
    return Status::Ok;
}

Status stream_eof(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<StreamResource> stream;
    Status status = Status::Ok;
    if (!stream_arg(context, args, argc, "eof", stream, error, status)) return status;
    if (stream->kind != StreamResource::Kind::Input || !stream->open || !stream->input) {
        error = "eof: stream is not open for reading";
        return Status::Error;
    }
    if (!no_args(argc, "eof", error)) return Status::Error;
    out = Value::boolean(stream->input->eof());
    return Status::Ok;
}

Status stream_write(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<StreamResource> stream;
    Status status = Status::Ok;
    if (!stream_arg(context, args, argc, "write", stream, error, status)) return status;
    if (stream->kind != StreamResource::Kind::Output || !stream->open || !stream->output) {
        error = "write: stream is not open for writing";
        return Status::Error;
    }
    if (argc != 2) { error = "write: expected one value"; return Status::Error; }
    std::string data;
    if (!render_arg(args[1], data)) { error = "write: value is not directly renderable"; return Status::Error; }
    stream->output->write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!*stream->output) { error = "stream.write_failed: output failure"; return Status::Error; }
    out = Value::null();
    return Status::Ok;
}

Status stream_write_line(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                         std::string& error) {
    if (argc != 2) { error = "write_line: expected one value"; return Status::Error; }
    Value newline = make_string("\n");
    Value first[2] = {args[0], args[1]};
    Status status = stream_write(context, first, 2, out, error);
    if (status != Status::Ok) return status;
    Value second[2] = {args[0], newline};
    return stream_write(context, second, 2, out, error);
}

Status stream_flush(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<StreamResource> stream;
    Status status = Status::Ok;
    if (!stream_arg(context, args, argc, "flush", stream, error, status)) return status;
    if (stream->kind != StreamResource::Kind::Output || !stream->output) {
        error = "flush: stream is not open for writing";
        return Status::Error;
    }
    stream->output->flush();
    if (!*stream->output) { error = "stream.flush_failed: output failure"; return Status::Error; }
    out = Value::null();
    return Status::Ok;
}

Status stream_close(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<StreamResource> stream;
    Status status = Status::Ok;
    if (!stream_arg(context, args, argc, "close", stream, error, status)) {
        if (status == Status::Recoverable) {
            const HandleStatus h = handles::native_handle_close(context.handles, "close", args[0],
                                                                HandleKind::Stream, error);
            if (h == HandleStatus::Ok) { out = Value::null(); return Status::Ok; }
            return map_handle(h);
        }
        return status;
    }
    if (!no_args(argc, "close", error)) return Status::Error;
    if (stream->open) {
        if (stream->output) { stream->output->flush(); stream->output->close(); }
        if (stream->input) stream->input->close();
        stream->open = false;
        stream->closed = true;
    }
    const HandleStatus h = handles::native_handle_close(context.handles, "close", args[0], HandleKind::Stream, error);
    if (h != HandleStatus::Ok) return map_handle(h);
    out = Value::null();
    return Status::Ok;
}

handles::CleanupOutcome cleanup_stream(handles::CleanupEvent& event) {
    auto stream = std::static_pointer_cast<StreamResource>(event.resource);
    if (stream && stream->open) {
        if (stream->output) { stream->output->flush(); stream->output->close(); }
        if (stream->input) stream->input->close();
        stream->open = false;
        stream->closed = true;
    }
    return handles::CleanupOutcome::completed();
}

} // namespace nift::rt::natives
