#pragma once
// CP23: execute one compiled entry proto on a fresh isolate-owned Vm execution
// domain on the calling thread. Arguments, global slots and the return value
// cross through TransferGraph; constants are rebuilt in the isolate heap. This
// milestone does not start OS worker threads or change Coro::spawn.
#include "rt/isolate_transfer.h"
#include "rt/proto.h"
#include "rt/value.h"
#include "rt/vm.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nift::rt {

enum class IsolateTaskStatus : std::uint8_t {
    Ok,
    RefusedFuture,
    RefusedHandle,
    UnsupportedConstant,
    UnsupportedValue,
    InvalidInput,
    EntryOutOfRange,
    ExecutionError,
};

const char* isolate_task_status_name(IsolateTaskStatus status);

struct IsolateTaskInput {
    std::uint32_t entry_proto = 0;
    std::uint32_t argc = 0;
    std::vector<Value> args;
    // Indexed by global slot; slots beyond the vector are null.
    std::vector<Value> globals;
    // Nonzero marks a global slot ready (MarkGlobal in the wrapper).
    std::vector<std::uint8_t> global_ready;
    // Required: maps the source Program's immutable protos to wire indices.
    const TransferProtoMap* proto_map = nullptr;
};

struct IsolateTaskOutcome {
    IsolateTaskStatus status = IsolateTaskStatus::Ok;
    std::string error;
    VmResult result;
    std::string output;
    // Decodable with the caller's TransferProtoMap; empty unless the wrapper
    // returned a value that transferred successfully.
    TransferGraph return_value;
    bool has_return_value = false;
};

IsolateTaskOutcome execute_isolate_task(const Program& program, const IsolateTaskInput& input,
                                       VmOptions options = VmOptions());

} // namespace nift::rt
