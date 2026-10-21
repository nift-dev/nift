#pragma once
// CP16 host built-in internals shared by the read/navigation/write natives.
// rt-lint: cold-file host I/O helpers; the filesystem calls keep the lexically
// resolved paths legacy uses.
#include "rt/natives/host_util.h"
#include "rt/natives/host_fs_stream.h"
#include "FileSystem.h"
#include "ParserHelpers.h"
#include "Proc.h"
#include "ReadLimit.h"
#include "Sha256.h"
#include "rt/aggregate.h"
#include "rt/heap.h"
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



inline Status read_failure(HostCall& call, ReadOutcome outcome, std::string_view name, const std::string& detail,
                    std::string& error) {
    if (outcome == ReadOutcome::InvalidLimit)
        return fail_host(call, host::HostStatus::Fatal, host_error("runtime.invalid_argument", detail), error);
    if (outcome == ReadOutcome::Limit)
        return fail_host(call, host::HostStatus::Limit, host_error("limit.exceeded", detail), error);
    if (outcome == ReadOutcome::Budget)
        return fail_host(call, host::HostStatus::Limit,
                         host_error("limit.exceeded", std::string(name) + ": file exceeds the memory budget"), error);
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
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 6
    std::error_code size_error;
    if (fs::is_regular_file(call.target().path, size_error)) {
        const auto size = fs::file_size(call.target().path, size_error);
        if (!size_error && !reserve_result(context, static_cast<std::size_t>(size)))
            return fail_host(call, host::HostStatus::Limit,
                             host_error("limit.exceeded",
                                        std::string(name) + ": file exceeds the memory budget"),
                             error);
    }
#endif
    const ReadOutcome outcome = read_whole(context, call.target().path, data, name, error);
    if (outcome != ReadOutcome::Ok) return read_failure(call, outcome, name, error, error);
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

inline std::size_t string_storage(const std::string& text) {
    return sizeof(std::string) + (text.capacity() > 15 ? text.capacity() + 1 : 0);
}

inline std::size_t string_heap_storage(const std::string& text) {
    return sizeof(StringObject) + (text.capacity() > 15 ? text.capacity() + 1 : 0);
}

inline std::size_t strings_retained(const std::vector<std::string>& values) {
    std::size_t total = 0;
    for (const std::string& value : values) total = size_add(total, string_storage(value));
    return total;
}

inline Status collect_glob_paths(const Context& context, const fs::path& path, std::string_view name,
                                 std::vector<std::string>& out, std::string& error, bool display_relative = false,
                                 const fs::path& canonical_base = {}) {
#if defined(NIFT_HOST_MUTANT) && NIFT_HOST_MUTANT == 7
    constexpr bool enforce_budget = false;
#else
    constexpr bool enforce_budget = true;
#endif
    std::vector<std::string> matches;
    std::size_t accounted = 0;
    bool policy_failure = false;
    const bool completed = nift::detail::glob_visit(path, [&](const fs::path& match) {
        std::string spelling = match.generic_string();
        if (context.host.paths) {
            host::Resolved checked;
            host::HostError policy_error;
            if (host::resolve_path(*context.host.paths, spelling, name, checked, policy_error) !=
                host::HostStatus::Ok) {
                error = policy_error.message;
                policy_failure = true;
                return false;
            }
        }
        if (display_relative) {
            std::error_code ec;
            const fs::path canonical = fs::weakly_canonical(match, ec);
            spelling = (ec ? match : canonical.lexically_relative(canonical_base)).generic_string();
        }
#if defined(NIFT_HOST_MUTANT) && NIFT_HOST_MUTANT == 9
        accounted = size_add(accounted, spelling.size() + sizeof(fs::path) + 128);
#else
        accounted = size_add(accounted, string_storage(spelling));
#endif
        if (matches.size() == matches.capacity()) {
            const std::size_t old_capacity = matches.capacity();
            const std::size_t capacity =
                old_capacity < 8 ? 8
                                : (old_capacity > kSizeOverflow / 2 ? kSizeOverflow : old_capacity * 2);
            const std::size_t vectors = size_add(size_mul(capacity, sizeof(std::string)),
                                                 size_mul(old_capacity, sizeof(std::string)));
            if (enforce_budget && !reserve_result(context, size_add(accounted, vectors))) {
                error = std::string(name) + ": result exceeds the memory budget";
                return false;
            }
            matches.reserve(capacity);
        }
        if (enforce_budget && !reserve_result(context, accounted)) {
            error = std::string(name) + ": result exceeds the memory budget";
            return false;
        }
        matches.push_back(std::move(spelling));
        return true;
    });
    if (!completed) return policy_failure ? Status::Error : Status::Limit;
    std::sort(matches.begin(), matches.end());
    matches.erase(std::unique(matches.begin(), matches.end()), matches.end());
    out = std::move(matches);
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
            std::vector<std::string> matches;
            const Status budget = collect_glob_paths(context, resolved.path, name, matches, error);
            if (budget != Status::Ok) return budget;
            const std::size_t conversion = size_add(strings_retained(matches),
                                                    size_mul(matches.size(), sizeof(fs::path)));
            if (!reserve_result(context, conversion)) {
                error = std::string(name) + ": result exceeds the memory budget";
                return Status::Limit;
            }
            const std::size_t old_capacity = paths.capacity();
            const std::size_t needed = size_add(paths.size(), matches.size());
            std::size_t new_capacity = old_capacity < 8 ? 8 : size_mul(old_capacity, 2);
            while (new_capacity < needed) new_capacity = size_mul(new_capacity, 2);
            if (new_capacity > paths.capacity()) {
                const std::size_t vectors = size_add(size_mul(new_capacity, sizeof(fs::path)),
                                                     size_mul(old_capacity, sizeof(fs::path)));
                if (!reserve_result(context, size_add(conversion, vectors))) {
                    error = std::string(name) + ": result exceeds the memory budget";
                    return Status::Limit;
                }
                paths.reserve(new_capacity);
            }
            for (std::string& match : matches) paths.emplace_back(std::move(match));
        } else {
            paths.push_back(resolved.path);
        }
    }
    return Status::Ok;
}


} // namespace nift::rt::natives::host_fs_detail
