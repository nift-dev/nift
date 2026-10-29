#include "rt/parallel_task_start.h"
#include "rt/parallel_task.h"
#include "rt/aggregate.h"
#include <algorithm>
#include <exception>
#include <new>

// Source-thread task snapshot and admission validation.
namespace nift::rt {

namespace {
bool finite_timer_body(const Proto& proto) {
    // Helping arbitrary work could await the sleeping ancestor's future and
    // deadlock its C++ stack. Admit only a finite, straight-line opcode subset
    // with no calls, jumps, handlers, shared waits, callbacks or native I/O.
    for (std::size_t i = 0; i < proto.code.size(); ++i) {
        const auto& in = proto.code[i];
        switch (in.op) {
            case OpCode::Nop: case OpCode::LoadK: case OpCode::LoadI:
            case OpCode::LoadNull: case OpCode::LoadBool: case OpCode::Move:
            case OpCode::Return: case OpCode::ReturnNull: break;
            case OpCode::Sleep:
                if (i == 0 || proto.code[i-1].op != OpCode::LoadI ||
                    proto.code[i-1].a != in.a || static_cast<std::int16_t>(proto.code[i-1].b) < 0) return false;
                break;
            default: return false;
        }
    }
    return true;
}
}

bool start_parallel_task(std::shared_ptr<ParallelTaskRecord> record, std::string& error) {
    auto runtime = record->runtime.lock();
    if (!runtime) {
        error = "parallel runtime is not available";
        return false;
    }
    if (!record->program || !record->proto_map || record->program->protos.empty() ||
        std::any_of(record->program->protos.begin(), record->program->protos.end(),
                    [](const auto& p) { return !p; })) {
        error = "missing task program/map"; return false;
    }
    if (record->input.has_program_snapshot &&
        std::any_of(record->program->protos.begin(), record->program->protos.end(),
                    [](const auto& p) { return !p->constants.empty(); })) {
        error = "snapshot metadata contains Values"; return false;
    }
    // Direct embedding callers have the same source-thread snapshot boundary.
    if (!record->input.has_program_snapshot) {
        if (!record->program || !record->proto_map) { error = "missing task program/map"; return false; }
        if (!record->input.has_seed_graph) {
            Value seeds = make_array();
            const auto g = record->program->main().nregs;
            for (std::size_t i = 0; i < g; ++i)
                array_push(seeds, i < record->input.globals.size() ? record->input.globals[i] : Value());
            for (const auto& v : record->input.args) array_push(seeds, v);
            if (record->input.seed_graph.encode(seeds, *record->proto_map, error) != TransferStatus::Ok) return false;
            record->input.has_seed_graph = true;
            record->input.argc = record->input.args.size();
            record->input.args.clear(); record->input.globals.clear();
        }
        std::shared_ptr<const Program> image;
        if (!snapshot_isolate_program(*record->program, *record->proto_map, record->input, image, error)) return false;
        record->program = std::move(image);
        record->options.program_keepalive = record->program;
    }
    // Ignored embedding vectors must not retain parent-isolate Values either.
    record->input.args.clear(); record->input.globals.clear();
    record->input.proto_map = record->proto_map.get();
    record->options.program_keepalive = record->program;
    if (!record->options.parallel_output) {
        record->options.parallel_output = std::make_shared<ParallelOutput>();
        record->output_root = true;
    }
    // Workers borrow the pool; the owning root keeps it alive and joins it.
    record->runtime = runtime;
    record->options.parallel = std::shared_ptr<ParallelRuntime>(runtime.get(), [](ParallelRuntime*) {});
    record->timer_help_safe = record->input.entry_proto < record->program->protos.size() &&
        finite_timer_body(*record->program->protos[record->input.entry_proto]);
    constexpr std::size_t max_helper_description = 256u << 10;
    std::size_t available = max_helper_description;
    const auto charge = [&](std::size_t bytes) {
        if (bytes > available) return false;
        available -= bytes; return true;
    };
    if (record->timer_help_safe) {
        record->timer_help_safe = record->input.argc == record->program->protos[record->input.entry_proto]->nparams &&
            record->program->main().nregs <= 1024 &&
            charge(record->input.seed_graph.storage_bytes()) && charge(record->input.constants_graph.storage_bytes());
        for (const auto& atom : record->input.atom_names)
            if (!charge(atom.second.size()) || !charge(256)) record->timer_help_safe = false;
    }
    try { return runtime->submit(record, error); }
    catch (const std::bad_alloc&) { throw; }
    catch (const std::exception& failure) {
        record->failure_reason = StopReason::Internal;
        error = std::string("cannot enqueue parallel task: ") + failure.what(); return false;
    }
}

} // namespace nift::rt
