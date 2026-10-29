#include "rt/shared_state.h"

#include "rt/shared_state_internal.h"

#include "rt/error.h"
#include "rt/heap_account.h"
#include "rt/future.h"
#include "rt/number.h"
#include "rt/parallel_task.h"

#include <utility>

namespace nift::rt {

using namespace shared_detail;

namespace shared_detail {

Registry& registry() {
    static Registry instance;
    return instance;
}

Entry::~Entry() {
    if (!id) return;
    auto& r = registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    r.entries.erase(id);
}

bool entry_kind_matches(EntryKind entry, HandleKind handle) {
    switch (handle) {
        case HandleKind::Mutex: return entry == EntryKind::Mutex;
        case HandleKind::Atomic: return entry == EntryKind::AtomicInt || entry == EntryKind::AtomicBool;
        case HandleKind::Channel: return entry == EntryKind::Channel;
        default: return false;
    }
}

std::shared_ptr<Entry> find_entry(const Value& handle, HandleKind kind) {
    if (!handle.is(Kind::Handle) || handle.handle_kind() != kind) return nullptr;
    Registry& r = registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    const auto found = r.entries.find(handle.handle_id());
    if (found == r.entries.end()) return nullptr;
    auto entry = found->second.lock();
    return entry && entry_kind_matches(entry->kind, kind) ? entry : nullptr;
}

std::shared_ptr<Entry> find_channel(const Value& handle) {
    if (!handle.is(Kind::Handle) || handle.handle_kind() != HandleKind::Channel) return nullptr;
    Registry& r = registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    const auto found = r.entries.find(handle.handle_id());
    if (found == r.entries.end()) return nullptr;
    auto entry = found->second.lock();
    return entry && entry->kind == EntryKind::Channel ? entry : nullptr;
}

Value add_entry(std::shared_ptr<Entry> entry, HandleKind kind) {
    Registry& r = registry();
    std::uint64_t id = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(r.mutex);
        id = r.next_id++;
        entry->id = id;
        r.entries.emplace(id, entry);
    }
    return shared_handle_from_backing(kind, id, std::move(entry));
}

bool cancelled(const std::shared_ptr<ParallelTaskRecord>& task) {
    return task_cancelled(task);
}

bool encode_value(const Value& value, const TransferProtoMap* protos, TransferGraph& graph, SharedOpError& error) {
    if (protos == nullptr) {
        fail(error, "runtime.invalid_argument", "shared value transfer needs a transfer map");
        return false;
    }
    std::string why;
    const TransferStatus status = graph.encode(value, *protos, why);
    if (status != TransferStatus::Ok) {
        fail(error, "runtime.invalid_argument",
             why.empty() ? std::string("shared value cannot be transferred: ") + transfer_status_name(status) : why);
        return false;
    }
    return true;
}

bool decode_value(const TransferGraph& graph, const TransferProtoMap* protos, Value& value, SharedOpError& error) {
    if (protos == nullptr) {
        fail(error, "runtime.invalid_argument", "shared value transfer needs a transfer map");
        return false;
    }
    std::string why;
    const TransferStatus status = graph.decode(*protos, value, why);
    if (status != TransferStatus::Ok) {
        fail(error, "runtime.invalid_argument",
             why.empty() ? std::string("shared value cannot be transferred: ") + transfer_status_name(status) : why);
        return false;
    }
    return true;
}


bool int_operand(const Value& value, const char* what, std::int64_t& out, SharedOpError& error) {
    if (!number_to_i64(value, out)) {
        fail(error, "runtime.invalid_argument", std::string(what) + " requires a signed 64-bit integer");
        return false;
    }
    return true;
}

}  // namespace shared_detail

namespace {
void release_shared_box(void* pointer, HandleKind, std::uint64_t, std::uint32_t) {
    auto& r = shared_detail::registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    delete static_cast<std::shared_ptr<void>*>(pointer);
    shared_collect();
}
}
Value shared_handle_from_backing(HandleKind kind, std::uint64_t id, std::shared_ptr<void> backing) {
    if (!backing) return Value();
    auto keep = std::make_unique<std::shared_ptr<void>>(std::move(backing));
    auto box = std::make_unique<HandleBoxObject>();
    box->id = id; box->handle_kind = kind; box->generation = 0;
    box->release = release_shared_box; box->table = keep.release();
    heap_retally(box.get());
    return Value::adopt_handle_box(box.release());
}
std::shared_ptr<void> shared_handle_backing(const Value& handle) {
    if (!is_shared_handle(handle)) return {};
    return shared_detail::find_entry(handle, handle.handle_kind());
}

bool shared_handle_kind(HandleKind kind) {
    return kind == HandleKind::Mutex || kind == HandleKind::Atomic || kind == HandleKind::Channel;
}

bool is_shared_handle(const Value& value) {
    return value.is(Kind::Handle) && shared_handle_kind(value.handle_kind());
}

const char* shared_handle_type_name(const Value& value) {
    if (!value.is(Kind::Handle)) return nullptr;
    switch (value.handle_kind()) {
        case HandleKind::Mutex: return "mutex";
        case HandleKind::Atomic: {
            const auto entry = find_entry(value, HandleKind::Atomic);
            if (!entry) return "atomic";
            return entry->kind == EntryKind::AtomicBool ? "atomic<bool>" : "atomic<int>";
        }
        case HandleKind::Channel: return "channel";
        default: return nullptr;
    }
}

Value shared_make_mutex(const Value* initial, const TransferProtoMap* protos, SharedOpError& error) {
    auto entry = std::make_shared<Entry>(EntryKind::Mutex);
    if (initial != nullptr && !encode_value(*initial, protos, entry->value, error)) return Value();
    return add_entry(std::move(entry), HandleKind::Mutex);
}

Value shared_make_atomic_int(std::int64_t initial, SharedOpError& error) {
    (void)error;
    auto entry = std::make_shared<Entry>(EntryKind::AtomicInt);
    entry->integer.store(initial);
    return add_entry(std::move(entry), HandleKind::Atomic);
}

Value shared_make_atomic_bool(bool initial, SharedOpError& error) {
    (void)error;
    auto entry = std::make_shared<Entry>(EntryKind::AtomicBool);
    entry->boolean.store(initial);
    return add_entry(std::move(entry), HandleKind::Atomic);
}

Value shared_make_channel(std::int64_t capacity, SharedOpError& error) {
    if (capacity < 0) {
        fail(error, "runtime.invalid_argument", "channel: capacity must be a non-negative integer");
        return Value();
    }
    if (capacity > static_cast<std::int64_t>((std::size_t(1) << 20))) {
        fail(error, "runtime.invalid_argument", "channel: capacity is too large");
        return Value();
    }
    auto entry = std::make_shared<Entry>(EntryKind::Channel);
    entry->capacity = static_cast<std::size_t>(capacity);
    return add_entry(std::move(entry), HandleKind::Channel);
}

bool shared_mutex_lock(const Value& handle, std::uint64_t owner,
                       const std::shared_ptr<ParallelTaskRecord>& task, SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Mutex);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "mutex: invalid handle");
        return false;
    }
    Registry& r = registry();
    std::unique_lock<std::recursive_mutex> lock(r.mutex);
    if (entry->locked && entry->owner == owner && !cancelled(task)) {
        fail(error, "runtime.invalid_argument", "mutex: recursive lock is not supported");
        return false;
    }
    if (!wait_shared(lock, r.cv, [&] { return !entry->locked || cancelled(task); })) {
        fail(error, "limit.exceeded", "parallel blocking wait limit exceeded"); return false;
    }
    if (cancelled(task)) {
        take_task_cancelled(task);
        fail(error, "runtime.cancelled", "task: cancelled while waiting for a mutex");
        return false;
    }
    entry->locked = true;
    entry->owner = owner;
    return true;
}

bool shared_mutex_try_lock(const Value& handle, std::uint64_t owner, bool& locked, SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Mutex);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "mutex: invalid handle");
        return false;
    }
    Registry& r = registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    if (entry->locked) {
        locked = false;
        return true;
    }
    entry->locked = true;
    entry->owner = owner;
    locked = true;
    return true;
}

bool shared_mutex_unlock(const Value& handle, std::uint64_t owner, SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Mutex);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "mutex: invalid handle");
        return false;
    }
    Registry& r = registry();
    {
        std::lock_guard<std::recursive_mutex> lock(r.mutex);
        if (!entry->locked) {
            fail(error, "runtime.invalid_argument", "mutex: unlock of unlocked mutex");
            return false;
        }
        if (entry->owner != owner) {
            fail(error, "runtime.invalid_argument", "mutex: unlock by non-owner");
            return false;
        }
        entry->locked = false;
        entry->owner = 0;
    }
    r.cv.notify_all();
    return true;
}

bool shared_mutex_locked(const Value& handle, bool& locked, SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Mutex);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "mutex: invalid handle");
        return false;
    }
    Registry& r = registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    locked = entry->locked;
    return true;
}

bool shared_mutex_get(const Value& handle, std::uint64_t owner, const TransferProtoMap* protos, Value& out,
                      SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Mutex);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "mutex: invalid handle");
        return false;
    }
    TransferGraph graph;
    {
        Registry& r = registry();
        std::lock_guard<std::recursive_mutex> lock(r.mutex);
        if (!entry->locked || entry->owner != owner) {
            fail(error, "runtime.invalid_argument", "mutex: get requires the current thread to hold the lock");
            return false;
        }
        graph = entry->value;
    }
    return decode_value(graph, protos, out, error);
}

bool shared_mutex_set(const Value& handle, std::uint64_t owner, const Value& value, const TransferProtoMap* protos,
                      SharedOpError& error) {
    const auto entry = find_entry(handle, HandleKind::Mutex);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "mutex: invalid handle");
        return false;
    }
    TransferGraph graph;
    if (!encode_value(value, protos, graph, error)) return false;
    Registry& r = registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    if (!entry->locked || entry->owner != owner) {
        fail(error, "runtime.invalid_argument", "mutex: set requires the current thread to hold the lock");
        return false;
    }
    entry->value = std::move(graph);
    return true;
}


bool task_cancelled(const std::shared_ptr<ParallelTaskRecord>& task) {
    if (!task) return false;
    std::lock_guard<std::mutex> lock(task->mutex);
    return task->cancelled;
}

bool take_task_cancelled(const std::shared_ptr<ParallelTaskRecord>& task) {
    if (!task) return false;
    std::lock_guard<std::mutex> lock(task->mutex);
    const bool cancelled = task->cancelled;
    task->cancelled = false;
    return cancelled;
}


void shared_registry_clear_for_test() {
    Registry& r = registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    r.entries.clear();
    // Never reuse ids while old handles may still exist.
    r.cv.notify_all();
}

std::size_t shared_registry_live_for_test() {
    Registry& r = registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    return r.entries.size();
}


}  // namespace nift::rt
