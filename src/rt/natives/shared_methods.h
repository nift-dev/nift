#pragma once
// CP23 shared concurrency built-ins: mutex, atomic<int>/<bool>, channel,
// send/recv/close/cancel and their methods.
#include "rt/natives/builtin.h"

namespace nift::rt::natives {

Status shared_mutex(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status shared_atomic_int(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status shared_atomic_bool(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status shared_channel(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status shared_send(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status shared_recv(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status shared_cancel(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

Status mutex_lock(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status mutex_try_lock(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status mutex_unlock(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status mutex_locked(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status mutex_get(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status mutex_set(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

Status atomic_load(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status atomic_store(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status atomic_exchange(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status atomic_fetch_add(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status atomic_fetch_sub(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);
Status atomic_compare_exchange(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

}  // namespace nift::rt::natives
