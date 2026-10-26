#include "rt/vm_coro.h"

#include "HostTrace.h"
#include "rt/heap_account.h"

#include <algorithm>
#include <utility>

// rt-lint: cold-file coroutine scheduling, once per task switch (not per instruction)

namespace nift::rt {

void Vm::CoroDelete::operator()(Coro* coro) const noexcept { delete coro; }

std::size_t Vm::Coro::storage_bytes(const Task& task) noexcept {
    return task.stack.capacity() * sizeof(Value) + task.frames.capacity() * sizeof(Frame) +
           task.handlers.capacity() * sizeof(Handler) + task.ready.capacity() + task.label.capacity();
}

void Vm::Coro::charge(Task& task, bool suspended) noexcept {
    FutureObject* future = future_of(task.future);
    if (future == nullptr) return;
    future->task_bytes = suspended ? storage_bytes(task) : 0;
    heap_retally(future);
}

void Vm::Coro::release(Task& task) noexcept {
    std::vector<Value>().swap(task.stack);
    std::vector<std::uint8_t>().swap(task.ready);
    std::vector<Frame>().swap(task.frames);
    std::vector<Handler>().swap(task.handlers);
    task.function = Value();
    task.closure = nullptr;
    charge(task, false);
}

void Vm::coro_init() {
    coro_.reset(new Coro());
    Coro& coro = *coro_;
    coro.globals = protos_.front()->nregs;
    coro.trampoline.name = "<task>";
    coro.trampoline.nregs = 1;
    coro.trampoline.code = {Instr{OpCode::Nop, 0, 0, 0, 0}, Instr{OpCode::TaskEnd, 0, 0, 0, 0}};
    coro.trampoline.offsets = {0, 0};
    coro.trampoline.notes = {0, 0};
    coro.trace = nift::trace::enabled();
    Coro::Task main;
    main.state = Coro::State::Running;
    auto* future = new FutureObject();  // internal: carries main's storage while it is suspended
    heap_retally(future);
    main.future = Value::adopt(future);
    if (coro.trace) main.label = nift::trace::detail::context();
    coro.tasks.push_back(std::move(main));
    for (std::size_t p = 0; p < protos_.size(); ++p)
        coro.proto_index.emplace(protos_[p], static_cast<std::uint32_t>(p));
    Coro::build_snapshot(coro, protos_);
}

namespace {

void add_slot(std::vector<std::uint16_t>& slots, std::uint16_t slot) {
    if (std::find(slots.begin(), slots.end(), slot) == slots.end()) slots.push_back(slot);
}

}  // namespace

// The global slots a task of an async proto can reach: the slots read or
// written by every proto reachable from it through Call, Spawn, Closure and
// LoadFunction; a proto that calls a value (CallValue, CallValueAny,
// CollectionCall) can reach every proto that is ever made a function value.
void Vm::Coro::build_snapshot(Coro& coro, const std::vector<const Proto*>& protos) {
    const std::size_t n = protos.size();
    std::vector<std::vector<std::uint16_t>> direct(n);
    std::vector<std::vector<std::uint32_t>> edges(n);
    std::vector<bool> dynamic(n, false);
    std::vector<std::uint32_t> values;  // protos made function values anywhere
    for (std::size_t p = 0; p < n; ++p) {
        for (const Instr& in : protos[p]->code) {
            switch (in.op) {
                case OpCode::GetGlobal: case OpCode::GetGlobalChecked: add_slot(direct[p], in.b); break;
                case OpCode::SetGlobal: case OpCode::MarkGlobal: case OpCode::CheckGlobal:
                    add_slot(direct[p], in.a);
                    break;
                case OpCode::CollectionSort: if (in.c & 0x10) add_slot(direct[p], in.b); break;
                case OpCode::Call: case OpCode::Spawn: edges[p].push_back(in.b); break;
                case OpCode::Closure: case OpCode::LoadFunction:
                    edges[p].push_back(in.b);
                    values.push_back(in.b);
                    break;
                case OpCode::CallValue: case OpCode::CallValueAny: case OpCode::CollectionCall:
                    dynamic[p] = true;
                    break;
                default: break;
            }
        }
    }
    coro.snapshot.assign(n, {});
    for (std::size_t root = 1; root < n; ++root) {
        if (!protos[root]->is_async) continue;
        std::vector<bool> seen(n, false);
        std::vector<std::uint32_t> work{static_cast<std::uint32_t>(root)};
        bool widened = false;
        std::vector<std::uint16_t>& slots = coro.snapshot[root];
        while (!work.empty()) {
            const std::uint32_t p = work.back();
            work.pop_back();
            if (p >= n || seen[p]) continue;
            seen[p] = true;
            for (std::uint16_t slot : direct[p]) add_slot(slots, slot);
            for (std::uint32_t q : edges[p]) work.push_back(q);
            if (dynamic[p] && !widened) {
                widened = true;
                for (std::uint32_t q : values) work.push_back(q);
            }
        }
        std::sort(slots.begin(), slots.end());
    }
}

void Vm::Coro::save(Vm& vm, const Proto* proto, std::size_t base, const Instr* pc) {
    Coro& coro = *vm.coro_;
    Task& task = coro.tasks[coro.current];
    task.proto = proto;
    task.base = base;
    task.pc = pc;
    task.closure = vm.closure_;
    task.next_frame_check = vm.next_frame_check_;
    task.stack.swap(vm.stack_);
    task.frames.swap(vm.frames_);
    task.handlers.swap(vm.handlers_);
    task.ready.swap(vm.global_ready_);
    charge(task, true);
}

void Vm::Coro::load(Vm& vm, std::uint32_t id, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                    const Instr*& pc) {
    Coro& coro = *vm.coro_;
    Task& task = coro.tasks[id];
    charge(task, false);
    coro.current = id;
    task.state = State::Running;
    vm.stack_.swap(task.stack);
    vm.frames_.swap(task.frames);
    vm.handlers_.swap(task.handlers);
    vm.global_ready_.swap(task.ready);
    vm.closure_ = task.closure;
    vm.next_frame_check_ = task.next_frame_check;
    proto = task.proto;
    base = task.base;
    pc = task.pc;
    R = vm.stack_.data() + base;
    K = proto->constants.data();
    if (coro.trace) nift::trace::detail::context() = task.label;
    ++coro.switches;
    vm.update_heap_limit();
}

void Vm::Coro::fire_due(Coro& coro, Clock::time_point now) {
#if NIFT_CORO_MUTANT == 3
    now += std::chrono::milliseconds(150);  // M3: timers fire early
#endif
    while (!coro.timers.empty() && coro.timers.top().at <= now) {
        const std::uint32_t id = coro.timers.top().task;
        const std::uint64_t seq = coro.timers.top().seq;
        coro.timers.pop();
        if (coro.tasks[id].state != State::Sleeping || coro.tasks[id].sleep_seq != seq) continue;
        coro.tasks[id].state = State::Ready;
        coro.run_queue.push_back(id);
#if NIFT_CORO_MUTANT == 2
        coro.run_queue.push_back(id);  // M2: double resume
#endif
        ++coro.wakeups;
    }
}

int Vm::Coro::next(Vm& vm, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base, const Instr*& pc,
                   Fault& fault) {
    Coro& coro = *vm.coro_;
    for (;;) {
        while (!coro.run_queue.empty()) {
            const std::uint32_t id = coro.run_queue.front();
            coro.run_queue.pop_front();
#if NIFT_CORO_MUTANT != 2
            if (coro.tasks[id].state != State::Ready) continue;  // a stale entry (never resumed twice)
#endif
            load(vm, id, R, K, proto, base, pc);
            return 1;
        }
        const Task& main = coro.tasks.front();
        if (main.body_done && main.children == 0) return 2;
        if (coro.timers.empty()) {
            // Unreachable by construction (a future only refers to an earlier
            // task, so waits form no cycle): an internal stop, never a hang.
            return raise(fault, StopReason::Internal, "coroutine deadlock") ? 1 : 0;
        }
#if NIFT_CORO_MUTANT == 3
        coro.wake->wait_until(coro.timers.top().at - std::chrono::milliseconds(150));  // M3: early wake-up
#else
        coro.wake->wait_until(coro.timers.top().at);
#endif
        fire_due(coro, Clock::now());
    }
}

void Vm::Coro::publish(Coro& coro, Task& task) {
    FutureObject* future = future_of(task.future);
    if (future == nullptr) return;
    future->state = task.failed ? FutureState::Failed : FutureState::Done;
#if NIFT_CORO_MUTANT == 1
    if (!future->waiters.empty()) future->waiters.pop_back();  // M1: a lost wakeup
#endif
    for (std::uint32_t waiter : future->waiters) {
        if (coro.tasks[waiter].state != State::Waiting) continue;
        coro.tasks[waiter].state = State::Ready;
        coro.run_queue.push_back(waiter);
    }
    std::vector<std::uint32_t>().swap(future->waiters);
    heap_retally(future);
}

void Vm::Coro::finish(Vm& vm, std::uint32_t id) {
    Coro& coro = *vm.coro_;
    for (std::uint32_t at = id; at != 0 && at != kNoTask;) {  // main (0) ends the run instead
        Task& task = coro.tasks[at];
        if (!task.body_done || task.children != 0 || task.state == State::Free) return;
        const std::uint32_t parent = task.parent;
        publish(coro, task);
        task.state = State::Free;
        task.future = Value();
        std::string().swap(task.label);
        coro.free_ids.push_back(at);
        --coro.live;
        ++coro.finished;
        if (parent == kNoTask) return;
        --coro.tasks[parent].children;
        at = parent;
    }
}

void Vm::Coro::end_body(Vm& vm, std::uint32_t id) {
    Coro& coro = *vm.coro_;
    Task& task = coro.tasks[id];
    task.body_done = true;
    task.state = State::Finishing;
#if NIFT_CORO_MUTANT == 4
    if (id != 0) {  // M4: a finished task's frames and registers are never released
        static std::vector<std::vector<Value>> leaked;
        leaked.push_back(std::move(task.stack));
    }
#endif
    release(task);
#if NIFT_CORO_MUTANT == 9
    if (id != 0) publish(coro, task);  // M9: the future completes before the task's children
#endif
    finish(vm, id);
}

}  // namespace nift::rt
