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
        if (call.resolve(".") != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    }
    call.set_listing_key(listing_key(call.target().path, glob));
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    std::vector<std::string> names;
    std::size_t accounted = 0;
    const auto account = [&](std::string_view value) {
        accounted = size_add(accounted, value.size() + 64);
        return reserve_result(context, accounted);
    };
    if (glob) {
        std::vector<std::string> matches;
        const bool absolute = !has_arg || fs::path(raw).is_absolute();
        const fs::path base = fs::current_path();
        std::error_code base_error;
        const fs::path canonical_base = absolute ? fs::path() : fs::weakly_canonical(base, base_error);
        const Status budget = collect_glob_paths(context, call.target().path, "ls", matches, error,
                                                 !absolute, canonical_base);
        if (budget != Status::Ok) {
            if (budget == Status::Limit)
                return fail_host(call, host::HostStatus::Limit,
                                 host_error("limit.exceeded", error), error);
            return fail_host(call, host::HostStatus::Fatal,
                             host_error("runtime.invalid_operation", error), error);
        }
        std::size_t retained = strings_retained(matches);
        std::size_t heap_strings = 0;
        for (const std::string& match : matches) heap_strings = size_add(heap_strings, string_heap_storage(match));
        const std::size_t array_storage = size_add(size_mul(matches.size(), sizeof(Value)), retained);
        if (!reserve_result(context, size_add(size_add(array_storage, heap_strings),
                                              size_mul(matches.capacity(), sizeof(std::string))))) {
            return fail_host(call, host::HostStatus::Limit,
                             host_error("limit.exceeded", "ls: result exceeds the memory budget"), error);
        }
        out = make_array(matches.size());
        for (std::string& match : matches) array_push(out, make_string(std::move(match)));
        if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
        return Status::Ok;
    } else {
        std::error_code ec;
        if (!fs::is_directory(call.target().path, ec)) {
            if (ec)
                return fail_host(call, host::HostStatus::Recoverable,
                                 host_error("io.directory_read_failed", "ls: " + ec.message()), error);
            return fail_host(call, host::HostStatus::Fatal,
                             host_error("runtime.invalid_operation", "ls: path is not a readable directory"), error);
        }
        for (fs::directory_iterator it(call.target().path, ec), end; !ec && it != end; it.increment(ec)) {
            const std::string name = it->path().filename().generic_string();
            if (!account(name))
                return fail_host(call, host::HostStatus::Limit,
                                 host_error("limit.exceeded", "ls: result exceeds the memory budget"), error);
            names.push_back(name);
        }
        if (ec)
            return fail_host(call, host::HostStatus::Recoverable,
                             host_error("io.directory_read_failed", "ls: " + ec.message()), error);
#if !defined(NIFT_HOST_MUTANT) || NIFT_HOST_MUTANT != 3
        std::sort(names.begin(), names.end());
#endif
    }
    // Legacy records a directory listing's result as its entry count.
    if (finish_host(call, std::to_string(names.size()), host::Observation{}, error) != Status::Ok) return Status::Error;
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
