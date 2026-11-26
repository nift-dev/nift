#include "rt/parallel_task_start.h"
#include "rt/parallel_task.h"
#include "rt/aggregate.h"
#include <algorithm>
#include <exception>
#include <new>
#include <string_view>
#include <system_error>

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
std::size_t image_bytes(const Program& program) {
    std::size_t bytes = 0;
    for (const auto& p : program.protos)
        bytes += sizeof(Proto) + p->code.size() * sizeof(Instr) + p->offsets.size() * sizeof(p->offsets[0]);
    return bytes;
}
}

ProgramTransferMap::ProgramTransferMap(const Program& program) {
    static std::atomic<std::uint64_t> next_domain{1};
    domain_ = next_domain.fetch_add(1);
    by_index_.reserve(program.protos.size());
    for (std::size_t i = 0; i < program.protos.size(); ++i) {
        const Proto* proto = program.protos[i].get();
        by_pointer_.emplace(proto, static_cast<std::uint32_t>(i));
        by_index_.push_back(proto);
    }
}
bool ProgramTransferMap::index_of(const Proto* proto, std::uint32_t& index) const {
    const auto found = by_pointer_.find(proto);
    if (found == by_pointer_.end()) return false;
    index = found->second;
    return true;
}
const Proto* ProgramTransferMap::proto_of(std::uint32_t index) const {
    return index < by_index_.size() ? by_index_[index] : nullptr;
}

void note_parallel_limit(const ParallelTaskRecord& record, const VmResult& result, bool worker_allocation_failed) {
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
    // C141a (T1, M14), test-only: run_parallel_task calls this just before it publishes the task's completion (and
    // execute_record then releases its CPU permit). With NIFT_TEST_HOLD_COMPLETION set, a task does not finish while
    // work is queued and no worker is waiting to resume (polled under the runtime mutex, at most 10 s).
    if (std::getenv("NIFT_TEST_HOLD_COMPLETION"))
        if (auto runtime = record.runtime.lock())
            wait_for_test([&] { return runtime->completion_may_proceed_for_test(); });
#endif
#if !(defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT == 8)  // test-only: an un-awaited failure dropped
    if (worker_allocation_failed) {
        if (auto runtime = record.runtime.lock())
            runtime->note_limit(result.message, result.offset, kParallelResourcesExhausted);
        return;
    }
    const auto ends = [&](std::string_view tail) {
        const std::string_view message(result.message);
        return message.size() >= tail.size() && message.substr(message.size() - tail.size()) == tail;
    };
    if (result.reason == StopReason::Error && result.code == "limit.exceeded" &&  // rt-lint: allow-string-compare
        (ends(kParallelBudgetExceeded) || ends(kParallelResourcesExhausted)))
        if (auto runtime = record.runtime.lock())
            runtime->note_limit(result.message, result.offset,
                                ends(kParallelBudgetExceeded) ? kParallelBudgetExceeded : kParallelResourcesExhausted);
#else
    static_cast<void>(result);
    static_cast<void>(record);
    static_cast<void>(worker_allocation_failed);
#endif
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
            build::Array seed_builder;
            const auto g = record->program->main().nregs;
            for (std::size_t i = 0; i < g; ++i)
                seed_builder.push(i < record->input.globals.size() ? record->input.globals[i] : Value());
            for (const auto& v : record->input.args) seed_builder.push(v);
            const Value seeds = std::move(seed_builder).publish();
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
    record->reserved_bytes = ParallelRuntime::kIsolateOverhead + record->input.seed_graph.storage_bytes() +
        record->input.constants_graph.storage_bytes() + image_bytes(*record->program);
    try { return runtime->submit(record, error); }
    catch (const std::bad_alloc&) { throw; }
    catch (const std::system_error&) {  // C140 (a): no worker thread: a resource limit, not an internal failure
        record->failure_code = "limit.exceeded";
        if (kThreadFailureInternal) record->failure_reason = StopReason::Internal;
        error = kParallelResourcesExhausted; return false;
    }
    catch (const std::exception& failure) {
        record->failure_reason = StopReason::Internal;
        error = std::string("cannot enqueue parallel task: ") + failure.what(); return false;
    }
}

} // namespace nift::rt
