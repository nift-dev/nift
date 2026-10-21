// CP16 VM host built-ins: clock, sleep and secure random bytes.
#include "rt/natives/host_time.h"

#include "rt/natives/host_util.h"

#include "ParserHelpers.h"
#include "RuntimeValue.h"
#include "rt/number.h"
#include "rt/string.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace nift::rt::natives {

namespace host = nift::rt::host;

namespace {
bool number_arg(const Value* args, std::uint32_t argc, const Value*& value) {
    if (argc != 1) return false;
    value = &args[0];
    return true;
}
} // namespace

Status host_epoch(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    (void)args;
    if (argc != 0) return fail(error, "epoch: expected no arguments");
    host::HostCall call(context.host, host::HostOp::Clock, "epoch");
    call.set_arg("");
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    std::int64_t milliseconds = 0;
    if (!nift::detail::nift_unix_epoch_milliseconds(milliseconds, error))
        return fail_host(call, host::HostStatus::Fatal, host_error("host.service_failed", error), error);
    if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    out = Value::integer(milliseconds);
    return Status::Ok;
}

Status host_sleep(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const Value* value = nullptr;
    if (!number_arg(args, argc, value)) return fail(error, "sleep: expected one millisecond duration");
    std::int64_t milliseconds = 0;
    if (!number_to_i64(*value, milliseconds) || milliseconds < 0)
        return fail(error, "sleep: milliseconds must be a non-negative signed 64-bit integer");
    using Rep = std::chrono::milliseconds::rep;
    if (static_cast<std::uint64_t>(milliseconds) > static_cast<std::uint64_t>(std::numeric_limits<Rep>::max()))
        return fail(error, "sleep: millisecond duration is unsupported on this platform");
    host::HostCall call(context.host, host::HostOp::Sleep, "sleep");
    call.set_arg("");
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<Rep>(milliseconds)));
    if (finish_host(call, std::string(), host::Observation{}, error) != Status::Ok) return Status::Error;
    out = Value::null();
    return Status::Ok;
}

Status host_secure_random_bytes(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                                std::string& error) {
    const Value* value = nullptr;
    if (!number_arg(args, argc, value)) return fail(error, "secure_random_bytes: expected one byte count");
    std::size_t count = 0;
    if (!number_to_size(*value, count))
        return fail(error, "secure_random_bytes: byte count must be a non-negative integer");
    if (count > 10000000) return fail(error, "secure_random_bytes: result exceeds 10000000 bytes");
    host::HostCall call(context.host, host::HostOp::Random, "secure_random_bytes");
    call.set_arg("");
    if (call.begin() != host::HostStatus::Ok) return fail_host(call, call.status(), call.error(), error);
    nift::RuntimeValue random_value;
    if (!nift::detail::nift_secure_random_bytes(count, random_value, error))
        return fail_host(call, host::HostStatus::Fatal, host_error("host.service_failed", error), error);
    const auto& bytes = *random_value.bytes;
    if (!reserve_result(context, bytes.size()))
        return fail_host(call, host::HostStatus::Limit,
                         host_error("limit.exceeded", "secure_random_bytes: result exceeds the memory budget"), error);
    if (finish_host(call, std::to_string(bytes.size()), host::Observation{}, error) != Status::Ok) return Status::Error;
    out = make_bytes(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    return Status::Ok;
}

} // namespace nift::rt::natives
