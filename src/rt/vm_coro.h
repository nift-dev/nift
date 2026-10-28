#pragma once
// CP22 coroutine scheduler of one VM run (docs/runtime-2/checkpoints/cp22-coroutines.md):
// task records (each a suspended VM computation with its own register stack,
// frames, handlers and global snapshot), the FIFO run queue, the timer heap,
// structured completion and the per-proto global snapshot slots. Single
// threaded: only the VM thread touches it (D-001 #7); other threads reach the
// run only through the WakeSource.
#include "rt/coro_wake.h"
#include "rt/future.h"
#include "rt/vm.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace nift::rt {

struct ParallelTaskRecord;
struct ParallelWait;
struct ParallelRuntime;
class ProgramTransferMap;

// Test-only deliberate defects (`-DNIFT_CORO_MUTANT=n`, tests/runtime2/cp22):
// each must be caught by its named fixture. Normal builds contain none.
#ifndef NIFT_CORO_MUTANT
#define NIFT_CORO_MUTANT 0
#endif

inline constexpr std::uint32_t kNoTask = 0xffffffffu;
// Loop back-edge safepoints between two preemption checks (timers polled,
// rotation if another task is ready). Deterministic: no clock in the decision
// to check, only in which timers are due.
inline constexpr std::uint32_t kCoroSlice = 1024;

struct Vm::Coro {
    using Clock = std::chrono::steady_clock;
    enum class State : std::uint8_t { Free, Ready, Running, Waiting, Sleeping, Finishing };

    struct Task {
        std::vector<Value> stack;          // registers; [0, globals) is the task's global snapshot
        std::vector<std::uint8_t> ready;   // its global_ready_
        std::vector<Frame> frames;         // frames_[0] is the trampoline (main: none)
        std::vector<Handler> handlers;     // CP13B handlers of its frames
        const FunctionObject* closure = nullptr;
        Value function;  // the callee value (keeps an async lambda alive)
        Value future;    // its FutureObject (main's is internal: memory accounting only)
        const Proto* proto = nullptr;
        std::size_t base = 0;
        const Instr* pc = nullptr;  // resume point
        std::size_t next_frame_check = 0;
        State state = State::Free;
        bool body_done = false;
        bool failed = false;
        std::uint32_t parent = kNoTask;
        std::uint32_t children = 0;  // live tasks it spawned (structured completion)
        std::uint64_t sleep_seq = 0;  // the timer entry that may wake it (stale entries are ignored)
        std::string label;           // Host-trace context ("<owner>/async#N")
    };
    struct Timer {
        Clock::time_point at;
        std::uint64_t seq;
        std::uint32_t task;
    };
    struct Later {
        bool operator()(const Timer& a, const Timer& b) const { return a.at != b.at ? a.at > b.at : a.seq > b.seq; }
    };
    // A local task blocked in `await`/`join` on a record owned by another
    // isolate. The owning scheduler cannot wake this task id, so this isolate
    // polls the record after its shared ParallelWait is notified.
    struct ForeignWait {
        std::shared_ptr<ParallelTaskRecord> record;
        std::uint32_t task = 0;
    };

    std::vector<Task> tasks;  // tasks[0] is the main program
    std::vector<std::uint32_t> free_ids;
    std::deque<std::uint32_t> run_queue;
    std::priority_queue<Timer, std::vector<Timer>, Later> timers;
    std::uint32_t current = 0;
    std::uint32_t budget = kCoroSlice;
    std::uint64_t timer_seq = 0;
    std::size_t globals = 0;  // main's register count: the global window
    std::vector<std::vector<std::uint16_t>> snapshot;  // per proto: global slots its tasks can reach
    std::unordered_map<const Proto*, std::uint32_t> proto_index;
    Proto trampoline;         // Nop; TaskEnd (a task's bottom frame returns into TaskEnd)
    std::shared_ptr<WakeSource> wake = std::make_shared<WakeSource>();
    // CP23 parallel tasks (null under the CP22 cooperative path).
    std::shared_ptr<ParallelRuntime> parallel;
    std::shared_ptr<const Program> program_owner;
    std::shared_ptr<const TransferProtoMap> parallel_map;
    std::shared_ptr<ParallelWait> parallel_wait;
    std::vector<std::shared_ptr<ParallelTaskRecord>> parallel_live;
    std::vector<ForeignWait> foreign_waits;
    std::deque<std::shared_ptr<ParallelTaskRecord>> parallel_output;
    bool trace = false;
    // A fatal failure re-raised by `await` reports the task's position.
    bool pending_report = false;
    std::uint32_t report_offset = 0;
    // Statistics (tests, benchmarks).
    std::uint64_t spawned = 0, finished = 0, switches = 0, preemptions = 0, wakeups = 0;
    std::size_t live = 1, live_peak = 1;
    Value last_result;  // set only by mutant M5 (same layout in every build)

    // Bytes of a suspended task's storage (charged to its future).
    static std::size_t storage_bytes(const Task& task) noexcept;
    // Re-counts the storage charged for `task` (0 while it runs).
    static void charge(Task& task, bool suspended) noexcept;

    // ---- scheduling (vm_coro.cpp); all on the VM thread ----
    // Saves the running task's state (resume at `pc`) into its record.
    static void save(Vm& vm, const Proto* proto, std::size_t base, const Instr* pc);
    // Makes task `id` the running one and reloads the dispatch registers.
    static void load(Vm& vm, std::uint32_t id, Value*& R, const Value*& K, const Proto*& proto,
                     std::size_t& base, const Instr*& pc);
    // Switches to the next runnable task, waiting for timers when none is:
    // 1 switched, 2 the run has ended (main and every task finished), 0 fault.
    static int next(Vm& vm, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                    const Instr*& pc, Fault& fault);
    // Moves due timers to the run queue (wake order: deadline, then sequence).
    static void fire_due(Coro& coro, Clock::time_point now);
    // A task's body has ended (storage released exactly once); completes it
    // when it has no live children, and its ancestors in turn.
    static void end_body(Vm& vm, std::uint32_t id);
    static void finish(Vm& vm, std::uint32_t id);
    // Completes a task's future (Done/Failed) and moves its waiters to the run queue.
    static void publish(Coro& coro, Task& task);
    static void release(Task& task) noexcept;
    // Creates the task of `callee` with the call window R[in.a ..] and puts
    // its future in R[in.a]. `function` is the callee value (null for Spawn).
    static bool spawn(Vm& vm, const Instr& in, const Proto* callee, const Value& function, Value* R, Fault& fault,
                      bool thread = false);
    // CP23: starts `callee` on an OS worker instead of the run queue. The
    // FutureObject is created immediately and references the shared record.
    static bool spawn_parallel(Vm& vm, const Instr& in, const Proto* callee, Value* R, Fault& fault, bool thread);
    // Settlement of completed records: structured child counts, waiters, stats.
    void reap_parallel(Vm& vm);
    void reap_foreign(Vm& vm);
    void flush_parallel_outputs(Vm& vm);
    void resolve_parallel(Vm& vm, FutureObject* future, const std::shared_ptr<ParallelTaskRecord>& record);
    int await_parallel(Vm& vm, const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                       const Instr*& pc, Fault& fault, FutureObject* future, bool join);
    // Waits for any parallel completion after `observed` or the next due timer.
    void wait_parallel(Vm& vm, std::uint64_t observed);
    // Per-proto global slots a task of that proto can reach (build once).
    static void build_snapshot(Coro& coro, const std::vector<const Proto*>& protos);

    // ---- opcodes (vm_coro_ops.cpp, vm_coro_run.cpp): 0 fault, 1 dispatch, 2 run ended ----
    static int await(Vm& vm, const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                     const Instr*& pc, Fault& fault);
    static int sleep(Vm& vm, const Instr& in, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                     const Instr*& pc, Fault& fault);
    static int yield(Vm& vm, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base, const Instr*& pc,
                     Fault& fault);
    static int task_end(Vm& vm, Value*& R, const Value*& K, const Proto*& proto, std::size_t& base,
                        const Instr*& pc, Fault& fault);
};

}  // namespace nift::rt
