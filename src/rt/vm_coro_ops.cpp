#include "rt/vm_coro_ops.h"

#include "HostTrace.h"
#include "rt/error.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/number.h"
#include "rt/string.h"
#include "rt/vm_coro.h"

#include <chrono>
#include <limits>
#include <utility>

// rt-lint: cold-file coroutine opcodes: spawn, await, sleep, safepoints (off the arithmetic hot path)

namespace nift::rt {

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point deadline_after(std::int64_t milliseconds) {
    const Clock::time_point now = Clock::now();
    const auto room = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::time_point::max() - now).count();
    if (milliseconds >= room) return Clock::time_point::max();
    return now + std::chrono::milliseconds(milliseconds);
}

}  // namespace

bool Vm::Coro::spawn(Vm& vm, const Instr& in, const Proto* callee, const Value& function, Value* R, Fault& fault,
                     bool thread) {
    if (!vm.options_.cooperative_tasks && vm.options_.parallel)
        return spawn_parallel(vm, in, callee, R, fault, thread);
    Coro& coro = *vm.coro_;
    const std::uint16_t argc = static_cast<std::uint16_t>(in.c & 0x7fffu);
    std::uint32_t id = 0;
    if (!coro.free_ids.empty()) {
        id = coro.free_ids.back();
        coro.free_ids.pop_back();
    } else {
        id = static_cast<std::uint32_t>(coro.tasks.size());
        coro.tasks.emplace_back();
    }
    Task& task = coro.tasks[id];
    const std::size_t g = coro.globals;
    task.stack.assign(g + callee->nregs + 1u, Value());
    task.ready.assign(vm.global_ready_.size(), 0);
    const auto index = coro.proto_index.find(callee);
    if (index == coro.proto_index.end()) return internal(fault, "spawn proto");
    for (std::uint16_t slot : coro.snapshot[index->second]) {
        if (slot < vm.stack_.size()) task.stack[slot] = vm.stack_[slot];
        if (slot < vm.global_ready_.size()) task.ready[slot] = vm.global_ready_[slot];
    }
    for (std::uint16_t k = 0; k < argc; ++k) task.stack[g + k] = std::move(R[in.a + k]);
    task.frames.assign(1, Frame{&coro.trampoline.code[1], g, &coro.trampoline, nullptr, false});
    task.handlers.clear();
    task.function = function;
    task.closure = function.is(Kind::Function) ? function.heap_as<FunctionObject>() : nullptr;
    task.proto = callee;
    task.base = g;
    task.pc = callee->code.data();
    task.next_frame_check = kLegacyMaxDepth < vm.options_.max_frames ? kLegacyMaxDepth : vm.options_.max_frames;
    task.state = State::Ready;
    task.body_done = task.failed = false;
    task.parent = coro.current;
    task.children = 0;
    ++coro.tasks[coro.current].children;
    auto* future = new FutureObject();
    future->thread = thread;
    heap_retally(future);
    task.future = Value::adopt(future);
    if (coro.trace) task.label = nift::trace::child_label(thread ? "thread" : "async");
    coro.run_queue.push_back(id);
    ++coro.spawned;
    if (++coro.live > coro.live_peak) coro.live_peak = coro.live;
    charge(task, true);
    R[in.a] = task.future;
    return vm.memory_ok(fault);
}

// `await R[b]` (c = 0) or a thread's `join()` (c = 1): a value at once, the
// original Error re-raised (CP13B A2), or a fatal failure reported at the
// task's position with legacy's `future: ` / `thread: `.
int Vm::Coro::await(Vm& vm, const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                    const Instr*& pc, Fault& fault) {
    Coro& coro = *vm.coro_;
    FutureObject* future = future_of(R[in.b]);
    const bool join = in.c == 1;
    if (future != nullptr && future->thread != join && join) return internal(fault, "join receiver") ? 1 : 0;
#if NIFT_CORO_MUTANT == 11
    if (future == nullptr)  // M11: `await` accepts a thread handle
#else
    if (future == nullptr || future->thread != join)
#endif
        return raise(fault, StopReason::Error, "await: expected future") ? 1 : 0;
    if (future->parallel)
        return coro.await_parallel(vm, in, R, K, proto, base, pc, fault, future, join);
    if (future->state == FutureState::Pending) {
        future->waiters.push_back(coro.current);
        heap_retally(future);
        coro.tasks[coro.current].state = State::Waiting;
        save(vm, proto, base, pc);  // resumes at this Await, which then finds it complete
        return next(vm, R, K, proto, base, pc, fault);
    }
    if (future->state == FutureState::Done) {
#if NIFT_CORO_MUTANT == 5
        Value value = coro.last_result;  // M5: the result of the wrong (last completed) future
#else
        Value value = future->result;
#endif
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
#if NIFT_CORO_MUTANT == 8
        SourceOrigin origin;  // M8: the Error re-stamped with the await site
        if (vm.origin_at(proto->offsets[static_cast<std::size_t>(pc - proto->code.data())], origin, fault))
            fault.error = clone_error_with_origin(fault.error, origin);
#endif
        return 0;
    }
    raise(fault, StopReason::Error, (join ? "thread: " : "future: ") + failure.message);
    fault.code = failure.code;
    coro.pending_report = true;
    coro.report_offset = failure.offset;
    return 0;
}

// `sleep(ms)` in a coroutine unit: the CP16 native validates the duration and
// traces the Host op (called with 0 ms when the duration is valid), then the
// task suspends until its deadline. sleep(0) does not yield (as legacy).
int Vm::Coro::sleep(Vm& vm, const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                    const Instr*& pc, Fault& fault) {
    Coro& coro = *vm.coro_;
    std::int64_t milliseconds = 0;
    const bool valid = number_to_i64(R[in.a], milliseconds) && milliseconds >= 0;
    if (valid) R[in.a] = Value::integer(0);
    if (!vm.builtin(in, proto, R, fault)) return 0;
    if (!valid || milliseconds == 0) {
        ++pc;
        return 1;
    }
    Task& task = coro.tasks[coro.current];
    task.state = State::Sleeping;
    task.sleep_seq = ++coro.timer_seq;
    coro.timers.push(Timer{deadline_after(milliseconds), task.sleep_seq, coro.current});
    save(vm, proto, base, pc + 1);
    return next(vm, R, K, proto, base, pc, fault);
}

// A loop back-edge safepoint: every kCoroSlice of them, due timers are
// polled and, when another task is ready, the running one goes to the back
// of the run queue.
int Vm::Coro::yield(Vm& vm, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                    const Instr*& pc, Fault& fault) {
    if (vm.cancellation_requested()) {
        vm.raise_cancelled(fault, proto, proto->offsets[static_cast<std::size_t>(pc - proto->code.data())]);
        return 0;
    }
    Coro& coro = *vm.coro_;
    ++pc;
    if (--coro.budget != 0) return 1;
    coro.budget = kCoroSlice;
#if NIFT_CORO_MUTANT == 10
    return 1;  // M10: no preemption
#endif
    if (!coro.timers.empty()) fire_due(coro, Clock::now());
    if (coro.run_queue.empty()) return 1;
    if (vm.global_taken_ != 0) return internal(fault, "global taken at a safepoint") ? 1 : 0;
    coro.tasks[coro.current].state = State::Ready;
    coro.run_queue.push_back(coro.current);
    ++coro.preemptions;
    save(vm, proto, base, pc);
    return next(vm, R, K, proto, base, pc, fault);
}

// The trampoline's TaskEnd: R[0] is the task's return value.
int Vm::Coro::task_end(Vm& vm, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                       const Instr*& pc, Fault& fault) {
    Coro& coro = *vm.coro_;
    const std::uint32_t id = coro.current;
    if (id == 0 || vm.global_taken_ != 0) return internal(fault, "task end") ? 1 : 0;
    FutureObject* future = future_of(coro.tasks[id].future);
    if (future == nullptr) return internal(fault, "task future") ? 1 : 0;
    future->result = std::move(R[0]);
#if NIFT_CORO_MUTANT == 5
    coro.last_result = future->result;
#endif
#if NIFT_CORO_MUTANT == 6
    Task& main = coro.tasks.front();  // M6: the task's global writes reach main
    for (std::uint16_t slot : coro.snapshot[coro.proto_index[coro.tasks[id].proto]])
        if (slot < main.stack.size() && slot < vm.stack_.size()) main.stack[slot] = vm.stack_[slot];
#endif
    save(vm, proto, base, pc);
    end_body(vm, id);
    return next(vm, R, K, proto, base, pc, fault);
}

}  // namespace nift::rt
