#include "rt/natives/host_process.h"

#include "Proc.h"
#include "ReadLimit.h"
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/build.h"
#include "rt/host/process_call.h"
#include "rt/heap_account.h"
#include "rt/natives/host_util.h"
#include "rt/string.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace nift::rt::natives {
namespace {
bool process_scalar(const Value& value) {
    return !value.is(Kind::Null) && renderable_scalar(value);
}

// Snapshot array operands before evaluating the next operand. Legacy turns
// each operand into argv strings at this point, even if later code edits it.
Status prepare_argument(const Context& context, const Value& value, bool spread, bool command,
                        Value& out, std::string& error) {
    const char* scalar_error = command ? "cmd: arguments must be scalar" : "run: arguments must be scalar";
    if (!value.is(Kind::Array)) {
        if (spread) return fail(error, "run: spread argument must be an array");
        if (!process_scalar(value)) return fail(error, scalar_error);
        out = value;
        return Status::Ok;
    }
    const auto& items = array_items(value);
    for (const Value& item : items)
        if (!process_scalar(item)) return fail(error, scalar_error);
    if (!reserve_result(context, size_add(sizeof(Value), size_mul(items.size(), sizeof(Value)))))
        return Status::Memory;
    ArrayBuilder copy(items.size());
    for (const Value& item : items) copy.push(item);
    out = copy.finish();
    return Status::Ok;
}

Status redirect_path(const Context& context, std::string_view spelling, std::string& target, std::string& error) {
    if (spelling.empty()) return Status::Ok;
    if (spelling.find('\0') != std::string_view::npos) return fail(error, "stdout: expected a path");
    host::PathPolicy policy;
    if (context.host.paths) policy = *context.host.paths;
    policy.standalone = false;  // command redirection uses its literal path
    host::Resolved resolved;
    host::HostError rejection;
    const auto status = host::resolve_path(policy, spelling, "run", resolved, rejection, host::PathSemantics::Os);
    if (status != host::HostStatus::Ok) {
        if (context.host.error_code) *context.host.error_code = rejection.code;
        error = rejection.message;
        return host_status(status);
    }
    target = resolved.for_open().u8string();
    return Status::Ok;
}

Status launch(const Context& context, ProcessSpec& stage, std::uint64_t limit, ProcessResult& result,
              std::string& error) {
#ifdef _WIN32
    std::string resolved;
    if (!nift_find_executable(stage.program, resolved)) {
        result.exit_code = 127;
        result.error = "executable not found: " + stage.program;
        return Status::Ok;
    }
    std::string extension = std::filesystem::u8path(resolved).extension().u8string();
    for (char& c : extension) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // The Windows legacy launcher also accepts .cmd/.bat and .f files.
    // Restrict the VM form to an exact native executable before opening outputs.
    if (extension != ".exe" && extension != ".com")  // rt-lint: allow-string-compare host launch policy
        return fail(error, "run: expected a native Windows executable");
    auto policy = context.host;
    policy.trace = nullptr;  // the Windows launcher emits the existing spawn trace
    host::HostCall call(policy, host::HostOp::Spawn, "run");
    std::vector<std::string> argv{stage.program};
    argv.insert(argv.end(), stage.args.begin(), stage.args.end());
    std::error_code ec;
    call.set_spawn(argv, "", std::filesystem::current_path(ec).generic_string());
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    ProcessSpec resolved_stage = std::move(stage);
    resolved_stage.program = std::filesystem::absolute(std::filesystem::u8path(resolved)).u8string();
    result = nift_run_pipeline({std::move(resolved_stage)}, true, false, limit);
    return finish_host(call, result.launched ? "launched" : "launch_failed", host::Observation{}, error);
#else
    host::SpawnRequest request;
    request.terminal = host::TerminalPolicy::Never;
    request.capture_limit = limit;
    host::SpawnStage child;
    child.program = stage.program;
    child.allow_enoexec_shell = false;
    child.args = std::move(stage.args);
    child.stdout_path = std::move(stage.stdout_path);
    request.stages.push_back(std::move(child));
    host::HostStatus status;
    host::HostError failure;
    auto group = host::start_traced(context.host, std::move(request), status, failure, true);
    if (!group) {
        error = failure.message;
        return host_status(status);
    }
    const auto& completed = group->wait();
    result.exit_code = completed.exit_code;
    result.launched = completed.launched;
    result.error = completed.error;
    result.out = completed.out;
    result.err = completed.err;
    result.capture_limit_exceeded = completed.capture_limit_exceeded;
    return Status::Ok;
#endif
}

Status run(const Context& context, const Value* args, std::uint32_t argc, bool redirect, Value& out,
           std::string& error) {
    if (argc < (redirect ? 2u : 1u) || !args[0].is(Kind::String)) return Status::Internal;
    ProcessSpec stage;
    stage.program = string_view_of(args[0]);
    if (redirect && !args[--argc].is(Kind::String)) return fail(error, "stdout: expected string");
    if (context.in_worker || (redirect && std::getenv("NIFT_NO_PROCESS")))
        return fail(error, "run: external process execution disabled");
    if (redirect) {
        const auto status = redirect_path(context, string_view_of(args[argc]), stage.stdout_path, error);
        if (status != Status::Ok) return status;
    }
    std::size_t argv_bytes = stage.program.size();
    for (std::uint32_t k = 1; k < argc; ++k) {
        const auto append = [&](const Value& value) {
            if (value.is(Kind::String)) {
                const auto text = string_view_of(value);
                if (text.find('\0') != std::string_view::npos)
                    return fail(error, "run: argument contains a NUL byte");
                argv_bytes = size_add(argv_bytes, text.size());
            } else {
                argv_bytes = size_add(argv_bytes, 64);
            }
            argv_bytes = size_add(argv_bytes, 2 * sizeof(std::string));
            // The platform launcher needs its own argv buffer and quoting workspace.
            if (!reserve_result(context, size_mul(argv_bytes, 4))) return Status::Memory;
            std::string rendered;
            append_scalar(value, rendered);
            stage.args.push_back(std::move(rendered));
            return Status::Ok;
        };
        if (args[k].is(Kind::Array)) {
            for (const Value& member : array_items(args[k])) {
                const auto status = append(member);
                if (status != Status::Ok) return status;
            }
        } else {
            const auto status = append(args[k]);
            if (status != Status::Ok) return status;
        }
    }
    const auto setting = readlimit::current();
    if (!setting.valid) return fail(error, readlimit::invalid_message("run", setting));
    const std::string display_program = stage.program;
    auto capture_limit = setting.bytes;
    if (context.heap_limit != Context::kNoHeapLimit) {
        const auto limit = context.heap_limit;
        const auto used = size_add(live_heap_bytes(), size_mul(argv_bytes, 4));
        if (used >= limit || limit - used < 4) return Status::Memory;
        // Two streams, with up to two live generations of each buffer.
        capture_limit = std::min<std::uint64_t>(capture_limit, (limit - used) / 4);
    }
    ProcessResult result;
    const auto status = launch(context, stage, capture_limit, result, error);
    if (status != Status::Ok) return status;
    if (!result.capture_limit_exceeded.empty()) {
        if (capture_limit < setting.bytes) return Status::Memory;
        error = readlimit::exceeded_message("run", "captured " + result.capture_limit_exceeded + " of " +
                                           display_program, setting.bytes);
        return Status::Limit;
    }
    if (!reserve_result(context, size_add(size_add(result.out.size(), result.err.size()), result.error.size())))
        return Status::Memory;
    build::Object report;
    report.set(atom("exit_code"), Value::integer(result.exit_code));
    report.set(atom("stdout"), make_string(std::move(result.out)));
    report.set(atom("stderr"), make_string(std::move(result.err)));
    report.set(atom("launched"), Value::boolean(result.launched));
    if (!result.error.empty()) report.set(atom("error"), make_string(std::move(result.error)));
    out = std::move(report).publish();
    return Status::Ok;
}
} // namespace

Status process_check(const Context& context, const Value*, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 0) return Status::Internal;
    if (context.in_worker || std::getenv("NIFT_NO_PROCESS"))
        return fail(error, "run: external process execution disabled");
    out = Value::null();
    return Status::Ok;
}

Status process_argument(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return argc == 1 || argc == 2 ? prepare_argument(context, args[0], false, argc == 2, out, error)
                                  : Status::Internal;
}

Status process_spread(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return argc == 1 || argc == 2 ? prepare_argument(context, args[0], true, argc == 2, out, error)
                                  : Status::Internal;
}

Status process_run(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return run(context, args, argc, false, out, error);
}

Status process_run_stdout(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                          std::string& error) {
    return run(context, args, argc, true, out, error);
}
} // namespace nift::rt::natives
