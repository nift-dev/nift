// CP16 VM host built-ins: environment and platform information.
#include "rt/natives/host_env.h"

#include "rt/natives/host_util.h"

#include "Environment.h"
#include "FeatureFlags.h"
#include "ProcessEnvironment.h"
#include "rt/aggregate.h"
#include "rt/string.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>

namespace nift::rt::natives {

namespace host = nift::rt::host;

namespace {

bool string_arg(const Value* args, std::uint32_t argc, std::uint32_t index, std::string& out) {
    return index < argc && args[index].is(Kind::String) && (out = std::string(string_view_of(args[index])), true);
}

#if defined(_WIN32)
char** environment_block() { return _environ; }
#else
char** environment_block() { return ::environ; }
#endif

} // namespace

Status host_getenv(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string name;
    if (!string_arg(args, argc, 0, name)) return fail(error, "getenv: expected name");
    host::HostCall call(context.host, host::HostOp::Getenv, "getenv");
    call.set_arg(name);
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    const char* value = std::getenv(name.c_str());
    host::Observation obs;
    if (value) {
        obs.env_value = std::string(value);
        if (finish_host(call, "found", obs, error) != Status::Ok) return Status::Error;
        out = make_string(value);
    } else {
#if defined(NIFT_HOST_MUTANT) && NIFT_HOST_MUTANT == 2
        obs.env_value = std::string();
        if (finish_host(call, "found", obs, error) != Status::Ok) return Status::Error;
        out = make_string("");
#else
        if (finish_host(call, "unset", obs, error) != Status::Ok) return Status::Error;
        out = Value::null();
#endif
    }
    return Status::Ok;
}

Status host_env(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    (void)args;
    if (argc != 0) return fail(error, "env: expected no arguments");
    host::HostCall call(context.host, host::HostOp::EnvAll, "env");
    call.set_arg("");
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    out = make_object();
    for (char** entry = environment_block(); entry && *entry; ++entry) {
        const std::string text(*entry);
        const std::size_t eq = text.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        object_set(out, atom(text.substr(0, eq)), make_string(text.substr(eq + 1)));
    }
    if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    return Status::Ok;
}

Status host_setenv(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string name, value;
    if (!string_arg(args, argc, 0, name) || !string_arg(args, argc, 1, value))
        return fail(error, "setenv: expected name and value");
    host::HostCall call(context.host, host::HostOp::Setenv, "setenv");
    call.set_arg(name);
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
#if defined(_WIN32)
    const int result = _putenv_s(name.c_str(), value.c_str());
#else
    std::lock_guard<std::mutex> lock(nift_environment_mutex());
    const int result = ::setenv(name.c_str(), value.c_str(), 1);
#endif
    if (result != 0)
        return fail_host(call, host::HostStatus::Fatal, host_error("host.service_failed", "setenv: failed"), error);
    nift::flags::note_environment_changed();
    if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    out = Value::null();
    return Status::Ok;
}

Status host_unsetenv(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string name;
    if (!string_arg(args, argc, 0, name)) return fail(error, "unsetenv: expected name");
    host::HostCall call(context.host, host::HostOp::Unsetenv, "unsetenv");
    call.set_arg(name);
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    {
        std::lock_guard<std::mutex> lock(nift_environment_mutex());
#if defined(_WIN32)
        _putenv_s(name.c_str(), "");
#else
        ::unsetenv(name.c_str());
#endif
    }
    nift::flags::note_environment_changed();
    if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    out = Value::null();
    return Status::Ok;
}

Status host_os(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    (void)args;
    if (argc != 0) return fail(error, "os: expected no arguments");
    out = make_string(nift_environment::host_os());
    return Status::Ok;
}

Status host_arch(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    (void)args;
    if (argc != 0) return fail(error, "arch: expected no arguments");
    out = make_string(nift_environment::host_arch());
    return Status::Ok;
}

Status host_platform(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    (void)args;
    if (argc != 0) return fail(error, "platform: expected no arguments");
    host::HostCall call(context.host, host::HostOp::Platform, "platform");
    call.set_arg("");
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    host::Observation obs;
    obs.platform = "native";
    if (finish_host(call, obs.platform, obs, error) != Status::Ok) return Status::Error;
    out = make_string(obs.platform);
    return Status::Ok;
}

Status host_hardware_concurrency(const Context&, const Value* args, std::uint32_t argc, Value& out,
                                 std::string& error) {
    (void)args;
    if (argc != 0) return fail(error, "hardware_concurrency: expected no arguments");
    out = Value::integer(static_cast<std::int64_t>(std::max(1u, std::thread::hardware_concurrency())));
    return Status::Ok;
}

} // namespace nift::rt::natives
