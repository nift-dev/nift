#include "rt/isolate_task.h"

#include "rt/aggregate.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace nift::rt {

namespace {

struct ProgramIndex {
    std::unordered_map<const Proto*, std::uint32_t> by_pointer;
    std::size_t count = 0;
    explicit ProgramIndex(const Program& program) : count(program.protos.size()) {
        for (std::size_t i = 0; i < program.protos.size(); ++i)
            by_pointer.emplace(program.protos[i].get(), static_cast<std::uint32_t>(i));
    }
};

// Wire indices follow the caller's map for source protos. Decoding turns them
// into the clone at index + 1 (the wrapper occupies index 0); encoding a
// result goes the other way, so the caller can decode with its own map.
class CloneTransferMap : public TransferProtoMap {
public:
    CloneTransferMap(const TransferProtoMap& source, const Program& program,
                     const std::vector<std::unique_ptr<Proto>>& clones) {
        for (std::size_t i = 0; i < program.protos.size(); ++i) {
            std::uint32_t wire = 0;
            const Proto* original = program.protos[i].get();
            if (original == nullptr || !source.index_of(original, wire)) continue;
            const Proto* clone = clones[i + 1].get();
            clone_to_wire_.emplace(clone, wire);
            wire_to_clone_[wire] = clone;
        }
    }
    bool index_of(const Proto* proto, std::uint32_t& index) const override {
        const auto found = clone_to_wire_.find(proto);
        if (found == clone_to_wire_.end()) return false;
        index = found->second;
        return true;
    }
    const Proto* proto_of(std::uint32_t index) const override {
        const auto found = wire_to_clone_.find(index);
        return found == wire_to_clone_.end() ? nullptr : found->second;
    }

private:
    std::unordered_map<const Proto*, std::uint32_t> clone_to_wire_;
    std::unordered_map<std::uint32_t, const Proto*> wire_to_clone_;
};

IsolateTaskOutcome failure(IsolateTaskStatus status, std::string error) {
    IsolateTaskOutcome outcome;
    outcome.status = status;
    outcome.error = std::move(error);
    return outcome;
}

IsolateTaskStatus transfer_status(TransferStatus status, bool constant) {
    switch (status) {
        case TransferStatus::Ok: return IsolateTaskStatus::Ok;
        case TransferStatus::RefusedFuture: return IsolateTaskStatus::RefusedFuture;
        case TransferStatus::RefusedHandle: return IsolateTaskStatus::RefusedHandle;
        case TransferStatus::ProtoUnavailable:
        case TransferStatus::InvalidGraph:
            return constant ? IsolateTaskStatus::UnsupportedConstant : IsolateTaskStatus::UnsupportedValue;
    }
    return IsolateTaskStatus::UnsupportedValue;
}

bool remap_code(Proto& clone, const std::size_t proto_count, std::string& error) {
    for (Instr& in : clone.code) {
        switch (in.op) {
            case OpCode::Call:
            case OpCode::Spawn:
            case OpCode::LoadFunction:
            case OpCode::Closure:
                if (in.b >= proto_count) {
                    error = "proto operand is out of range";
                    return false;
                }
                in.b = static_cast<std::uint16_t>(in.b + 1u);
                break;
            default: break;
        }
    }
    return true;
}

bool clone_proto(const Proto& source, Proto& clone, const ProgramIndex& source_index,
                 const std::vector<std::unique_ptr<Proto>>& clones, std::string& error) {
    clone.collection_expression_view = source.collection_expression_view;
    clone.name = source.name;
    clone.is_async = source.is_async;
    clone.nparams = source.nparams;
    clone.nregs = source.nregs;
    clone.code = source.code;
    clone.offsets = source.offsets;
    clone.notes = source.notes;
    clone.note_table = source.note_table;
    if (!remap_code(clone, source_index.count, error)) return false;
    if (source.collection_entry == nullptr) return true;
    const auto found = source_index.by_pointer.find(source.collection_entry);
    if (found == source_index.by_pointer.end()) {
        error = "collection entry proto is not in the program";
        return false;
    }
    clone.collection_entry = clones[found->second + 1].get();
    return true;
}

TransferStatus transfer_values(const std::vector<Value>& values, const TransferProtoMap& source,
                               const TransferProtoMap& destination, std::vector<Value>& decoded,
                               std::string& error) {
    decoded.clear();
    if (values.empty()) return TransferStatus::Ok;
    Value root = make_array(values.size());
    for (const Value& value : values) array_push(root, value);
    TransferGraph graph;
    TransferStatus status = graph.encode(root, source, error);
    if (status != TransferStatus::Ok) return status;
    Value transferred;
    status = graph.decode(destination, transferred, error);
    if (status != TransferStatus::Ok) return status;
    const std::vector<Value>& items = array_items(transferred);
    decoded.assign(items.begin(), items.end());
    return TransferStatus::Ok;
}

bool add_constant(Proto& proto, const Value& value, std::uint16_t& index, std::string& error) {
    if (proto.constants.size() >= kMaxConstants) {
        error = "too many wrapper constants";
        return false;
    }
    index = static_cast<std::uint16_t>(proto.constants.size());
    proto.constants.push_back(value);
    return true;
}

void emit(Proto& proto, OpCode op, std::uint16_t a, std::uint16_t b, std::uint16_t c) {
    proto.code.push_back(Instr{op, 0, a, b, c});
    proto.offsets.push_back(0);
    proto.notes.push_back(0);
}

bool build_wrapper(Proto& wrapper, const std::vector<Value>& globals,
                   const std::vector<std::uint8_t>& ready, const std::vector<Value>& args,
                   std::uint32_t entry, std::string& error) {
    const std::uint32_t global_count = static_cast<std::uint32_t>(globals.size());
    wrapper.name = "<isolate-task>";
    wrapper.nregs = static_cast<std::uint16_t>(global_count + std::max<std::uint32_t>(args.size(), 1));
    for (std::uint32_t g = 0; g < global_count; ++g) {
        std::uint16_t index = 0;
        if (!add_constant(wrapper, globals[g], index, error)) return false;
        emit(wrapper, OpCode::LoadK, static_cast<std::uint16_t>(g), index, 0);
    }
    const std::size_t ready_count = std::min<std::size_t>(ready.size(), global_count);
    for (std::size_t g = 0; g < ready_count; ++g)
        if (ready[g] != 0)
            emit(wrapper, OpCode::MarkGlobal, static_cast<std::uint16_t>(g), 0, 0);
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::uint16_t index = 0;
        if (!add_constant(wrapper, args[i], index, error)) return false;
        emit(wrapper, OpCode::LoadK, static_cast<std::uint16_t>(global_count + i), index, 0);
    }
    emit(wrapper, OpCode::Call, static_cast<std::uint16_t>(global_count),
         static_cast<std::uint16_t>(entry + 1u), static_cast<std::uint16_t>(args.size()));
    emit(wrapper, OpCode::TopReturn, static_cast<std::uint16_t>(global_count), 0, 0);
    return true;
}

bool prepare_clones(const Program& source, Program& clone, std::string& error) {
    clone.has_collection_boundaries = source.has_collection_boundaries;
    clone.has_coroutines = source.has_coroutines;
    clone.protos.resize(source.protos.size() + 1);
    for (auto& proto : clone.protos) proto = std::make_unique<Proto>();
    const ProgramIndex source_index(source);
    for (std::size_t i = 0; i < source.protos.size(); ++i)
        if (!clone_proto(*source.protos[i], *clone.protos[i + 1], source_index, clone.protos, error))
            return false;
    return true;
}

bool constants_from(const Program& source, const TransferProtoMap& parent, const CloneTransferMap& map,
                    std::vector<Value>& decoded, std::string& error, IsolateTaskStatus& status) {
    std::vector<Value> constants;
    for (const auto& proto : source.protos)
        for (const Value& value : proto->constants) constants.push_back(value);
    const TransferStatus transferred = transfer_values(constants, parent, map, decoded, error);
    if (transferred == TransferStatus::Ok) return true;
    status = transfer_status(transferred, true);
    return false;
}

bool seed_values(const Program& source, const IsolateTaskInput& input, const CloneTransferMap& map,
                 std::vector<Value>& globals, std::vector<Value>& args,
                 std::string& error, IsolateTaskStatus& status) {
    const std::size_t global_count = source.main().nregs;
    std::vector<Value> seeds;
    seeds.reserve(global_count + input.args.size());
    for (std::size_t g = 0; g < global_count; ++g)
        seeds.push_back(g < input.globals.size() ? input.globals[g] : Value());
    seeds.insert(seeds.end(), input.args.begin(), input.args.end());
    std::vector<Value> transferred;
    const TransferStatus result = transfer_values(seeds, *input.proto_map, map, transferred, error);
    if (result != TransferStatus::Ok) {
        status = transfer_status(result, false);
        return false;
    }
    globals.assign(transferred.begin(), transferred.begin() + static_cast<std::ptrdiff_t>(global_count));
    args.assign(transferred.begin() + static_cast<std::ptrdiff_t>(global_count), transferred.end());
    return true;
}

IsolateTaskStatus valid_input(const Program& source, const IsolateTaskInput& input, std::string& error) {
    if (input.proto_map == nullptr) {
        error = "a TransferProtoMap is required";
        return IsolateTaskStatus::InvalidInput;
    }
    if (source.protos.empty() || source.protos.front() == nullptr) {
        error = "the program has no main proto";
        return IsolateTaskStatus::InvalidInput;
    }
    for (const auto& proto : source.protos) {
        if (proto == nullptr) {
            error = "the program has a null proto";
            return IsolateTaskStatus::InvalidInput;
        }
    }
    if (input.entry_proto >= source.protos.size()) {
        error = "entry proto is out of range";
        return IsolateTaskStatus::EntryOutOfRange;
    }
    if (input.argc != input.args.size()) {
        error = "argc does not match the argument count";
        return IsolateTaskStatus::InvalidInput;
    }
    const std::uint32_t globals = source.main().nregs;
    if (input.globals.size() > globals) {
        error = "more global values than the program's global slots";
        return IsolateTaskStatus::InvalidInput;
    }
    if (globals + std::max<std::uint32_t>(input.argc, 1) > kMaxRegisters) {
        error = "wrapper register budget exceeded";
        return IsolateTaskStatus::InvalidInput;
    }
    if (globals + input.argc > kMaxConstants) {
        error = "wrapper constant budget exceeded";
        return IsolateTaskStatus::InvalidInput;
    }
    return IsolateTaskStatus::Ok;
}

} // namespace

const char* isolate_task_status_name(IsolateTaskStatus status) {
    switch (status) {
        case IsolateTaskStatus::Ok: return "ok";
        case IsolateTaskStatus::RefusedFuture: return "refused-future";
        case IsolateTaskStatus::RefusedHandle: return "refused-handle";
        case IsolateTaskStatus::UnsupportedConstant: return "unsupported-constant";
        case IsolateTaskStatus::UnsupportedValue: return "unsupported-value";
        case IsolateTaskStatus::InvalidInput: return "invalid-input";
        case IsolateTaskStatus::EntryOutOfRange: return "entry-out-of-range";
        case IsolateTaskStatus::ExecutionError: return "execution-error";
    }
    return "unknown";
}

IsolateTaskOutcome execute_isolate_task(const Program& source, const IsolateTaskInput& input,
                                       VmOptions options) {
    std::string error;
    const IsolateTaskStatus valid = valid_input(source, input, error);
    if (valid != IsolateTaskStatus::Ok) return failure(valid, std::move(error));
    Program clone;
    if (!prepare_clones(source, clone, error)) return failure(IsolateTaskStatus::InvalidInput, std::move(error));
    const CloneTransferMap map(*input.proto_map, source, clone.protos);
    IsolateTaskStatus status = IsolateTaskStatus::Ok;
    std::vector<Value> globals;
    std::vector<Value> args;
    if (!seed_values(source, input, map, globals, args, error, status)) return failure(status, std::move(error));
    std::vector<Value> constants;
    if (!constants_from(source, *input.proto_map, map, constants, error, status))
        return failure(status, std::move(error));
    std::size_t next = 0;
    for (std::size_t i = 0; i < source.protos.size(); ++i)
        for (std::size_t k = 0; k < source.protos[i]->constants.size(); ++k)
            clone.protos[i + 1]->constants.push_back(constants[next++]);
    if (!build_wrapper(*clone.protos[0], globals, input.global_ready, args, input.entry_proto, error))
        return failure(IsolateTaskStatus::InvalidInput, std::move(error));

    options.return_value_only = true;
    Vm vm(clone, options);
    IsolateTaskOutcome outcome;
    outcome.result = vm.run(outcome.output);
    if (outcome.result.reason != StopReason::None) {
        outcome.status = IsolateTaskStatus::ExecutionError;
        outcome.error = outcome.result.message;
        return outcome;
    }
    const Value* returned = vm.return_value();
    if (returned == nullptr) return outcome;
    const TransferStatus encoded = outcome.return_value.encode(*returned, map, outcome.error);
    if (encoded != TransferStatus::Ok) {
        outcome.return_value.clear();
        outcome.status = transfer_status(encoded, false);
        return outcome;
    }
    outcome.has_return_value = true;
    return outcome;
}

} // namespace nift::rt
