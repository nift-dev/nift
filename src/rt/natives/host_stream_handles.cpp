#include "rt/natives/host_stream_handles.h"
// rt-lint: cold-file stream methods, per-call host I/O

#include "rt/natives/host_file_handles_internal.h"

#include "ReadLimit.h"
#include "rt/natives/support.h"

namespace nift::rt::natives {
using namespace host_handle_detail;

namespace {

Status stream_read_failure(const Context& context, std::string_view operation, std::string& error) {
    error = std::string(operation) + ": input failure";
    if (context.host.error_code) *context.host.error_code = "stream.read_failed";
    return Status::Recoverable;
}

Status stream_limit(const Context& context, std::string message, const char* code, std::string& error) {
    error = std::move(message);
    if (context.host.error_code) *context.host.error_code = code;
    return Status::Limit;
}

Status stream_invalid_setting(const Context& context, const nift::readlimit::Setting& setting,
                              std::string_view operation, std::string& error) {
    error = nift::readlimit::invalid_message(std::string(operation), setting);
    if (context.host.error_code) *context.host.error_code = "runtime.invalid_argument";
    return Status::Error;
}

Status stream_read_impl(const Context& context, StreamResource& stream, std::size_t requested, bool all,
                        std::string_view operation, Value& out, std::string& error) {
    const nift::readlimit::Setting setting = nift::readlimit::current();
    if (!setting.valid) return stream_invalid_setting(context, setting, operation, error);
    const std::uint64_t limit = setting.bytes;
    const std::uint64_t max_bytes = all ? limit : static_cast<std::uint64_t>(requested);
    if (max_bytes > limit)
        return stream_limit(context,
                            nift::readlimit::exceeded_message(std::string(operation),
                                                              "a read of " + stream.path.generic_string(), limit),
                            "limit.exceeded", error);

    BudgetedText data(context);
    char buffer[64 * 1024];
    std::uint64_t total = 0;
    while (total < max_bytes) {
        const std::uint64_t room = max_bytes - total;
        const std::size_t ask = static_cast<std::size_t>(std::min<std::uint64_t>(room, sizeof buffer));
        stream.input->read(buffer, static_cast<std::streamsize>(ask));
        const std::streamsize got_signed = stream.input->gcount();
        if (stream.input->bad()) return stream_read_failure(context, operation, error);
        if (got_signed <= 0) break;
        const std::uint64_t got = static_cast<std::uint64_t>(got_signed);
        if (total + got > limit)
            return stream_limit(context,
                                nift::readlimit::exceeded_message(std::string(operation),
                                                                  (all ? "the rest of " : "a read of ") +
                                                                      stream.path.generic_string(),
                                                                  limit),
                                "limit.exceeded", error);
        if (!data.append(std::string_view(buffer, static_cast<std::size_t>(got))))
            return stream_limit(context, std::string(operation) + ": file exceeds the memory budget",
                                "limit.exceeded", error);
        total += got;
        if (static_cast<std::size_t>(got) < ask) break;
    }
    if (all && total == limit && stream.input->good()) {
        char extra = 0;
        stream.input->read(&extra, 1);
        if (stream.input->bad()) return stream_read_failure(context, operation, error);
        if (stream.input->gcount() > 0)
            return stream_limit(context,
                                nift::readlimit::exceeded_message(std::string(operation),
                                                                  "the rest of " + stream.path.generic_string(), limit),
                                "limit.exceeded", error);
    }
    std::string text = data.take();
    // Move the buffer into the StringObject: copying here would keep both the
    // temporary and the result live. Charge the retained capacity, not just
    // the logical size.
    const std::size_t retained = text.capacity();
    if (!reserve_result(context, retained))
        return stream_limit(context, std::string(operation) + ": file exceeds the memory budget",
                            "limit.exceeded", error);
    out = make_string(std::move(text));
    return Status::Ok;
}

} // namespace

Status stream_read(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::shared_ptr<StreamResource> stream;
    Status status = Status::Ok;
    if (!stream_arg(context, args, argc, "read", stream, error, status)) return status;
    if (stream->kind != StreamResource::Kind::Input || !stream->open || !stream->input) {
        error = "read: stream is not open for reading";
        return Status::Error;
    }
    std::size_t count = std::numeric_limits<std::size_t>::max();
    bool all = true;
    if (argc == 2) {
        std::int64_t requested = 0;
        if (!number_to_i64(args[1], requested) || requested < 0) {
            error = "read: invalid byte count";
            return Status::Error;
        }
        count = static_cast<std::size_t>(requested);
        all = false;
    } else if (argc > 2) { error = "read: expected zero or one byte count"; return Status::Error; }
    return stream_read_impl(context, *stream, count, all, "read", out, error);
}

Status stream_read_all(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                       std::string& error) {
    std::shared_ptr<StreamResource> stream;
    Status status = Status::Ok;
    if (!stream_arg(context, args, argc, "read_all", stream, error, status)) return status;
    if (stream->kind != StreamResource::Kind::Input || !stream->open || !stream->input) {
        error = "read_all: stream is not open for reading";
        return Status::Error;
    }
    if (!no_args(argc, "read_all", error)) return Status::Error;
    return stream_read_impl(context, *stream, 0, true, "read_all", out, error);
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
    const nift::readlimit::Setting setting = nift::readlimit::current();
    if (!setting.valid) return stream_invalid_setting(context, setting, "read_line", error);
    BudgetedText line(context);
    bool extracted = false;
    char c = 0;
    while (stream->input->get(c)) {
        extracted = true;
        if (c == '\n') break;
        if (line.size() >= setting.bytes)
            return stream_limit(context,
                                nift::readlimit::exceeded_message("read_line",
                                                                  "a line of " + stream->path.generic_string(),
                                                                  setting.bytes),
                                "limit.exceeded", error);
        if (!line.append(std::string_view(&c, 1)))
            return stream_limit(context, "read_line: file exceeds the memory budget", "limit.exceeded", error);
    }
    if (stream->input->bad()) return stream_read_failure(context, "read_line", error);
    if (!extracted) { out = Value::null(); return Status::Ok; }
    std::string text = line.take();
    const std::size_t retained = text.capacity();
    if (!reserve_result(context, retained))
        return stream_limit(context, "read_line: file exceeds the memory budget", "limit.exceeded", error);
    out = make_string(std::move(text));
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
    out = Value::boolean(stream->input->peek() == std::char_traits<char>::eof());
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
    if (!*stream->output) {
        error = "write: output failure";
        if (context.host.error_code) *context.host.error_code = "stream.write_failed";
        return Status::Recoverable;
    }
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
    if (!*stream->output) {
        error = "flush: output failure";
        if (context.host.error_code) *context.host.error_code = "stream.flush_failed";
        return Status::Recoverable;
    }
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
        if (stream->output) {
            stream->output->flush();
            if (!*stream->output) {
                stream->output->close();
                stream->open = false;
                stream->closed = true;
                const HandleStatus closed = handles::native_handle_close(
                    context.handles, "close", args[0], HandleKind::Stream, error);
                (void)closed;  // the stream operation error is the primary result
                error = "close: output flush failed";
                if (context.host.error_code) *context.host.error_code = "stream.close_failed";
                return Status::Recoverable;
            }
            stream->output->close();
        }
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
        if (stream->output) {
            stream->output->flush();
            if (!*stream->output) {
                stream->output->close();
                stream->open = false;
                stream->closed = true;
                return handles::CleanupOutcome::failure("stream.close_failed", "close: output flush failed");
            }
            stream->output->close();
        }
        if (stream->input) stream->input->close();
        stream->open = false;
        stream->closed = true;
    }
    return handles::CleanupOutcome::completed();
}

} // namespace nift::rt::natives
