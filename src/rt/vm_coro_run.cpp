#include "rt/vm_coro_run.h"

#include "HostTrace.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/parallel_task.h"
#include "rt/string.h"

#include <utility>

// rt-lint: cold-file coroutine run control, once per coroutine opcode or task end

namespace nift::rt {

namespace {
const Note* note_of(const Proto* proto, const Instr* pc) {
    return proto->note(static_cast<std::size_t>(pc - proto->code.data()));
}
}  // namespace

int Vm::op_coro(const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                const Instr*& pc, std::string& output, VmResult& result, Fault& fault) {
    (void)output;
    (void)result;
    if (!coro_) return internal(fault, "coroutine opcode without a scheduler") ? 1 : 0;
    switch (in.op) {
        case OpCode::Spawn:
            if (in.b >= protos_.size() || !Coro::spawn(*this, in, protos_[in.b], Value(), R, fault)) {
                if (fault.reason == StopReason::None) internal(fault, "spawn proto");
                return 0;
            }
            ++pc;
            return 1;
        case OpCode::CallValueAny: return coro_call_value(in, R, K, proto, base, pc, fault);
        case OpCode::Await: return Coro::await(*this, in, R, K, proto, base, pc, fault);
        case OpCode::FutureOp: {
            const FutureObject* future = future_of(R[in.b]);
            if (future == nullptr) return internal(fault, "future method receiver") ? 1 : 0;
            FutureState state = future->state;
            if (future->parallel) {
                const ParallelTaskState parallel = future->parallel->snapshot_state();
                state = parallel == ParallelTaskState::Pending ? FutureState::Pending
                        : parallel == ParallelTaskState::Done  ? FutureState::Done
                                                               : FutureState::Failed;
            }
            Value out = in.c == 0 ? Value::boolean(state != FutureState::Pending)
                                  : make_string(state == FutureState::Pending  ? "running"
                                                : state == FutureState::Failed ? "error"
                                                                               : "done");
            R[in.a] = std::move(out);
            ++pc;
            return memory_ok(fault) ? 1 : 0;
        }
        case OpCode::Sleep: return Coro::sleep(*this, in, R, K, proto, base, pc, fault);
        case OpCode::Yield: return Coro::yield(*this, R, K, proto, base, pc, fault);
        case OpCode::TaskEnd: return Coro::task_end(*this, R, K, proto, base, pc, fault);
        default: return internal(fault, "coroutine opcode") ? 1 : 0;
    }
}

// CallValue in a coroutine unit: an async callee spawns (after legacy's
// arity check, made before spawning), anything else is an ordinary CallValue.
int Vm::coro_call_value(const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                        const Instr*& pc, Fault& fault) {
    const Value& callee = R[in.b];
    const FunctionObject* f = callee.is(Kind::Function) ? callee.heap_as<FunctionObject>() : nullptr;
    const Proto* code = f != nullptr ? static_cast<const Proto*>(f->code) : nullptr;
    if (in.c & 0x8000u) {  // `thread(callee, …)`: runtime callee and arity validation
        if (code == nullptr)
            return raise(fault, StopReason::Error, "thread: first argument must be callable") ? 1 : 0;
        if (code->is_async) return raise(fault, StopReason::Error, "thread: invalid async callable") ? 1 : 0;
        const std::uint16_t argc = static_cast<std::uint16_t>(in.c & 0x7fffu);
        if (argc != code->nparams) {
            const Note* note = note_of(proto, &in);
            if (f->is_lambda)
                return raise(fault, StopReason::Error, "thread: lambda argument count mismatch") ? 1 : 0;
            return raise(fault, StopReason::Error,
                         "thread: callable argument count mismatch: " +
                             std::string(note != nullptr && !note->text.empty() ? note->text : "callable")) ? 1 : 0;
        }
        const Value function = callee;
        if (!Coro::spawn(*this, in, code, function, R, fault, true)) return 0;
        ++pc;
        return 1;
    }
    if (code == nullptr || !code->is_async) return op_call_value(in, R, K, proto, base, pc, fault) ? 1 : 0;
    if (in.c != code->nparams) {
        const Note* note = note_of(proto, &in);
        if (f->is_lambda) return raise(fault, StopReason::Error, "lambda argument count mismatch") ? 1 : 0;
        return raise(fault, StopReason::Error,
                     "callable argument count mismatch: " + std::string(note ? note->text : "")) ? 1 : 0;
    }
    const Value function = callee;
    if (!Coro::spawn(*this, in, code, function, R, fault)) return 0;
    ++pc;
    return 1;
}

// An uncaught stop. Internal: the run stops at once (every task is abandoned
// by coro_teardown). A task's Error: recorded in its future (the original
// Error when catchable, CP13B A2) and the next task runs. Main's Error: the
// run's result; the remaining tasks still drain (legacy L9).
bool Vm::coro_uncaught(Fault& fault, VmResult& result, Value*& R, const Value*& K, const Proto*& proto,
                       std::size_t& base, const Instr*& pc) {
    Coro& coro = *coro_;
    const auto task_position = [&coro](VmResult& r) {
        if (!coro.pending_report) return;
        coro.pending_report = false;
        r.offset = r.main_offset = coro.report_offset;
    };
    if (fault.reason != StopReason::Error) {
        error_result(result, fault, proto, pc);
        coro.pending_report = false;
        return false;
    }
    if (fault.code == "runtime.cancelled") coro_abort_parallel();  // rt-lint: allow-string-compare cancellation
    const std::uint32_t id = coro.current;
    if (id == 0) {
        if (fault.catchable && fault.error.is(Kind::Error)) {
            uncaught_error_ = fault.error;
            has_uncaught_error_ = true;
        }
        error_result(result, fault, proto, pc);
        task_position(result);
    } else {
        VmResult stop;
        error_result(stop, fault, proto, pc);
        task_position(stop);
        Coro::Task& task = coro.tasks[id];
        FutureObject* future = future_of(task.future);
        if (future == nullptr) {
            internal(fault, "task future");
            error_result(result, fault, proto, pc);
            return false;
        }
        FutureFailure& failure = future->failure;
        failure.message = stop.message;
        failure.code = stop.code;
        failure.catchable = fault.catchable && fault.error.is(Kind::Error);
        failure.offset = stop.offset;
        failure.error_origin = stop.error_origin;
        failure.in_loop_path = stop.error_in_loop_path;
        future->error = failure.catchable ? fault.error : Value();
        heap_retally(future);
        task.failed = true;
    }
    global_taken_ = 0;  // a taken slot belonged to the stopped task's own stack
    fault = Fault{};
    Coro::save(*this, proto, base, pc);
    Coro::end_body(*this, id);
    const int step = Coro::next(*this, R, K, proto, base, pc, fault);
    if (step == 1) return true;
    if (step == 0) error_result(result, fault, proto, pc);
    return false;
}

// Main's Halt / TopReturn. The CLI prints a return value here (legacy prints
// it before the tasks still running finish); then the run drains.
int Vm::coro_main_end(Value*& R, const Value*& K, const Proto*& proto, std::size_t& base, const Instr*& pc,
                      std::string& output, VmResult& result, Fault& fault) {
    Coro& coro = *coro_;
    if (coro.current != 0) return internal(fault, "main end outside main") ? 1 : 0;
    drain_parallel_output(output);
    if (options_.return_in_output && result.returned) {
        if (!result.return_text.empty()) {
            output += result.return_text;
            output += '\n';
            ++lines_;
            flush_if_needed(output);
        }
        result.returned = false;
        result.return_text.clear();
    }
#if NIFT_CORO_MUTANT == 7
    return 2;  // M7: the tasks still running are not drained
#endif
    if (coro.tasks.front().children == 0 && coro.parallel_live.empty()) {
        coro.tasks.front().body_done = true;
        coro.flush_parallel_outputs(*this);
        return 2;
    }
    Coro::save(*this, proto, base, pc);
    Coro::end_body(*this, 0);
    return Coro::next(*this, R, K, proto, base, pc, fault);
}

// End of the run (every path): tasks still alive (only after an internal
// stop, or mutant M7) are abandoned; every Value they hold is released once.
void Vm::coro_teardown(VmResult& result) {
    coro_abort_parallel();
    Coro& coro = *coro_;
    const std::string label = coro.tasks.empty() ? std::string() : coro.tasks.front().label;
    if (!coro.tasks.empty()) Coro::release(coro.tasks.front());  // a suspended main's record (else empty)
    for (std::size_t id = 1; id < coro.tasks.size(); ++id) {
        Coro::Task& task = coro.tasks[id];
        if (task.state == Coro::State::Free) continue;
        Coro::release(task);
        task.future = Value();
        task.state = Coro::State::Free;
    }
    coro.run_queue.clear();
    while (!coro.timers.empty()) coro.timers.pop();
    coro.last_result = Value();
    if (!coro.tasks.empty()) coro.tasks.front().future = Value();  // main's internal accounting future
    if (coro.trace) nift::trace::detail::context() = label;
    if (coro.parallel && !options_.current_task) coro.parallel->wait_idle();
    result.coro = true;
    result.coro_spawned = coro.spawned;
    result.coro_switches = coro.switches;
    result.coro_preemptions = coro.preemptions;
    result.coro_wakeups = coro.wakeups;
    result.coro_live_peak = coro.live_peak;
}

std::string coro_stats_json(const VmResult& result) {
    return ",\"coro_spawned\":" + std::to_string(result.coro_spawned) + ",\"coro_switches\":" +
           std::to_string(result.coro_switches) + ",\"coro_preemptions\":" + std::to_string(result.coro_preemptions) +
           ",\"coro_wakeups\":" + std::to_string(result.coro_wakeups) +
           ",\"coro_live_peak\":" + std::to_string(result.coro_live_peak);
}

}  // namespace nift::rt
