#pragma once
// Internal representation and helpers of the CP23 shared concurrency registry
// (rt/shared_state.h): entry storage, the one registry mutex/CV, wire-value
// encode/decode and cancellation helpers shared by the split implementations.
#include "rt/isolate_transfer.h"
#include "rt/shared_state.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace nift::rt::shared_detail {

enum class EntryKind : std::uint8_t { Mutex, AtomicInt, AtomicBool, Channel };

struct Entry {
    explicit Entry(EntryKind kind) : kind(kind) {}
    EntryKind kind;
    bool locked = false;
    std::uint64_t owner = 0;
    TransferGraph value;
    std::atomic<std::int64_t> integer{0};
    std::atomic<bool> boolean{false};
    std::size_t capacity = 0;
    std::deque<TransferGraph> buffer;
    bool closed = false;
};

struct Registry {
    std::mutex mutex;
    std::condition_variable cv;
    std::unordered_map<std::uint64_t, std::shared_ptr<Entry>> entries;
    std::uint64_t next_id = 1;
};

Registry& registry();

inline void fail(SharedOpError& error, const char* code, std::string message) {
    error.code = code;
    error.message = std::move(message);
}

std::shared_ptr<Entry> find_entry(const Value& handle, HandleKind kind);
std::shared_ptr<Entry> find_channel(const Value& handle);
Value add_entry(std::shared_ptr<Entry> entry, HandleKind kind);
bool cancelled(const std::shared_ptr<ParallelTaskRecord>& task);
bool encode_value(const Value& value, const TransferProtoMap* protos, TransferGraph& graph, SharedOpError& error);
bool decode_value(const TransferGraph& graph, const TransferProtoMap* protos, Value& value, SharedOpError& error);
bool int_operand(const Value& value, const char* what, std::int64_t& out, SharedOpError& error);

}  // namespace nift::rt::shared_detail
