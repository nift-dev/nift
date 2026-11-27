// CP16 VM host built-ins: whole-file reads and metadata.
// rt-lint: cold-file host I/O
#include "rt/natives/host_fs_read.h"

#include "rt/natives/host_fs_internal.h"
#include "rt/natives/json_methods.h"
#include "ParserHelpers.h"

namespace nift::rt::natives {
using namespace host_fs_detail;

namespace {

enum class ReadMode { Text, Bytes, Cat, JsonText };

Status read_source(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error,
                   std::string_view name, ReadMode mode) {
    const bool json_source = mode == ReadMode::JsonText;
    const auto op = json_source ? host::HostOp::Inject :
                    mode == ReadMode::Bytes ? host::HostOp::ReadBytes : host::HostOp::Read;
    HostCall call(context.host, op, name);
    if (resolve_arg(context, call, args, argc, 0, name, error) != Status::Ok) return Status::Error;
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    std::error_code regular_error;
    if (json_source) {
        const auto setting = nift::readlimit::current();
        if (!setting.valid)
            return read_failure(call, ReadOutcome::InvalidLimit, name,
                                nift::readlimit::invalid_message("inject", setting), error);
        if (!fs::is_regular_file(fs::symlink_status(call.target().path, regular_error)))
            return fail_host(call, host::HostStatus::Fatal,
                             host_error("runtime.invalid_operation", "inject: source is not readable"), error);
    }
    if (fs::is_directory(call.target().path)) {
        return fail_host(call, host::HostStatus::Fatal,
                         host_error("runtime.invalid_operation", std::string(name) + ": path is a directory"), error);
    }
    std::string data;
    if (std_stream_target(call.target().path)) context.publish_output();  // a read of our own output (D-073 #3)
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 6
    std::error_code size_error;
    if (fs::is_regular_file(call.target().path, size_error)) {
        const auto size = fs::file_size(call.target().path, size_error);
#if !(defined(NIFT_READ_LIMIT_MUTANT) && (NIFT_READ_LIMIT_MUTANT == 1 || NIFT_READ_LIMIT_MUTANT == 5))
        // B01: the read limit before the heap budget (legacy names the limit; NIFT_READ_LIMIT=1g used
        // to report "file exceeds the memory budget" for a file one byte over a 1 GiB limit)
        const auto setting = nift::readlimit::current();
        if (!size_error && setting.valid && size > setting.bytes)
            return fail_host(call, host::HostStatus::Limit,
                             host_error("limit.exceeded",
                                        nift::readlimit::exceeded_message(std::string(name),
                                                                          call.target().path.generic_string(),
                                                                          setting.bytes)),
                             error);
#endif
        if (!size_error && !reserve_result(context, static_cast<std::size_t>(size)))
            return fail_host(call, host::HostStatus::Limit,
                             host_error("limit.exceeded",
                                        std::string(name) + ": file exceeds the memory budget"),
                             error);
    }
#endif
    const ReadOutcome outcome = read_whole(context, call.target().path, data, name, error);
    if (json_source && (outcome == ReadOutcome::ReadFailed || outcome == ReadOutcome::OpenFailed))
        return fail_host(call, host::HostStatus::Fatal,
                         host_error("runtime.invalid_operation", "inject: source is not readable"), error);
    if (outcome != ReadOutcome::Ok) return read_failure(call, outcome, name, error, error);
    host::Observation obs;
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 4
    obs.bytes = data;
#endif
    const std::string size = std::to_string(data.size());
    if (mode == ReadMode::Bytes) {
        obs.size = data.size();
        if (finish_host(call, size, obs, error) != Status::Ok) return Status::Error;
        out = make_bytes(std::vector<std::uint8_t>(data.begin(), data.end()));
    } else if (mode == ReadMode::Cat) {
        if (finish_host(call, size, obs, error) != Status::Ok) return Status::Error;
        emit_program_output(context, data);  // D-072 #1: program output, in program order
        out = Value::null();
    } else {
        if (finish_host(call, size, obs, error) != Status::Ok) return Status::Error;
        out = make_string(std::move(data));
    }
    return Status::Ok;
}

} // namespace

Status host_inject_json(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                        std::string& error) {
    std::string spelling;
    if (!string_arg(args, argc, 0, spelling)) return fail(error, "inject: expected string path");
    const auto candidate = fs::u8path(spelling);
    if (spelling.empty() || spelling.front() == '~' ||
        std::any_of(candidate.begin(), candidate.end(), [](const auto& part) { return part == ".."; }))
        return fail(error, "inject: expected an unambiguous process output path");
    Value source;
    const Status read = read_source(context, args, argc, source, error, "inject", ReadMode::JsonText);
    if (read != Status::Ok) return read;
    Value result;
    const Status parsed = json_parse(context, string_view_of(source), result, error);
    if (parsed != Status::Ok) {
        if (parsed == Status::Error) error.insert(0, "inject: ");
        return parsed;
    }
    // Legacy handles top-level strings as literals after the strict JSON
    // check. Reuse its escape rules without running the expression evaluator.
    if (result.is(Kind::String)) {
        const auto text = string_view_of(source);
        const auto first = text.find_first_not_of(" \t\r\n");
        const auto last = text.find_last_not_of(" \t\r\n");
        if (text.find("$[") != std::string_view::npos)
            return fail(error, "inject: JSON string interpolation is unsupported");
        result = Value::null();
        if (!reserve_result(context, size_mul(text.size(), 2))) return Status::Memory;
        const auto body = text.substr(first + 1, last - first - 1);
        result = make_string(nift::detail::unescape_parameter_string(std::string(body)));
    }
    out = std::move(result);
    return Status::Ok;
}

Status host_open(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return read_source(context, args, argc, out, error, "open", ReadMode::Text);
}

Status host_open_bytes(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return read_source(context, args, argc, out, error, "open_bytes", ReadMode::Bytes);
}

Status host_cat(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return read_source(context, args, argc, out, error, "cat", ReadMode::Cat);
}

Status host_sha256_file(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    HostCall call(context.host, host::HostOp::ReadBytes, "sha256_file");
    if (resolve_arg(context, call, args, argc, 0, "sha256_file", error) != Status::Ok) return Status::Error;
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    if (std_stream_target(call.target().path)) context.publish_output();  // a read of our own output (D-073 #3)
    const auto result = nift::crypto::sha256_file(call.target().path);
    switch (result.status) {
        case nift::crypto::Sha256FileStatus::Ok: break;
        case nift::crypto::Sha256FileStatus::NotFound:
        case nift::crypto::Sha256FileStatus::OpenFailed:
            return fail_host(call, host::HostStatus::Recoverable,
                             host_error("io.open_failed", "sha256_file: cannot open path"), error);
        case nift::crypto::Sha256FileStatus::IsDirectory:
            return fail_host(call, host::HostStatus::Fatal,
                             host_error("runtime.invalid_operation", "sha256_file: path is a directory"), error);
        case nift::crypto::Sha256FileStatus::ReadFailed:
            return fail_host(call, host::HostStatus::Recoverable,
                             host_error("io.read_failed", "sha256_file: input failure"), error);
    }
    host::Observation obs;
    obs.size = result.size;
    if (finish_host(call, std::to_string(result.size), obs, error) != Status::Ok) return Status::Error;
    out = make_string(nift::crypto::sha256_hex(result.digest));
    return Status::Ok;
}

Status host_exists(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return metadata_op(context, args, argc, out, error, "exists", 0, false);
}

Status host_is_file(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return metadata_op(context, args, argc, out, error, "is_file", 1, false);
}

Status host_is_dir(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return metadata_op(context, args, argc, out, error, "is_dir", 2, false);
}

Status host_stat(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return metadata_op(context, args, argc, out, error, "stat", 0, true);
}


} // namespace nift::rt::natives
