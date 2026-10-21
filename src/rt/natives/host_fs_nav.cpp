// CP16 VM host built-ins: directory listing, pwd and cd.
// rt-lint: cold-file host I/O
#include "rt/natives/host_fs_nav.h"

#include "rt/natives/host_fs_internal.h"

namespace nift::rt::natives {
using namespace host_fs_detail;

Status host_ls(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc > 1) return fail(error, "ls: expected zero or one path/pattern");
    std::string raw;
    const bool has_arg = argc == 1;
    if (has_arg && !string_arg(args, argc, 0, raw)) return fail(error, "ls: expected string path");
    const bool glob = has_arg && nift::detail::glob_has_magic(raw);
    HostCall call(context.host, glob ? host::HostOp::Glob : host::HostOp::List, "ls");
    if (has_arg) {
        if (call.resolve(raw) != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    } else {
        call.adopt(fs::current_path() / ".");
    }
    call.set_listing_key(listing_key(call.target().path, glob));
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    std::vector<std::string> names;
    if (glob) {
        if (has_arg && fs::path(raw).is_absolute()) {
            for (const fs::path& match : nift::detail::glob_expand(call.target().path))
                names.push_back(match.generic_string());
        } else {
            const auto matches = nift::detail::glob_expand_shared(call.target().path);
            std::error_code base_error;
            const fs::path canonical_base = matches.empty()
                ? fs::path()
                : fs::weakly_canonical(fs::current_path(), base_error);
            for (const auto& match : matches) names.push_back(display_glob(match, canonical_base, bool(base_error)));
        }
    } else {
        std::error_code ec;
        if (!fs::is_directory(call.target().path, ec)) {
            if (ec)
                return fail_host(call, host::HostStatus::Recoverable,
                                 host_error("io.directory_read_failed", "ls: " + ec.message()), error);
            return fail_host(call, host::HostStatus::Fatal,
                             host_error("runtime.invalid_operation", "ls: path is not a readable directory"), error);
        }
        for (fs::directory_iterator it(call.target().path, ec), end; !ec && it != end; it.increment(ec))
            names.push_back(it->path().filename().generic_string());
        if (ec)
            return fail_host(call, host::HostStatus::Recoverable,
                             host_error("io.directory_read_failed", "ls: " + ec.message()), error);
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 3
        std::sort(names.begin(), names.end());
#endif
    }
    if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    out = make_array(names.size());
    for (const std::string& name : names) array_push(out, make_string(name));
    return Status::Ok;
}

Status host_pwd(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    (void)args;
    if (argc != 0) return fail(error, "pwd: expected no arguments");
    out = make_string(fs::current_path().generic_string());
    return Status::Ok;
}

Status host_cd(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    HostCall call(context.host, host::HostOp::Chdir, "cd");
    if (resolve_arg(context, call, args, argc, 0, "cd", error) != Status::Ok) return Status::Error;
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    std::error_code ec;
    fs::current_path(call.target().path, ec);
    if (ec)
        return fail_host(call, host::HostStatus::Recoverable,
                         host_error("io.change_directory_failed", "cd: " + ec.message()), error);
    if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    out = Value::null();
    return Status::Ok;
}


} // namespace nift::rt::natives
