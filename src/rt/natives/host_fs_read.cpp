// CP16 VM host built-ins: whole-file reads and metadata.
// rt-lint: cold-file host I/O
#include "rt/natives/host_fs_read.h"

#include "rt/natives/host_fs_internal.h"

namespace nift::rt::natives {
using namespace host_fs_detail;

Status host_open(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return read_source(context, args, argc, out, error, "open", false, false);
}

Status host_open_bytes(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return read_source(context, args, argc, out, error, "open_bytes", true, false);
}

Status host_cat(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return read_source(context, args, argc, out, error, "cat", false, true);
}

Status host_sha256_file(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    HostCall call(context.host, host::HostOp::ReadBytes, "sha256_file");
    if (resolve_arg(context, call, args, argc, 0, "sha256_file", error) != Status::Ok) return Status::Error;
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
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
