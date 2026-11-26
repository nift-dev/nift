#include "rt/vm_coro_parallel.h"

#include "HostTrace.h"
#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt/parallel_task.h"
#include "rt/shared_state_internal.h"

#include <algorithm>
#include <chrono>
#include <new>
#include <utility>

// rt-lint: cold-file CP23 parallel task bridge, once per spawn/await/reap

namespace nift::rt {

namespace {

void mark_failed(const std::shared_ptr<ParallelTaskRecord>& record, std::string message) {
    std::lock_guard<std::mutex> lock(record->mutex);
    record->failure_message = std::move(message);
    record->state = ParallelTaskState::Failed;
}

TransferStatus decode_parallel_graph(const TransferGraph& graph, const TransferProtoMap& map,
                                     Value& value, std::string& error) {
    if (graph.shared_backings().empty() && graph.task_backings().empty())
        return graph.decode(map, value, error);
    // Decoding promotes neutral edges to isolate roots. The collector must
    // see the old source or the new roots, even if the source is then dropped.
    // Take the registry lock only after releasing the record's task mutex.
    std::lock_guard<std::recursive_mutex> lock(shared_detail::registry().mutex);
    return graph.decode(map, value, error);
}

} // namespace

bool Vm::Coro::spawn_parallel(Vm& vm, const Instr& in, const Proto* callee, const Value& function, Value* R,
                              Fault& fault, bool thread) {
    try { return create_parallel(vm, in, callee, function, R, fault, thread); }
    catch (const std::bad_alloc&) {  // C140a F3 (D-081): creating the task ran out of memory; nothing was published
        if (kAllocFailureInternal) throw;
        fault.code = "limit.exceeded"; fault.catchable = false;
        return raise(fault, StopReason::Error, kParallelResourcesExhausted);
    }
}

bool Vm::Coro::create_parallel(Vm& vm, const Instr& in, const Proto* callee,
                               const Value& function, Value* R, Fault& fault,
                               bool thread) {
    Coro& coro = *vm.coro_;
    const std::uint16_t argc = static_cast<std::uint16_t>(in.c & 0x7fffu);
    const auto index = coro.proto_index.find(callee);
    if (index == coro.proto_index.end()) return internal(fault, "spawn proto");
    // Async-recursion fix (c): in an isolate, slot 0 is its `<isolate-task>` wrapper (no transfer wire) and the
    // global window is the wrapper's registers. A child gets the original program and the original globals (the
    // wrapper's first `globals` registers), so nesting does not stack wrappers, constants and slots per level.
    std::uint32_t wire = 0;
    const std::size_t first = coro.parallel_map->index_of(vm.program_.protos.front().get(), wire) ? 0 : 1;
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT == 5  // test-only: the wrapped image again
    const std::size_t skip = 0;
    static_cast<void>(first);
#else
    const std::size_t skip = first;
#endif
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT == 7  // test-only: wrapper registers seeded too
    const std::size_t g = coro.globals;
#else
    const std::size_t g = skip != 0 ? vm.program_.protos[1]->nregs : coro.globals;
#endif
    std::vector<Value> globals(g);
    std::vector<std::uint8_t> ready(g, 0);
    for (std::uint16_t slot : coro.snapshot[index->second]) {
        if (slot >= g) continue;
        if (slot < vm.stack_.size()) globals[slot] = vm.stack_[slot];
        if (slot < vm.global_ready_.size()) ready[slot] = vm.global_ready_[slot];
    }
    auto record = std::make_shared<ParallelTaskRecord>();

    record->proto_map = coro.parallel_map;
    record->runtime = coro.parallel->weak_from_this();
    record->wait_state = coro.parallel_wait;
    record->owner_task = coro.current;
    record->wait_states.push_back(coro.parallel_wait);
    if (coro.trace) record->label = nift::trace::child_label(thread ? "thread" : "async");
    for (std::size_t i = 0; i < vm.program_.protos.size(); ++i) {  // C140a: where it was spawned (cold, per spawn)
        const auto& code = vm.program_.protos[i]->code;
        if (&in >= code.data() && &in < code.data() + code.size()) {
            record->spawn_proto = static_cast<std::uint32_t>(i);
            record->spawn_instr = static_cast<std::uint32_t>(&in - code.data());
            record->spawn_offset = vm.program_.protos[i]->offsets[record->spawn_instr];
        }
    }
    auto* future = new FutureObject();
    future->collect_parallel = shared_collect;
    future->thread = thread;
    future->parallel = record;
    heap_retally(future);
    const Value future_value = Value::adopt(future);
    build::Array seed_builder(g + argc);
    for (const Value& value : globals) seed_builder.push(value);
    for (std::uint16_t k = 0; k < argc; ++k) seed_builder.push(R[in.a + k]);
    record->input.has_function = function.is(Kind::Function);
    if (record->input.has_function) seed_builder.push(function);
    const Value seeds = std::move(seed_builder).publish();
    TransferGraph graph;
    std::string error;
    const TransferStatus encoded = graph.encode(seeds, *coro.parallel_map, error);
    if (encoded != TransferStatus::Ok) {
        mark_failed(record, error.empty() ? transfer_status_name(encoded) : error);
        R[in.a] = future_value;
        return vm.memory_ok(fault);
    }
    if (!snapshot_isolate_program(vm.program_, *coro.parallel_map, record->input, record->program, error, skip)) {
        mark_failed(record, error); R[in.a] = future_value;
        return vm.memory_ok(fault);
    }
    record->input.entry_proto = static_cast<std::uint32_t>(index->second - skip);
    record->input.argc = argc;
    record->input.global_ready = std::move(ready);
    record->input.proto_map = record->proto_map.get();
    record->input.has_seed_graph = true;
    record->input.seed_graph = std::move(graph);
    record->options = vm.options_;
    record->options.current_task.reset();  // queued work must not retain its parent record
    record->options.flush = nullptr;
    record->options.flush_context = nullptr;
    record->options.return_in_output = false;
    record->options.return_value_only = true;
    // CP19 x CP23 (integration review F1): a worker never writes the parent's
    // eval/session result or touches its persistent session globals.
    record->options.return_value = nullptr;
    record->options.session_globals = nullptr;
    record->options.cooperative_tasks = false;
    record->options.parallel = coro.parallel;
    record->options.program_keepalive = record->program;
    // Publish the identity only once the spawn input is constructed, but
    // before a worker can run. The future is still local until admission
    // ends, so an exception here cannot leave a graph referencing this task.
    parallel_task_identity(record);
    bool started = false;
    std::size_t listed = 0;  // C140a F3: listed before the start (nothing throws after it); unlisted unless started
    const auto unlist = [&] {
        if (listed > 1) coro.parallel_output.pop_back();
        if (listed > 0) coro.parallel_live.pop_back();
    };
    try {
        coro.parallel_live.push_back(record); ++listed;
        coro.parallel_output.push_back(record); ++listed;
        NIFT_INJECT_BAD_ALLOC("start");
        started = start_parallel_task(record, error);
    } catch (...) { unlist(); throw; }
    if (!started) unlist();
    if (!started && record->failure_code == "limit.exceeded" && record->failure_reason == StopReason::Error) {
        fault.code = "limit.exceeded"; fault.catchable = false;  // C140: fatal at the spawn, awaited or not
        return raise(fault, StopReason::Error, error);
    }
    if (!started) {
        mark_failed(record, std::move(error));
    } else {
        ++coro.tasks[coro.current].children;
        ++coro.spawned;
        if (++coro.live > coro.live_peak) coro.live_peak = coro.live;
    }
    R[in.a] = future_value;
    return vm.memory_ok(fault);
}

void Vm::Coro::resolve_parallel(Vm& vm, FutureObject* future,
                                const std::shared_ptr<ParallelTaskRecord>& record) {
    (void)vm;
    std::unique_lock<std::mutex> lock(record->mutex);
    if (future->state != FutureState::Pending || record->state == ParallelTaskState::Pending) return;
    // The caller retains an external record root. Decode can take the
    // registry lock, so release the task mutex after observing publication.
    lock.unlock();
    if (record->state == ParallelTaskState::Done) {
        std::string error;
        Value value;
        const TransferProtoMap* map = vm.transfer_map_ ? vm.transfer_map_ : record->proto_map.get();
        if (!record->has_result ||
            (map && decode_parallel_graph(record->result, *map, value, error) == TransferStatus::Ok)) {
            future->result = std::move(value);
            future->state = FutureState::Done;
        } else {
            future->state = FutureState::Failed;
            future->failure.message = error.empty() ? "cannot decode the task result" : error;
        }
        return;
    }
    future->state = FutureState::Failed;
    future->failure.message = record->failure_resources_exhausted ? kParallelResourcesExhausted
                                                                   : record->failure_message;
    future->failure.code = record->failure_resources_exhausted ? "limit.exceeded" : record->failure_code;
    future->failure.offset = record->failure_offset;
    future->failure.error_origin = record->failure_error_origin;
    future->failure.in_loop_path = record->failure_in_loop_path;
    future->failure.catchable = record->failure_catchable && record->has_error_value;
    if (future->failure.catchable) {
        std::string error;
        Value value;
        const TransferProtoMap* map = vm.transfer_map_ ? vm.transfer_map_ : record->proto_map.get();
        if (map && decode_parallel_graph(record->error_value, *map, value, error) == TransferStatus::Ok &&
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
    coro.clear_parallel_await(coro.current);
    if (vm.cancellation_requested()) {
        vm.raise_cancelled(fault, proto, proto->offsets[static_cast<std::size_t>(pc - proto->code.data())]);
        return 0;
    }
    bool complete = false;
    bool foreign = false;
    std::shared_ptr<ParallelWaitRegistration> registration;
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        complete = record->state != ParallelTaskState::Pending;
        if (!complete) {
            registration = std::make_shared<ParallelWaitRegistration>(record);
            foreign = std::find(coro.parallel_live.begin(), coro.parallel_live.end(), record) ==
                      coro.parallel_live.end();
            if (!foreign)
                record->waiters.push_back({coro.current, registration.get()});
            else
                record->wait_observers.push_back({registration.get(), coro.parallel_wait});
        }
    }
    if (!complete) {
        coro.tasks[coro.current].parallel_await = std::move(registration);
        if (foreign) coro.foreign_waits.push_back({record, coro.current,
                                                 coro.tasks[coro.current].parallel_await.get()});
        coro.tasks[coro.current].state = State::Waiting;
        save(vm, proto, base, pc);
        return next(vm, R, K, proto, base, pc, fault);
    }
    if (vm.cancellation_requested()) {
        vm.raise_cancelled(fault, proto, proto->offsets[static_cast<std::size_t>(pc - proto->code.data())]);
        return 0;
    }
    if (record->failure_reason == StopReason::Internal)
        return raise(fault, StopReason::Internal, record->failure_message) ? 1 : 0;
    resolve_parallel(vm, future, record);
    flush_parallel_outputs(vm);
    if (vm.coro_output_) vm.drain_parallel_output(*vm.coro_output_);
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

bool Vm::Coro::reap_parallel(Vm& vm, Fault& fault) {
    Coro& coro = *vm.coro_;
    for (std::size_t i = 0; i < coro.parallel_live.size();) {
        const std::shared_ptr<ParallelTaskRecord> record = coro.parallel_live[i];
        if (!record->complete()) {
            ++i;
            continue;
        }
        if (record->failure_reason == StopReason::Internal)
            return raise(fault, StopReason::Internal, record->failure_message);
        std::vector<ParallelTaskWaiter> waiters;
        {
            std::lock_guard<std::mutex> lock(record->mutex);
            waiters.swap(record->waiters);
        }
        for (const auto& entry : waiters) {
            const std::uint32_t waiter = entry.task;
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
    return true;
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

void Vm::Coro::clear_parallel_await(std::uint32_t id) noexcept {
    auto& registration = tasks[id].parallel_await;
    if (!registration) return;
    const auto* identity = registration.get();
    foreign_waits.erase(std::remove_if(foreign_waits.begin(), foreign_waits.end(),
        [&](const ForeignWait& wait) { return wait.registration == identity; }), foreign_waits.end());
    registration.reset(); // detaches only this lease under the record mutex
}

bool Vm::Coro::wait_parallel(Vm& vm, std::uint64_t observed, Fault& fault) {
    Coro& coro = *vm.coro_;
    const std::shared_ptr<ParallelWait> state = coro.parallel_wait;
    if (!state) return true;
    std::unique_lock<std::mutex> lock(state->mutex);
    const auto ready = [&state, observed] { return state->generation != observed; };
    lock.unlock();
    {
    ParallelBlock blocked;
    if (!blocked.admitted()) {
        fault.code = "limit.exceeded"; fault.catchable = false;
        return raise(fault, StopReason::Error, blocked.refusal());
    }
    lock.lock();
    if (coro.timers.empty()) {
        state->cv.wait(lock, ready);
    } else {
        state->cv.wait_until(lock, coro.timers.top().at, ready);
    }
    lock.unlock();
    }
    if (!coro.timers.empty() && coro.timers.top().at <= Clock::now()) fire_due(coro, Clock::now());
    return true;
}

}  // namespace nift::rt
