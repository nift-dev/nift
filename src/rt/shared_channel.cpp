#include "rt/shared_channel.h"

#include "rt/shared_state_internal.h"

#include <utility>
#include <algorithm>

namespace nift::rt {

using namespace shared_detail;

bool shared_channel_send(const Value& handle, const Value& value, const TransferProtoMap* protos,
                         const std::shared_ptr<ParallelTaskRecord>& task, SharedOpError& error) {
    const auto entry = find_channel(handle);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "send: expected a channel");
        return false;
    }
    TransferGraph graph;
    if (!encode_value(value, protos, graph, error)) return false;
    Registry& r = registry();
    std::unique_lock<std::recursive_mutex> lock(r.mutex);
    const auto ready = [&] {
        return entry->closed || cancelled(task) ||
               entry->buffer.size() < (entry->capacity == 0 ? 1u : entry->capacity);
    };
    wait_shared(lock, r.cv, ready);
    if (entry->closed) {
        fail(error, "runtime.channel_closed", "send: channel is closed");
        return false;
    }
    if (cancelled(task)) {
        take_task_cancelled(task);
        fail(error, "runtime.cancelled", "task: cancelled while waiting to send");
        return false;
    }
    auto item = std::make_shared<ChannelItem>();
    item->value = std::move(graph);
    entry->buffer.push_back(item);
    r.cv.notify_all();
    if (entry->capacity != 0) return true;
    // A rendezvous send completes only when a receiver takes the value.
    wait_shared(lock, r.cv, [&] { return item->consumed || entry->closed || cancelled(task); });
    if (item->consumed) return true;
    if (cancelled(task)) {
        take_task_cancelled(task);
        const auto at = std::find(entry->buffer.begin(), entry->buffer.end(), item);
        if (at != entry->buffer.end()) entry->buffer.erase(at);
        r.cv.notify_all();
        fail(error, "runtime.cancelled", "task: cancelled while waiting to send");
        return false;
    }
    fail(error, "runtime.channel_closed", "send: channel is closed");
    return false;
}

bool shared_channel_recv(const Value& handle, const TransferProtoMap* protos,
                         const std::shared_ptr<ParallelTaskRecord>& task, Value& out, bool& closed,
                         SharedOpError& error) {
    closed = false;
    const auto entry = find_channel(handle);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "recv: expected a channel");
        return false;
    }
    Registry& r = registry();
    TransferGraph graph;
    {
        std::unique_lock<std::recursive_mutex> lock(r.mutex);
        wait_shared(lock, r.cv, [&] { return !entry->buffer.empty() || entry->closed || cancelled(task); });
        if (cancelled(task)) {
            take_task_cancelled(task);
            fail(error, "runtime.cancelled", "task: cancelled while waiting to receive");
            return false;
        }
        if (entry->buffer.empty()) {
            closed = true;
            return true;
        }
        entry->buffer.front()->consumed = true;
        graph = std::move(entry->buffer.front()->value);
        entry->buffer.pop_front();
    }
    r.cv.notify_all();
    return decode_value(graph, protos, out, error);
}

bool shared_channel_close(const Value& handle, SharedOpError& error) {
    const auto entry = find_channel(handle);
    if (!entry) {
        fail(error, "runtime.invalid_argument", "close: expected a channel");
        return false;
    }
    Registry& r = registry();
    {
        std::lock_guard<std::recursive_mutex> lock(r.mutex);
        entry->closed = true;
        if (entry->capacity == 0) entry->buffer.clear();
    }
    r.cv.notify_all();
    return true;
}

bool shared_channel_close_any(const Value& handle, SharedOpError& error) {
    return shared_channel_close(handle, error);
}

}  // namespace nift::rt
