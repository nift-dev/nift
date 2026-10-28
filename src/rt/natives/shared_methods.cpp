#include "rt/natives/shared_methods.h"

#include "rt/error.h"
#include "rt/natives/support.h"
#include "rt/number.h"
#include "rt/shared_state.h"

#include <cstdint>

namespace nift::rt::natives {

namespace {

Status recoverable(const Context& context, const SharedOpError& failure, std::string& error) {
    error = failure.message;
    if (context.host.error_code) *context.host.error_code = failure.code;
    return Status::Recoverable;
}

Status complete(const Context& context, const SharedOpError& failure, Value& out, std::string& error) {
    if (!failure.ok()) return recoverable(context, failure, error);
    out = Value();
    return Status::Ok;
}

Status invalid(const Context& context, std::string message, std::string& error) {
    error = std::move(message);
    if (context.host.error_code) *context.host.error_code = "runtime.invalid_argument";
    return Status::Recoverable;
}

bool no_args(std::uint32_t argc, const char* name, std::string& error) {
    if (argc == 0 || argc == 1) return true;  // args[0] is the receiver for a method
    error = std::string(name) + ": expected no arguments";
    return false;
}

}  // namespace

Status shared_mutex(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc > 1) return invalid(context, "mutex: expected zero or one initial value", error);
    SharedOpError failure;
    out = shared_make_mutex(argc == 1 ? &args[0] : nullptr, context.protos, failure);
    return failure.ok() ? Status::Ok : recoverable(context, failure, error);
}

Status shared_atomic_int(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                         std::string& error) {
    (void)context;
    if (argc != 1) return invalid(context, "atomic<int>: expected one initial value", error);
    std::int64_t value = 0;
    if (!number_to_i64(args[0], value))
        return invalid(context, "atomic<int>: initial value must be a signed 64-bit integer", error);
    SharedOpError failure;
    out = shared_make_atomic_int(value, failure);
    return failure.ok() ? Status::Ok : recoverable(context, failure, error);
}

Status shared_atomic_bool(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                          std::string& error) {
    (void)context;
    if (argc != 1) return invalid(context, "atomic<bool>: expected one initial value", error);
    if (!args[0].is(Kind::Bool)) return invalid(context, "atomic<bool>: initial value must be bool", error);
    SharedOpError failure;
    out = shared_make_atomic_bool(args[0].as_bool(), failure);
    return failure.ok() ? Status::Ok : recoverable(context, failure, error);
}

Status shared_channel(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                      std::string& error) {
    (void)context;
    if (argc > 1) return invalid(context, "channel: expected zero or one capacity", error);
    std::int64_t capacity = 0;
    if (argc == 1 && !number_to_i64(args[0], capacity))
        return invalid(context, "channel: capacity must be a non-negative integer", error);
    SharedOpError failure;
    out = shared_make_channel(capacity, failure);
    return failure.ok() ? Status::Ok : recoverable(context, failure, error);
}

Status shared_send(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 2) return invalid(context, "send: expected a channel and a value", error);
    SharedOpError failure;
    shared_channel_send(args[0], args[1], context.protos, context.task, failure);
    return complete(context, failure, out, error);
}

Status shared_recv(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 1) return invalid(context, "recv: expected a channel", error);
    SharedOpError failure;
    Value value;
    bool closed = false;
    if (!shared_channel_recv(args[0], context.protos, context.task, value, closed, failure))
        return recoverable(context, failure, error);
    if (closed) {
        std::string why;
        if (!make_system_error("runtime.channel_closed", "recv: channel is closed and drained", SourceOrigin{}, out,
                               why)) {
            error = "channel sentinel: " + why;
            return Status::Internal;
        }
        return Status::Ok;
    }
    out = std::move(value);
    return Status::Ok;
}

Status shared_cancel(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    (void)context;
    if (argc != 1) return invalid(context, "cancel: expected one task", error);
    SharedOpError failure;
    shared_cancel(args[0], failure);
    return complete(context, failure, out, error);
}

Status mutex_lock(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!no_args(argc, "lock", error)) return Status::Error;
    SharedOpError failure;
    shared_mutex_lock(args[0], context.owner_token, context.task, failure);
    return complete(context, failure, out, error);
}

Status mutex_try_lock(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                      std::string& error) {
    if (!no_args(argc, "try_lock", error)) return Status::Error;
    SharedOpError failure;
    bool locked = false;
    if (!shared_mutex_try_lock(args[0], context.owner_token, locked, failure))
        return recoverable(context, failure, error);
    out = Value::boolean(locked);
    return Status::Ok;
}

Status mutex_unlock(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!no_args(argc, "unlock", error)) return Status::Error;
    SharedOpError failure;
    shared_mutex_unlock(args[0], context.owner_token, failure);
    return complete(context, failure, out, error);
}

Status mutex_locked(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!no_args(argc, "locked", error)) return Status::Error;
    SharedOpError failure;
    bool locked = false;
    if (!shared_mutex_locked(args[0], locked, failure)) return recoverable(context, failure, error);
    out = Value::boolean(locked);
    return Status::Ok;
}

Status mutex_get(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!no_args(argc, "get", error)) return Status::Error;
    SharedOpError failure;
    if (!shared_mutex_get(args[0], context.owner_token, context.protos, out, failure))
        return recoverable(context, failure, error);
    return Status::Ok;
}

Status mutex_set(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 2) return invalid(context, "set: expected one value", error);
    SharedOpError failure;
    shared_mutex_set(args[0], context.owner_token, args[1], context.protos, failure);
    return complete(context, failure, out, error);
}

Status atomic_load(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (!no_args(argc, "load", error)) return Status::Error;
    SharedOpError failure;
    if (!shared_atomic_load(args[0], out, failure)) return recoverable(context, failure, error);
    return Status::Ok;
}

Status atomic_store(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 2) return invalid(context, "store: expected one value", error);
    SharedOpError failure;
    shared_atomic_store(args[0], args[1], failure);
    return complete(context, failure, out, error);
}

Status atomic_exchange(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 2) return invalid(context, "exchange: expected one value", error);
    SharedOpError failure;
    if (!shared_atomic_exchange(args[0], args[1], out, failure)) return recoverable(context, failure, error);
    return Status::Ok;
}

Status atomic_fetch_add(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 2) return invalid(context, "fetch_add: expected one integer", error);
    SharedOpError failure;
    if (!shared_atomic_fetch(args[0], args[1], false, out, failure)) return recoverable(context, failure, error);
    return Status::Ok;
}

Status atomic_fetch_sub(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc != 2) return invalid(context, "fetch_sub: expected one integer", error);
    SharedOpError failure;
    if (!shared_atomic_fetch(args[0], args[1], true, out, failure)) return recoverable(context, failure, error);
    return Status::Ok;
}

Status atomic_compare_exchange(const Context& context, const Value* args, std::uint32_t argc, Value& out,
                               std::string& error) {
    if (argc != 3) return invalid(context, "compare_exchange: expected expected and desired values", error);
    SharedOpError failure;
    bool result = false;
    if (!shared_atomic_compare_exchange(args[0], args[1], args[2], result, failure))
        return recoverable(context, failure, error);
    out = Value::boolean(result);
    return Status::Ok;
}

}  // namespace nift::rt::natives
