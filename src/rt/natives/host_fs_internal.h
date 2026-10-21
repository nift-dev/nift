#pragma once
// CP16 host built-in internals shared by the read/navigation/write natives.
// rt-lint: cold-file host I/O helpers; the filesystem calls keep the lexically
// resolved paths legacy uses.
#include "rt/natives/host_util.h"
#include "FileSystem.h"
#include "ParserHelpers.h"
#include "Proc.h"
#include "ReadLimit.h"
#include "Sha256.h"
#include "rt/aggregate.h"
#include "rt/host/path.h"
#include "rt/string.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

namespace nift::rt::natives::host_fs_detail {

namespace fs = std::filesystem;
namespace host = nift::rt::host;
using host::HostCall;


struct Info {
    bool exists = false;
    bool error = false;
    std::string type;
    std::uint64_t size = 0;
    std::string message;
};

inline Info inspect(const fs::path& path) {
    Info info;
    std::error_code ec;
    const fs::file_status st = fs::status(path, ec);
    if (ec) {
        if (st.type() == fs::file_type::not_found || ec == std::errc::no_such_file_or_directory ||
            ec == std::errc::not_a_directory)
            return info;
        info.error = true;
        info.message = ec.message();
        return info;
    }
    if (!fs::exists(st)) return info;
    info.exists = true;
    switch (st.type()) {
        case fs::file_type::regular: info.type = "file"; break;
        case fs::file_type::directory: info.type = "directory"; break;
        case fs::file_type::symlink: info.type = "symlink"; break;
        default: info.type = "other"; break;
    }
    if (info.type == "file") {
        std::error_code size_error;
        const std::uintmax_t size = fs::file_size(path, size_error);
        if (size_error) {
            info.error = true;
            info.message = size_error.message();
        } else {
            info.size = static_cast<std::uint64_t>(size);
        }
    }
    return info;
}

enum class ReadOutcome { Ok, Limit, InvalidLimit, OpenFailed, ReadFailed };

template <class Buffer>
inline ReadOutcome read_whole(const fs::path& path, Buffer& out, std::string_view operation, std::string& error) {
    const auto setting = nift::readlimit::current();
    if (!setting.valid) {
        error = nift::readlimit::invalid_message(std::string(operation), setting);
        return ReadOutcome::InvalidLimit;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::error_code status_error;
        const fs::file_status st = fs::status(path, status_error);
        if ((!status_error && !fs::exists(st)) || status_error == std::errc::no_such_file_or_directory ||
            status_error == std::errc::not_a_directory)
            return ReadOutcome::OpenFailed;
        return ReadOutcome::OpenFailed;
    }
    std::uint64_t hint = 0;
    std::error_code size_error;
    if (fs::is_regular_file(path, size_error)) {
        const auto size = fs::file_size(path, size_error);
        if (!size_error) {
            if (size > setting.bytes) {
                error = nift::readlimit::exceeded_message(std::string(operation), path.generic_string(), setting.bytes);
                return ReadOutcome::Limit;
            }
            hint = size;
        }
    }
    switch (nift::readlimit::read_bounded(in, out, setting.bytes, std::numeric_limits<std::uint64_t>::max(), hint)) {
        case nift::readlimit::Status::Ok: return ReadOutcome::Ok;
        case nift::readlimit::Status::Exceeded:
            error = nift::readlimit::exceeded_message(std::string(operation), path.generic_string(), setting.bytes);
            return ReadOutcome::Limit;
        case nift::readlimit::Status::ReadError: return ReadOutcome::ReadFailed;
    }
    return ReadOutcome::ReadFailed;
}

inline Status read_failure(HostCall& call, ReadOutcome outcome, std::string_view name, const std::string& detail,
                    std::string& error) {
    if (outcome == ReadOutcome::InvalidLimit)
        return fail_host(call, host::HostStatus::Fatal, host_error("runtime.invalid_argument", detail), error);
    if (outcome == ReadOutcome::Limit)
        return fail_host(call, host::HostStatus::Limit, host_error("limit.exceeded", detail), error);
    if (outcome == ReadOutcome::ReadFailed)
        return fail_host(call, host::HostStatus::Recoverable,
                         host_error("io.read_failed", std::string(name) + ": input failure"), error);
    return fail_host(call, host::HostStatus::Recoverable,
                     host_error("io.open_failed", std::string(name) + ": cannot open path"), error);
}

inline bool string_arg(const Value* args, std::uint32_t argc, std::uint32_t index, std::string& out) {
    return index < argc && args[index].is(Kind::String) && (out = std::string(string_view_of(args[index])), true);
}

inline Status resolve_arg(const Context& context, HostCall& call, const Value* args, std::uint32_t argc,
                          std::uint32_t index, std::string_view name, std::string& error) {
    (void)context;
    std::string raw;
    if (!string_arg(args, argc, index, raw)) {
        error = std::string(name) + ": expected string path";
        return Status::Error;
    }
    if (call.resolve(raw) != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    return Status::Ok;
}

inline std::string type_label(const Info& info) {
    if (info.error) return "error";
    if (!info.exists) return "missing";
    return info.type;
}

inline Status metadata_op(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error,
                   std::string_view name, int mode, bool as_object) {
    HostCall call(context.host, host::HostOp::Stat, name);
    if (resolve_arg(context, call, args, argc, 0, name, error) != Status::Ok) return Status::Error;
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    const Info info = inspect(call.target().path);
    if (info.error)
        return fail_host(call, host::HostStatus::Recoverable,
                         host_error("io.metadata_failed", std::string(name) + ": " + info.message), error);
    host::Observation obs;
    obs.exists = info.exists;
    obs.kind = info.type;
    obs.metadata = as_object && info.type == "file";
    if (!as_object) {
        bool value = mode == 0 ? info.exists : mode == 1 ? (info.exists && info.type == "file")
                                                        : (info.exists && info.type == "directory");
#if defined(NIFT_HOST_MUTANT) && NIFT_HOST_MUTANT == 1
        if (name == "is_dir") value = info.exists && info.type == "file";
#endif
        if (finish_host(call, type_label(info), obs, error) != Status::Ok) return Status::Error;
        out = Value::boolean(value);
        return Status::Ok;
    }
    if (finish_host(call, type_label(info), obs, error) != Status::Ok) return Status::Error;
    out = make_object();
    object_set(out, atom("exists"), Value::boolean(info.exists));
    if (info.exists) {
        object_set(out, atom("type"), make_string(info.type));
        if (info.type == "file") object_set(out, atom("size"), Value::integer(static_cast<std::int64_t>(info.size)));
    }
    return Status::Ok;
}

inline std::string listing_key(const fs::path& path, bool glob) {
    return (glob ? "glob:" : "dir:") + path.generic_string();
}

inline std::string display_glob(const nift::detail::GlobMatch& match, const fs::path& canonical_base,
                         bool base_error) {
    if (base_error) return match.absolute.generic_string();
    if (match.resolve_leaf || !match.prefix || match.prefix->requires_leaf_resolution) {
        std::error_code ec;
        const fs::path canonical = fs::weakly_canonical(match.absolute, ec);
        return (ec ? match.absolute : canonical.lexically_relative(canonical_base)).generic_string();
    }
    auto& prefix = *match.prefix;
    if (!prefix.ready) {
        prefix.resolved = fs::weakly_canonical(prefix.original, prefix.error);
        if (!prefix.error) {
            const fs::path relative = prefix.resolved.lexically_relative(canonical_base);
            prefix.relative_fallback = relative.empty();
            bool only_parents = !relative.empty();
            for (const auto& component : relative)
                if (component != "..") { only_parents = false; break; }
            prefix.relative_fallback = prefix.relative_fallback || only_parents;
            if (relative != ".") prefix.relative_display = relative.generic_string();
        }
        prefix.ready = true;
    }
    if (prefix.error) return match.absolute.generic_string();
    if (prefix.relative_fallback)
        return (prefix.resolved / match.absolute.filename()).lexically_relative(canonical_base).generic_string();
    std::string rendered = prefix.relative_display;
    if (!rendered.empty()) rendered += '/';
    rendered += match.absolute.filename().generic_string();
    return rendered.empty() ? "." : rendered;
}

inline Status read_source(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error,
                   std::string_view name, bool bytes, bool cat) {
    HostCall call(context.host, bytes ? host::HostOp::ReadBytes : host::HostOp::Read, name);
    if (resolve_arg(context, call, args, argc, 0, name, error) != Status::Ok) return Status::Error;
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    if (fs::is_directory(call.target().path)) {
        return fail_host(call, host::HostStatus::Fatal,
                         host_error("runtime.invalid_operation", std::string(name) + ": path is a directory"), error);
    }
    std::string data;
    const ReadOutcome outcome = read_whole(call.target().path, data, name, error);
    if (outcome != ReadOutcome::Ok) return read_failure(call, outcome, name, error, error);
    if (!reserve_result(context, data.size()))
        return fail_host(call, host::HostStatus::Limit,
                         host_error("limit.exceeded", std::string(name) + ": file exceeds the memory budget"), error);
    host::Observation obs;
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 4
    obs.bytes = data;
#endif
    const std::string size = std::to_string(data.size());
    if (bytes) {
        obs.size = data.size();
        if (finish_host(call, size, obs, error) != Status::Ok) return Status::Error;
        out = make_bytes(std::vector<std::uint8_t>(data.begin(), data.end()));
    } else if (cat) {
        if (finish_host(call, size, obs, error) != Status::Ok) return Status::Error;
        if (!data.empty()) std::fwrite(data.data(), 1, data.size(), stdout);
        std::fflush(stdout);
        out = Value::null();
    } else {
        if (finish_host(call, size, obs, error) != Status::Ok) return Status::Error;
        out = make_string(std::move(data));
    }
    return Status::Ok;
}

inline Status resolve_plain(const Context& context, const std::string& raw, std::string_view name,
                            host::Resolved& out, std::string& error) {
    host::HostError host_error_value;
    const host::PathPolicy default_policy;
    const host::PathPolicy& policy = context.host.paths ? *context.host.paths : default_policy;
    const host::HostStatus status = host::resolve_path(policy, raw, name, out, host_error_value);
    if (status != host::HostStatus::Ok) {
        error = host_error_value.message;
        return host_status(status);
    }
    return Status::Ok;
}

inline Status collect_paths(const Context& context, const Value& value, std::string_view name,
                            std::vector<fs::path>& paths, bool& globbed, std::string& error) {
    std::vector<std::string> raws;
    if (value.is(Kind::String)) {
        raws.emplace_back(string_view_of(value));
    } else if (value.is(Kind::Array)) {
        for (const Value& item : array_items(value)) {
            if (!item.is(Kind::String)) {
                error = std::string(name) + ": path arrays must contain strings";
                return Status::Error;
            }
            raws.emplace_back(string_view_of(item));
        }
    } else {
        error = std::string(name) + ": expected string path or array of paths";
        return Status::Error;
    }
    for (const std::string& raw : raws) {
        host::Resolved resolved;
        if (resolve_plain(context, raw, name, resolved, error) != Status::Ok) return Status::Error;
        if (nift::detail::glob_has_magic(raw)) {
            globbed = true;
            const auto matches = nift::detail::glob_expand(resolved.path);
            for (const fs::path& match : matches) {
                host::Resolved checked;
                if (resolve_plain(context, match.generic_string(), name, checked, error) != Status::Ok)
                    return Status::Error;
                paths.push_back(checked.path);
            }
        } else {
            paths.push_back(resolved.path);
        }
    }
    return Status::Ok;
}


} // namespace nift::rt::natives::host_fs_detail
