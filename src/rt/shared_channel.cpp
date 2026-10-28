#include "rt/shared_channel.h"

#include "rt/shared_state_internal.h"

#include <utility>

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
    std::unique_lock<std::mutex> lock(r.mutex);
    const auto ready = [&] {
        return entry->closed || cancelled(task) ||
               entry->buffer.size() < (entry->capacity == 0 ? 1u : entry->capacity);
    };
    r.cv.wait(lock, ready);
    if (entry->closed) {
        fail(error, "runtime.channel_closed", "send: channel is closed");
        return false;
    }
    if (cancelled(task)) {
        take_task_cancelled(task);
        fail(error, "runtime.cancelled", "task: cancelled while waiting to send");
        return false;
    }
    entry->buffer.push_back(std::move(graph));
    r.cv.notify_all();
    if (entry->capacity != 0) return true;
    // A rendezvous send completes only when a receiver takes the value.
    r.cv.wait(lock, [&] { return entry->buffer.empty() || entry->closed || cancelled(task); });
    if (cancelled(task)) {
        take_task_cancelled(task);
        if (!entry->buffer.empty()) entry->buffer.pop_front();
        r.cv.notify_all();
        fail(error, "runtime.cancelled", "task: cancelled while waiting to send");
        return false;
    }
    return true;
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
        std::unique_lock<std::mutex> lock(r.mutex);
        r.cv.wait(lock, [&] { return !entry->buffer.empty() || entry->closed || cancelled(task); });
        if (cancelled(task)) {
            take_task_cancelled(task);
            fail(error, "runtime.cancelled", "task: cancelled while waiting to receive");
            return false;
        }
        if (entry->buffer.empty()) {
            closed = true;
            return true;
        }
        graph = std::move(entry->buffer.front());
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
        std::lock_guard<std::mutex> lock(r.mutex);
        entry->closed = true;
    }
    r.cv.notify_all();
    return true;
}

bool shared_channel_close_any(const Value& handle, SharedOpError& error) {
    return shared_channel_close(handle, error);
}

}  // namespace nift::rt
