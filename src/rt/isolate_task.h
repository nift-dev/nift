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
    // CP23 parallel tasks encode [globals..., args...] on the spawning thread.
    // When set, `args` and `globals` are ignored and the graph is decoded in
    // this thread's heap after the clone is prepared (so no Value crosses).
    bool has_seed_graph = false;
    bool has_function = false;
    TransferGraph seed_graph;
    // Captured on the spawning thread. Metadata contains no heap-backed Value.
    bool has_program_snapshot = false;
    TransferGraph constants_graph;
    std::vector<std::size_t> constant_counts;
    std::vector<std::uint32_t> proto_wires;
    std::vector<std::pair<std::uint16_t, std::string>> atom_names;
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
    // The original uncaught catchable Error, when it transferred successfully.
    TransferGraph error_value;
    bool has_error_value = false;
};

// The opcodes whose b operand is a proto index: an isolate clone shifts it by its wrapper slot (remap_code), and a
// nested snapshot shifts it back (snapshot_isolate_program with `first`).
inline bool proto_operand(OpCode op) {
    return op == OpCode::Call || op == OpCode::Spawn || op == OpCode::LoadFunction || op == OpCode::Closure;
}

// `first`: the number of leading protos left out. A nested isolate leaves out its `<isolate-task>` wrapper (slot 0),
// so the image it hands on is the original program, not a growing chain of wrappers (async-recursion fix (c)).
bool snapshot_isolate_program(const Program& source, const TransferProtoMap& map,
                              IsolateTaskInput& input, std::shared_ptr<const Program>& metadata,
                              std::string& error, std::size_t first = 0);

IsolateTaskOutcome execute_isolate_task(const Program& program, const IsolateTaskInput& input,
                                       VmOptions options = VmOptions());

} // namespace nift::rt
