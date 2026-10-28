#include "rt/vm_coro_parallel.h"

#include "HostTrace.h"
#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt/parallel_task.h"
#include "rt/shared_state.h"

#include <algorithm>
#include <chrono>
#include <utility>

// rt-lint: cold-file CP23 parallel task bridge, once per spawn/await/reap

namespace nift::rt {

namespace {

void mark_failed(const std::shared_ptr<ParallelTaskRecord>& record, std::string message) {
    std::lock_guard<std::mutex> lock(record->mutex);
    record->state = ParallelTaskState::Failed;
    record->failure_message = std::move(message);
}

} // namespace

bool Vm::Coro::spawn_parallel(Vm& vm, const Instr& in, const Proto* callee, Value* R, Fault& fault, bool thread) {
    Coro& coro = *vm.coro_;
    const std::uint16_t argc = static_cast<std::uint16_t>(in.c & 0x7fffu);
    const auto index = coro.proto_index.find(callee);
    if (index == coro.proto_index.end()) return internal(fault, "spawn proto");
    const std::size_t g = coro.globals;
    std::vector<Value> globals(g);
    std::vector<std::uint8_t> ready(g, 0);
    for (std::uint16_t slot : coro.snapshot[index->second]) {
        if (slot < vm.stack_.size()) globals[slot] = vm.stack_[slot];
        if (slot < vm.global_ready_.size()) ready[slot] = vm.global_ready_[slot];
    }
    auto record = std::make_shared<ParallelTaskRecord>();
    record->program = coro.program_owner;
    record->proto_map = coro.parallel_map;
    record->runtime = coro.parallel;
    record->wait_state = coro.parallel_wait;
    record->owner_task = coro.current;
    record->wait_states.push_back(coro.parallel_wait);
    parallel_task_identity(record);
    if (coro.trace) record->label = nift::trace::child_label(thread ? "thread" : "async");
    auto* future = new FutureObject();
    future->thread = thread;
    future->parallel = record;
    heap_retally(future);
    const Value future_value = Value::adopt(future);
    Value seeds = make_array(g + argc);
    for (const Value& value : globals) array_push(seeds, value);
    for (std::uint16_t k = 0; k < argc; ++k) array_push(seeds, R[in.a + k]);
    TransferGraph graph;
    std::string error;
    const TransferStatus encoded = graph.encode(seeds, *coro.parallel_map, error);
    if (encoded != TransferStatus::Ok) {
        mark_failed(record, error.empty() ? transfer_status_name(encoded) : error);
        R[in.a] = future_value;
        return vm.memory_ok(fault);
    }
    record->input.entry_proto = index->second;
    record->input.argc = argc;
    record->input.global_ready = std::move(ready);
    record->input.proto_map = record->proto_map.get();
    record->input.has_seed_graph = true;
    record->input.seed_graph = std::move(graph);
    record->options = vm.options_;
    record->options.flush = nullptr;
    record->options.flush_context = nullptr;
    record->options.return_in_output = false;
    record->options.return_value_only = true;
    record->options.cooperative_tasks = false;
    record->options.parallel = coro.parallel;
    record->options.program_keepalive = coro.program_owner;
    const bool started = start_parallel_task(record, error);
    if (!started) {
        mark_failed(record, std::move(error));
    } else {
        ++coro.tasks[coro.current].children;
        coro.parallel_live.push_back(record);
        coro.parallel_output.push_back(record);
        ++coro.spawned;
        if (++coro.live > coro.live_peak) coro.live_peak = coro.live;
    }
    R[in.a] = future_value;
    return vm.memory_ok(fault);
}

void Vm::Coro::resolve_parallel(Vm& vm, FutureObject* future,
                                const std::shared_ptr<ParallelTaskRecord>& record) {
    (void)vm;
    std::lock_guard<std::mutex> lock(record->mutex);
    if (future->state != FutureState::Pending || record->state == ParallelTaskState::Pending) return;
    if (record->state == ParallelTaskState::Done) {
        std::string error;
        Value value;
        const TransferProtoMap* map = vm.transfer_map_ ? vm.transfer_map_ : record->proto_map.get();
        if (!record->has_result || (map && record->result.decode(*map, value, error) == TransferStatus::Ok)) {
            future->result = std::move(value);
            future->state = FutureState::Done;
        } else {
            future->state = FutureState::Failed;
            future->failure.message = error.empty() ? "cannot decode the task result" : error;
        }
        return;
    }
    future->state = FutureState::Failed;
    future->failure.message = record->failure_message;
    future->failure.code = record->failure_code;
    future->failure.offset = record->failure_offset;
    future->failure.error_origin = record->failure_error_origin;
    future->failure.in_loop_path = record->failure_in_loop_path;
    future->failure.catchable = record->failure_catchable && record->has_error_value;
    if (future->failure.catchable) {
        std::string error;
        Value value;
        const TransferProtoMap* map = vm.transfer_map_ ? vm.transfer_map_ : record->proto_map.get();
        if (map && record->error_value.decode(*map, value, error) == TransferStatus::Ok &&
            value.is(Kind::Error)) {
            future->error = std::move(value);
        } else {
            future->failure.catchable = false;
        }
    }
    if (!future->failure.catchable) future->error = Value();
}

int Vm::Coro::await_parallel(Vm& vm, const Instr& in, Value*& R, const Value*& K, const Proto*& proto,
                             std::size_t& base, const Instr*& pc, Fault& fault, FutureObject* future, bool join) {
    Coro& coro = *vm.coro_;
    const std::shared_ptr<ParallelTaskRecord> record = future->parallel;
    if (vm.cancellation_requested()) {
        vm.raise_cancelled(fault, proto, proto->offsets[static_cast<std::size_t>(pc - proto->code.data())]);
        return 0;
    }
    bool complete = false;
    bool foreign = false;
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        complete = record->state != ParallelTaskState::Pending;
        if (!complete) {
            foreign = std::find(coro.parallel_live.begin(), coro.parallel_live.end(), record) ==
                      coro.parallel_live.end();
            if (!foreign) {
                record->waiters.push_back(coro.current);
            } else {
                bool known = false;
                for (const auto& state : record->wait_states)
                    if (state.get() == coro.parallel_wait.get()) {
                        known = true;
                        break;
                    }
                if (!known) record->wait_states.push_back(coro.parallel_wait);
            }
        }
    }
    if (!complete) {
        if (foreign) coro.foreign_waits.push_back({record, coro.current});
        coro.tasks[coro.current].state = State::Waiting;
        save(vm, proto, base, pc);
        return next(vm, R, K, proto, base, pc, fault);
    }
    if (vm.cancellation_requested()) {
        vm.raise_cancelled(fault, proto, proto->offsets[static_cast<std::size_t>(pc - proto->code.data())]);
        return 0;
    }
    resolve_parallel(vm, future, record);
    flush_parallel_outputs(vm);
    if (future->state == FutureState::Done) {
        Value value = future->result;
        R[in.a] = std::move(value);
        ++pc;
        return 1;
    }
    const FutureFailure& failure = future->failure;
    if (failure.catchable && future->error.is(Kind::Error)) {
        fault.reason = StopReason::Error;
        fault.error = future->error;
        fault.message = failure.message;
        fault.code = failure.code;
        fault.catchable = true;
        return 0;
    }
    raise(fault, StopReason::Error, (join ? "thread: " : "future: ") + failure.message);
    fault.code = failure.code;
    coro.pending_report = true;
    coro.report_offset = failure.offset;
    return 0;
}

void Vm::Coro::reap_parallel(Vm& vm) {
    Coro& coro = *vm.coro_;
    for (std::size_t i = 0; i < coro.parallel_live.size();) {
        const std::shared_ptr<ParallelTaskRecord> record = coro.parallel_live[i];
        if (!record->complete()) {
            ++i;
            continue;
        }
        std::vector<std::uint32_t> waiters;
        {
            std::lock_guard<std::mutex> lock(record->mutex);
            waiters.swap(record->waiters);
        }
        for (std::uint32_t waiter : waiters) {
            if (waiter >= coro.tasks.size() || coro.tasks[waiter].state != State::Waiting) continue;
            coro.tasks[waiter].state = State::Ready;
            coro.run_queue.push_back(waiter);
        }
        const std::uint32_t owner = record->owner_task;
        if (owner < coro.tasks.size() && coro.tasks[owner].children != 0) --coro.tasks[owner].children;
        if (owner < coro.tasks.size()) finish(vm, owner);
        coro.parallel_live.erase(coro.parallel_live.begin() + static_cast<std::ptrdiff_t>(i));
        if (coro.live != 0) --coro.live;
        ++coro.finished;
    }
}

void Vm::Coro::reap_foreign(Vm& vm) {
    Coro& coro = *vm.coro_;
    for (std::size_t i = 0; i < coro.foreign_waits.size();) {
        const ForeignWait& wait = coro.foreign_waits[i];
        if (!wait.record || !wait.record->complete()) {
            ++i;
            continue;
        }
        if (wait.task < coro.tasks.size() && coro.tasks[wait.task].state == State::Waiting) {
            coro.tasks[wait.task].state = State::Ready;
            coro.run_queue.push_back(wait.task);
        }
        coro.foreign_waits.erase(coro.foreign_waits.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void Vm::Coro::flush_parallel_outputs(Vm& vm) {
    if (vm.coro_output_ == nullptr) return;
    Coro& coro = *vm.coro_;
    while (!coro.parallel_output.empty()) {
        const std::shared_ptr<ParallelTaskRecord> record = coro.parallel_output.front();
        std::string text;
        {
            std::lock_guard<std::mutex> lock(record->mutex);
            if (record->state == ParallelTaskState::Pending) return;
            if (record->output_merged) {
                coro.parallel_output.pop_front();
                continue;
            }
            text.swap(record->output);
            record->output_merged = true;
        }
        *vm.coro_output_ += text;
        coro.parallel_output.pop_front();
    }
}

void Vm::Coro::wait_parallel(Vm& vm, std::uint64_t observed) {
    Coro& coro = *vm.coro_;
    const std::shared_ptr<ParallelWait> state = coro.parallel_wait;
    if (!state) return;
    std::unique_lock<std::mutex> lock(state->mutex);
    const auto ready = [&state, observed] { return state->generation != observed; };
    if (coro.timers.empty()) {
        state->cv.wait(lock, ready);
    } else {
        state->cv.wait_until(lock, coro.timers.top().at, ready);
    }
    lock.unlock();
    if (!coro.timers.empty() && coro.timers.top().at <= Clock::now()) fire_due(coro, Clock::now());
}

}  // namespace nift::rt
