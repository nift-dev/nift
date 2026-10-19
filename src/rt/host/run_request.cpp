#include "rt/host/run_request.h"

#include <filesystem>
#include <fstream>

namespace nift::rt::host {

namespace fs = std::filesystem;

OutputMode output_mode_for(RunMode mode) {
    switch (mode) {
        case RunMode::Capture: return OutputMode::FileCapture;  // today's run(), unchanged (D-034)
        case RunMode::Pipe: return OutputMode::PipeCapture;     // explicit opt-in (D-034)
        case RunMode::Stream: return OutputMode::Stream;
        case RunMode::Interactive: return OutputMode::Interactive;
        case RunMode::Detached: return OutputMode::Detached;
    }
    return OutputMode::FileCapture;
}

HostStatus read_stdin_file(const HostCallEnv& env, const std::string& raw, const RunRequestContext& context,
                           std::string& bytes, HostError& error, std::string* open_path) {
    const std::string& b = context.builtin;
    HostCall call(env, HostOp::Read, b);
    if (call.resolve(raw, PathSemantics::Os) != HostStatus::Ok) { error = call.error(); return call.status(); }
    if (call.begin() != HostStatus::Ok) { error = call.error(); return call.status(); }
    const fs::path& path = call.target().for_open();
    if (open_path) *open_path = path.string();
    std::error_code ec;
    if (fs::is_directory(path, ec)) {
        make_error(error, "io.open_failed", b + ": cannot open stdin file " + raw + ": it is a directory", EISDIR);
        return call.fail(HostStatus::Recoverable, error);
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        const int sys = errno;
        make_error(error, "io.open_failed", b + ": cannot open stdin file " + raw, sys);
        return call.fail(HostStatus::Recoverable, error);
    }
    // Bounded while reading (never from st_size): devices and FIFOs too.
    bytes.clear();
    char chunk[64 * 1024];
    for (;;) {
        in.read(chunk, sizeof chunk);
        const std::streamsize n = in.gcount();
        if (n > 0) {
            if (static_cast<std::uint64_t>(bytes.size()) + static_cast<std::uint64_t>(n) > context.read_limit) {
                bytes.clear();
                make_error(error, "limit.exceeded", b + ": stdin file " + raw + " exceeds the read limit of " +
                                                        std::to_string(context.read_limit) + " bytes (NIFT_READ_LIMIT)");
                return call.fail(HostStatus::Limit, error);
            }
            bytes.append(chunk, static_cast<std::size_t>(n));
        }
        if (!in) break;
    }
    if (in.bad()) {
        bytes.clear();
        make_error(error, "io.read_failed", b + ": cannot read stdin file " + raw);
        return call.fail(HostStatus::Recoverable, error);
    }
    Observation obs;
    obs.bytes = bytes;  // dependency + content hash in builds (the exact bytes the child receives)
    return call.succeed(std::to_string(bytes.size()), obs);
}

HostStatus build_spawn_request(const HostCallEnv& env, const RunOptions& options, const std::vector<std::string>& argv,
                               const RunRequestContext& context, SpawnRequest& out, HostError& error) {
    HostStatus status = check_run_context(options, context.in_build, context.builtin, error);
    if (status != HostStatus::Ok) return status;
    if (argv.empty()) return make_error(error, "runtime.arity_error", context.builtin + ": expected a program");
    SpawnRequest req;
    SpawnStage stage;
    stage.program = argv.front();
    stage.args.assign(argv.begin() + 1, argv.end());
    req.stages.push_back(std::move(stage));
    req.mode = output_mode_for(options.mode);
    req.timeout = options.timeout;
    req.kill_after = options.kill_after ? *options.kill_after : kDefaultKillAfter;
    req.capture_limit = context.read_limit;
    req.in_build = context.in_build;
    req.flush_output = context.flush_output;
    req.builtin = context.builtin;
    switch (options.stdin_source.kind) {
        case StdinOption::Kind::Inherit:
            req.stdin_mode = StdinMode::Inherit;
            break;
        case StdinOption::Kind::Null:
            req.stdin_mode = StdinMode::Null;
            break;
        case StdinOption::Kind::Text:
            req.stdin_mode = StdinMode::Text;
            req.stdin_text = options.stdin_source.value;
            break;
        case StdinOption::Kind::File: {
            std::string bytes, open_path;
            status = read_stdin_file(env, options.stdin_source.value, context, bytes, error, &open_path);
            if (status != HostStatus::Ok) return status;
            if (options.mode == RunMode::Detached) {
                // A detached process is never connected to Nift: it reads the file itself.
                req.stdin_mode = StdinMode::Null;
                req.stages[0].stdin_path = open_path;
            } else {
                req.stdin_mode = StdinMode::Text;
                req.stdin_text = std::move(bytes);
            }
            break;
        }
    }
    out = std::move(req);
    return HostStatus::Ok;
}

} // namespace nift::rt::host
