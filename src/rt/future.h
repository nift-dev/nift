#pragma once
// CP22 futures: the heap object behind a Kind::Future value (what calling an
// async function returns). Owned by reference counting like every heap value
// of the isolate; the scheduler keeps one reference while the task runs.
//
// A future is completed exactly once: Done with `result`, or Failed with the
// task's stop record. A catchable failure keeps the task's original Error
// value in `error` (CP13B A2: `await` re-raises it unchanged); a fatal one
// keeps the message and the task's source position for the report. Waiting
// tasks are listed by id until completion moves them to the run queue.
// `task_bytes` is the storage of the suspended task this future belongs to
// (its stack, frames and handlers), charged to the isolate's live heap through
// heap_retally while the task is suspended (memory budget, CP22 §4.2).
#include "rt/value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nift::rt {

enum class FutureState : std::uint8_t { Pending, Done, Failed };

struct FutureFailure {
    std::string message;          // the task's stop message (with VM prefixes)
    std::string code;             // diagnostic code, when the stop has one
    bool catchable = false;       // `error` holds the original Error
    std::uint32_t offset = 0;     // the task's report offset (linked-unit source offset)
    bool error_origin = false;    // a catchable Error carries its own origin
    bool in_loop_path = false;    // evidence (VM statistics)
};

struct FutureObject : HeapObject {
    FutureState state = FutureState::Pending;
    Value result;   // Done: the task's return value
    Value error;    // Failed && catchable: the original Error value
    FutureFailure failure;
    std::vector<std::uint32_t> waiters;  // ids of tasks blocked in `await` on this future
    std::size_t task_bytes = 0;          // suspended task storage charged to the heap
    FutureObject() : HeapObject(Kind::Future) {}
};

inline FutureObject* future_of(const Value& value) noexcept {
    return value.is(Kind::Future) ? value.heap_as<FutureObject>() : nullptr;
}

}  // namespace nift::rt
