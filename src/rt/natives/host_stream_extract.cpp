#include "rt/natives/host_stream_extract.h"
// rt-lint: cold-file bounded token extraction from a managed input stream

#include "ReadLimit.h"
#include "rt/natives/host_file_handles_internal.h"
#include "rt/natives/support.h"

#include <cctype>

namespace nift::rt::natives {

Status stream_extract_string(const Context& context, const Value* args, std::uint32_t argc,
                             Value& out, std::string& error) {
    if (argc != 2 || !args[1].is(Kind::String)) {
        error = "extraction: invalid string destination";
        return Status::Internal;
    }

    std::shared_ptr<host_handle_detail::StreamResource> stream;
    Status status = Status::Ok;
    if (!host_handle_detail::stream_arg(context, args, argc, "extraction", stream, error, status)) {
        if (status != Status::Recoverable) return status;
        if (context.host.error_code) context.host.error_code->clear();
        error = "stream is closed or invalid";
        return Status::Error;
    }
    if (!stream->open) {
        error = "stream is closed or invalid";
        return Status::Error;
    }
    if (stream->kind != host_handle_detail::StreamResource::Kind::Input || !stream->input) {
        error = "extraction requires an input stream";
        return Status::Error;
    }

    const auto input_failure = [&] {
        error = "extraction: input failure";
        if (context.host.error_code) *context.host.error_code = "stream.read_failed";
        return Status::Recoverable;
    };
    const auto limit_error = [&](std::string message) {
        error = std::move(message);
        if (context.host.error_code) *context.host.error_code = "limit.exceeded";
        return Status::Limit;
    };
    auto& input = *stream->input;
    if (input.bad()) return input_failure();
    input >> std::ws;
    if (input.bad()) return input_failure();
    if (input.peek() == std::char_traits<char>::eof()) {
        if (input.bad()) return input_failure();
        out = args[1];  // EOF does not change the destination.
        return Status::Ok;
    }

    const auto setting = nift::readlimit::current();
    if (!setting.valid) {
        error = nift::readlimit::invalid_message("extraction", setting);
        return Status::Error;
    }
    BudgetedText token(context);
    char c;
    while (input.get(c) && !std::isspace(static_cast<unsigned char>(c))) {
        if (token.size() == setting.bytes)
            return limit_error(nift::readlimit::exceeded_message("extraction",
                               "a token of " + stream->path.generic_string(), setting.bytes));
        if (token.size() == token.capacity() &&
            !token.reserve(static_cast<std::size_t>(nift::readlimit::growth_target(
                token.capacity(), token.size() + 1, setting.bytes, nift::readlimit::kChunk))))
            return limit_error("extraction: token exceeds the memory budget");
        if (!token.append(std::string_view(&c, 1)))
            return limit_error("extraction: token exceeds the memory budget");
    }
    if (input.bad()) return input_failure();
    if (token.size() == 0) {
        out = args[1];
        return Status::Ok;
    }
    std::string result = token.take();
    if (!reserve_result(context, result.capacity()))
        return limit_error("extraction: token exceeds the memory budget");
    out = make_string(std::move(result));
    return Status::Ok;
}

} // namespace nift::rt::natives
