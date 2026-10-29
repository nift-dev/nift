#pragma once
// CP23: process-wide shared concurrency registry. Mutexes, atomics and
// channels live here, outside every isolate heap, and are addressed by
// (HandleKind, id, generation) values. Values held by a mutex or carried by a
// channel are stored as TransferGraph wire data, so they cross isolates by
// copy/freeze rather than sharing VM heap objects. All waits use the
// registry's mutex/condition variable, which lets cancellation wake every
// blocked send/recv/lock in one notification.
#include "rt/value.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace nift::rt {

class TransferProtoMap;
struct ParallelTaskRecord;

struct SharedOpError {
    std::string code;
    std::string message;
    bool ok() const noexcept { return code.empty(); }
};

const char* shared_handle_type_name(const Value& value);
bool is_shared_handle(const Value& value);
std::shared_ptr<void> shared_handle_backing(const Value& value);
Value shared_handle_from_backing(HandleKind kind, std::uint64_t id, std::shared_ptr<void> backing);
bool shared_handle_kind(HandleKind kind);

Value shared_make_mutex(const Value* initial, const TransferProtoMap* protos, SharedOpError& error);
Value shared_make_atomic_int(std::int64_t initial, SharedOpError& error);
Value shared_make_atomic_bool(bool initial, SharedOpError& error);
Value shared_make_channel(std::int64_t capacity, SharedOpError& error);

bool shared_mutex_lock(const Value& handle, std::uint64_t owner,
                       const std::shared_ptr<ParallelTaskRecord>& task, SharedOpError& error);
bool shared_mutex_try_lock(const Value& handle, std::uint64_t owner, bool& locked, SharedOpError& error);
bool shared_mutex_unlock(const Value& handle, std::uint64_t owner, SharedOpError& error);
bool shared_mutex_locked(const Value& handle, bool& locked, SharedOpError& error);
bool shared_mutex_get(const Value& handle, std::uint64_t owner, const TransferProtoMap* protos, Value& out,
                      SharedOpError& error);
bool shared_mutex_set(const Value& handle, std::uint64_t owner, const Value& value, const TransferProtoMap* protos,
                      SharedOpError& error);

bool shared_channel_send(const Value& handle, const Value& value, const TransferProtoMap* protos,
                         const std::shared_ptr<ParallelTaskRecord>& task, SharedOpError& error);
bool shared_channel_recv(const Value& handle, const TransferProtoMap* protos,
                         const std::shared_ptr<ParallelTaskRecord>& task, Value& out, bool& closed,
                         SharedOpError& error);
bool shared_channel_close(const Value& handle, SharedOpError& error);
bool shared_channel_close_any(const Value& handle, SharedOpError& error);

bool shared_atomic_load(const Value& handle, Value& out, SharedOpError& error);
bool shared_atomic_store(const Value& handle, const Value& value, SharedOpError& error);
bool shared_atomic_exchange(const Value& handle, const Value& value, Value& old, SharedOpError& error);
bool shared_atomic_fetch(const Value& handle, const Value& operand, bool subtract, Value& out, SharedOpError& error);
bool shared_atomic_compare_exchange(const Value& handle, const Value& expected, const Value& desired, bool& out,
                                    SharedOpError& error);
// op: 0 +=, 1 -=, 2 %=, 3 &=, 4 |=, 5 ^= (legacy's atomic<int> compound set).
bool shared_atomic_compound(const Value& handle, int op, const Value& operand, bool return_old, Value& out,
                            SharedOpError& error);

bool shared_cancel(const Value& task_value, SharedOpError& error);
bool task_cancelled(const std::shared_ptr<ParallelTaskRecord>& task);
// Consumes an observed cancellation request (a caught cancellation lets the
// task run its cleanup and continue; a later cancel() may request it again).
bool take_task_cancelled(const std::shared_ptr<ParallelTaskRecord>& task);

void shared_registry_clear_for_test();
std::size_t shared_registry_live_for_test();

}  // namespace nift::rt
