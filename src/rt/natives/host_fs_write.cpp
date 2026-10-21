// CP16 VM host built-ins: writes and executable lookup.
// rt-lint: cold-file host I/O
#include "rt/natives/host_fs_write.h"

#include "rt/natives/host_fs_internal.h"

namespace nift::rt::natives {
using namespace host_fs_detail;

Status host_touch(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    HostCall call(context.host, host::HostOp::Touch, "touch");
    if (resolve_arg(context, call, args, argc, 0, "touch", error) != Status::Ok) return Status::Error;
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    std::ofstream file(call.target().path, std::ios::app);
    if (!file)
        return fail_host(call, host::HostStatus::Recoverable,
                         host_error("io.create_failed", "touch: cannot open path"), error);
    if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    out = Value::null();
    return Status::Ok;
}

Status host_mkdir(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    HostCall call(context.host, host::HostOp::Mkdir, "make_dir");
    if (resolve_arg(context, call, args, argc, 0, "make_dir", error) != Status::Ok) return Status::Error;
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    std::error_code ec;
    fs::create_directories(call.target().path, ec);
    if (ec)
        return fail_host(call, host::HostStatus::Recoverable,
                         host_error("io.create_failed", "make_dir: " + ec.message()), error);
    if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    out = Value::null();
    return Status::Ok;
}

Status host_remove(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc == 0) return fail(error, "remove: expected at least one path");
    std::vector<fs::path> paths;
    bool globbed = false;
    for (std::uint32_t i = 0; i < argc; ++i) {
        if (collect_paths(context, args[i], "remove", paths, globbed, error) != Status::Ok) return Status::Error;
    }
    (void)globbed;
    for (const fs::path& path : paths) {
        HostCall call(context.host, host::HostOp::Remove, "remove");
        call.adopt(path);
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            const host::HostError directory_error =
                host_error("runtime.invalid_operation", "remove: directories are not removed recursively");
            return fail_host(call, host::HostStatus::Fatal, directory_error, error);
        }
        if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
        fs::remove(path, ec);
        if (ec)
            return fail_host(call, host::HostStatus::Recoverable,
                             host_error("io.remove_failed", "remove: " + ec.message()), error);
        if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    }
    out = Value::null();
    return Status::Ok;
}

Status host_cp(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc < 2) return fail(error, "copy: expected source(s) and destination");
    std::vector<fs::path> sources;
    bool globbed = false;
    for (std::uint32_t i = 0; i + 1 < argc; ++i)
        if (collect_paths(context, args[i], "copy", sources, globbed, error) != Status::Ok) return Status::Error;
    std::vector<fs::path> destinations;
    bool destination_glob = false;
    if (collect_paths(context, args[argc - 1], "copy", destinations, destination_glob, error) != Status::Ok) {
        if (error.find("path escapes configured filesystem root") != std::string::npos)
            error = "copy: destination must be one path";
        return Status::Error;
    }
    (void)globbed;
    if (sources.empty()) return fail(error, "copy: glob matched no source files");
    if (destinations.size() != 1) return fail(error, "copy: destination must be one path");
    const fs::path& destination = destinations.front();
    std::error_code ec;
    const bool destination_directory = fs::is_directory(destination, ec);
    if (sources.size() > 1 && !destination_directory)
        return fail(error, "copy: destination must be an existing directory for multiple sources");
    for (const fs::path& source : sources) {
        const fs::path target = destination_directory ? destination / source.filename() : destination;
        HostCall call(context.host, host::HostOp::CopyTo, "copy");
        call.adopt(target);
        if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
        std::error_code copy_error;
        fs::copy_file(source, target, fs::copy_options::overwrite_existing, copy_error);
        if (copy_error)
            return fail_host(call, host::HostStatus::Recoverable,
                             host_error("io.copy_failed", "copy: " + copy_error.message()), error);
        if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    }
    out = Value::null();
    return Status::Ok;
}

Status host_mv(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc < 2) return fail(error, "move: expected source(s) and destination");
    std::vector<fs::path> sources;
    bool globbed = false;
    for (std::uint32_t i = 0; i + 1 < argc; ++i)
        if (collect_paths(context, args[i], "move", sources, globbed, error) != Status::Ok) return Status::Error;
    std::vector<fs::path> destinations;
    bool destination_glob = false;
    if (collect_paths(context, args[argc - 1], "move", destinations, destination_glob, error) != Status::Ok) {
        if (error.find("path escapes configured filesystem root") != std::string::npos)
            error = "move: destination must be one path";
        return Status::Error;
    }
    (void)globbed;
    if (sources.empty()) return fail(error, "move: glob matched no source files");
    if (destinations.size() != 1) return fail(error, "move: destination must be one path");
    const fs::path& destination = destinations.front();
    std::error_code directory_error;
    const bool destination_directory = fs::is_directory(destination, directory_error);
    if (sources.size() > 1 && !destination_directory)
        return fail(error, "move: destination must be an existing directory for multiple sources");
    for (const fs::path& source : sources) {
        const fs::path target = destination_directory ? destination / source.filename() : destination;
        HostCall to(context.host, host::HostOp::MoveTo, "move");
        HostCall from(context.host, host::HostOp::MoveFrom, "move");
        to.adopt(target);
        from.adopt(source);
        if (to.begin() != host::HostStatus::Ok) return fail_host(to, to.status(), to.error(), error);
        if (from.begin() != host::HostStatus::Ok) {
            const host::HostStatus status = from.status();
            const host::HostError host_error_value = from.error();
            to.fail(status, host_error_value);
            return fail_host(from, status, host_error_value, error);
        }
        std::error_code move_error;
        bool partial = false;
        ::filesystem::move_path(source, target, move_error, &partial);
        if (move_error) {
            const std::string message = partial
                ? "move: copied to " + target.generic_string() + " but could not remove the source " +
                      source.generic_string() + ": " + move_error.message()
                : "move: " + move_error.message();
            const host::HostStatus status = host::HostStatus::Recoverable;
            const host::HostError host_error_value = host_error("io.move_failed", message);
            to.fail(status, host_error_value);
            from.fail(status, host_error_value);
            error = message;
            return host_status(status);
        }
        if (finish_host(to, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
        if (finish_host(from, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    }
    out = Value::null();
    return Status::Ok;
}

Status host_which(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string name;
    (void)argc;
    if (!string_arg(args, argc, 0, name)) return fail(error, "which: expected command name");
    static const std::unordered_set<std::string> builtins = {
        "ls", "cd", "pwd", "cat", "touch", "exists", "cp", "mv", "rm", "mkdir", "copy", "move",
        "remove", "make_dir", "run", "cmd",
    };
    if (builtins.count(name)) {
        out = make_string("nift:" + name);
        return Status::Ok;
    }
    std::string path;
    if (nift_find_executable(name, path)) out = make_string(path);
    else out = Value::null();
    return Status::Ok;
}


} // namespace nift::rt::natives
