#pragma once
// CP23: thread-safe completion records and the bounded worker admission for
// parallel `async`/`thread()` tasks. Records live outside isolate heaps; a
// parent FutureObject references one, while the worker owns only immutable
// compiled code and TransferGraph wire data.
#include "rt/isolate_task.h"
#include "rt/parallel_task_start.h"
#include "rt/parallel_wait_registration.h"
#include "rt/proto.h"
#include "rt/shared_gc.h"

#include <atomic>
#include <cstdlib>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <thread>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nift::rt {

enum class ParallelWaitKind : std::uint8_t { Blocking, Timer, Cleanup };

enum class ParallelTaskState : std::uint8_t { Pending, Done, Failed, Cancelled };

struct ParallelWait {
    mutable std::mutex mutex;
    std::condition_variable cv;
    std::uint64_t generation = 0;
    std::uint64_t snapshot_generation() const {
        std::lock_guard<std::mutex> lock(mutex);
        return generation;
    }
};

// Strings only: workers publish complete print chunks; the root drains them
// before observing a task result or printing. No isolate Value enters the sink.
struct ParallelOutput {
    std::mutex mutex;
    std::string pending;
    void publish(std::string& text);
    void drain(std::string& output);
};

struct ParallelTaskRecord {
    ~ParallelTaskRecord();
    SharedGcNode gc;
    mutable std::mutex mutex;
    std::condition_variable cv;
    // Written last under mutex on terminal publication. After this transition
    // the worker no longer touches the input or completion graphs or failure
    // payload. The shared collector clears graph edges only for unreachable
    // terminal records, under the registry mutex.
    ParallelTaskState state = ParallelTaskState::Pending;
    TransferGraph result;
    bool has_result = false;
    TransferGraph error_value;
    bool has_error_value = false;
    std::string failure_message;
    std::string failure_code;
    bool failure_catchable = false;
    StopReason failure_reason = StopReason::Error;
    std::uint32_t failure_offset = 0;
    bool failure_starved = false;  // C141: failed unrun by ParallelRuntime::starve (kParallelResourcesExhausted)
    // C140a: the spawning instruction in the parent's program (proto index, instruction index), for a position
    std::uint32_t spawn_proto = 0xffffffffu, spawn_instr = 0, spawn_offset = 0;
    bool failure_error_origin = false;
    bool failure_in_loop_path = false;
    bool cancelled = false;
    // Completion claims the cancellation decision under mutex before encoding.
    bool completion_started = false;
    bool timer_help_safe = false; // immutable after admission; finite nonblocking bytecode only
    std::size_t reserved_bytes = 0;  // C140 (b): bootstrap bytes + kIsolateOverhead, held from admission to completion
    // CP23 transfer identity: a process-unique id, never a heap/FutureObject
    // pointer. TransferGraph carries it so another isolate can reference the
    // same completion record.
    std::uint64_t shared_id = 0;
    std::string output;
    bool output_merged = false;
    bool output_root = false;
    std::uint32_t owner_task = 0;
    std::vector<ParallelTaskWaiter> waiters; // exact local await registrations
    std::string label;
    std::shared_ptr<const Program> program;
    std::shared_ptr<const TransferProtoMap> proto_map;
    IsolateTaskInput input;
    VmOptions options;
    std::weak_ptr<ParallelRuntime> runtime;
    std::shared_ptr<ParallelWait> wait_state;
    // Every scheduler wait state that has a task blocked in this record. A
    // future/thread handle may be awaited from another isolate, whose wait
    // state must be woken too.
    std::vector<std::shared_ptr<ParallelWait>> wait_states; // owner/explicit persistent notification targets
    std::vector<ParallelTaskObserver> wait_observers; // individually owned foreign awaits

    ParallelTaskState snapshot_state() const {
        std::lock_guard<std::mutex> lock(mutex);
        return state;
    }
    bool complete() const {
        std::lock_guard<std::mutex> lock(mutex);
        return state != ParallelTaskState::Pending;
    }
};

// Identity proto map for one (possibly cloned) Program: wire indices are the
// Program's own proto indices. Constructed once per isolate and read-only.
class ProgramTransferMap : public TransferProtoMap {
public:
    explicit ProgramTransferMap(const Program& program);
    std::uint64_t domain() const noexcept override { return domain_; }
    bool index_of(const Proto* proto, std::uint32_t& index) const override;
    const Proto* proto_of(std::uint32_t index) const override;

private:
    std::uint64_t domain_ = 0;
    std::vector<const Proto*> by_index_;
    std::unordered_map<const Proto*, std::uint32_t> by_pointer_;
};

// Reusable pool with bounded CPU permits and bounded compensating workers
// for blocked thread-affine VMs. Default min(8, hardware_concurrency);
// NIFT_VM_PARALLEL_WORKERS overrides CPU permits. Admission is bounded.
class ParallelRuntime : public std::enable_shared_from_this<ParallelRuntime> {
public:
    static std::shared_ptr<ParallelRuntime> from_environment(
        std::size_t root_heap_budget = VmOptions::kDefaultHeapBudget);
    // Neutral queued/unfinished records, independent of CPU topology. The
    // default covers CP22's 10,000-sleeper workload with bounded headroom.
    static constexpr std::size_t kTaskCapacity = 16384;
    static constexpr std::size_t kBlockedCapacity = 1024;
    static constexpr std::size_t kTimerWorkerCapacity = 64;
    // C140 (b): fixed per-isolate charge (VM state and worker stack touched; about 65 KB measured per nested level).
    static constexpr std::size_t kIsolateOverhead = std::size_t(64) << 10;
    explicit ParallelRuntime(std::size_t limit, std::size_t task_capacity = kTaskCapacity,
                             std::size_t blocking_capacity = kBlockedCapacity,
                             std::size_t root_heap_budget = VmOptions::kDefaultHeapBudget);
    ~ParallelRuntime();
    bool submit(std::shared_ptr<ParallelTaskRecord> record, std::string& error);
    // Compatibility admission hooks for direct embedding probes.
    bool try_acquire(std::string& error);
    void release();
    std::size_t limit() const noexcept { return limit_; }
    std::size_t task_capacity() const noexcept { return capacity_; }
    std::size_t blocking_capacity() const noexcept { return blocking_capacity_; }
    std::size_t active() const;
    std::size_t executing() const;
    std::size_t blocked() const;
    std::size_t worker_count() const;
    void wait_idle();
    bool cancel_queued(const std::shared_ptr<ParallelTaskRecord>& record);
    // `exhausted` (C141, cleanup waits): when no compensating worker can be started, keep the block, set it, no throw.
    bool block_worker(ParallelWaitKind kind = ParallelWaitKind::Blocking, bool* exhausted = nullptr);
    void resume_worker(ParallelWaitKind kind = ParallelWaitKind::Blocking);
    // Only used at a timer suspension: bounded nested dispatch retains every
    // isolate on its owning OS thread and never adds a CPU execution permit.
    static bool help_timer(std::chrono::steady_clock::time_point deadline);
    // C140: the first budget or thread-creation failure of any task, awaited or not; the run ends with it (exit 1).
    // `reason` (static: the budget or the resources text) is the fatal limit (C141, D-085); waiters are woken.
    void note_limit(const std::string& message, std::uint32_t offset, const char* reason);
    bool take_limit(std::string& message, std::uint32_t& offset) {
        std::lock_guard<std::mutex> lock(mutex_);
        message = limit_message_.empty() && fatal_limit() ? fatal_limit() : limit_message_.c_str();
        offset = limit_offset_; return limit_failed_;
    }
    // C141 (D-085): this runtime's first fatal limit (set once, with the first recorded limit), else null. From then
    // on every shared wait (send, recv, lock) that would block ends the run with it (shared_detail::wait_shared).
    const char* fatal_limit() const noexcept { return fatal_.load(std::memory_order_acquire); }
    // C141 (R1, D-083): a cleanup wait could not start a worker. Every queued task fails unrun (limit.exceeded, its
    // waiters woken, its charge released once), new tasks are refused from now on, and the limit is recorded once.
    // noexcept and allocation-free: it runs in Vm::coro_abort_parallel (parallel_starve.cpp). Phase 2 of the
    // starve: phase 1 (exhaust_locked) already refused new work and emptied the queue in block_worker's lock hold.
    void starve(std::uint32_t offset) noexcept;
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
    // C141a (M17), test-only: whether a worker is waiting to resume (vm_parallel_cleanup.cpp's hook).
    bool resuming_for_test() const { std::lock_guard<std::mutex> lock(mutex_); return resuming_ != 0; }
    // C141a (T1, M14), test-only: whether a finishing task may publish its completion and release its permit now:
    // no work is queued (nothing could take the permit), or a worker is waiting to resume (it takes the permit first).
    bool completion_may_proceed_for_test() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty() || resuming_ != 0;
    }
#endif
private:
    void worker_loop();
    void execute_record(std::shared_ptr<ParallelTaskRecord> record, bool owns_cpu);
    void ensure_worker_locked();
    // C141a (N1, D-087): after any recorded fatal limit (the C140 (d) note or the starve; under mutex_, where both are
    // recorded) no task is admitted: refused with the first recorded reason. Test-only M21: refused once starved only.
    bool refuse_fatal(ParallelTaskRecord& record, std::string& error) {
        const char* reason = fatal_.load(std::memory_order_relaxed);  // set with limit_failed_, under mutex_
        record.failure_code = "limit.exceeded"; error = reason ? reason : kParallelResourcesExhausted;
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
        refusals_for_test_.fetch_add(1);  // test-only: the phase-2 observer hook (parallel_starve.cpp)
#endif
        return false;
    }
    // C141 (W1): phase 1 of the starve, in block_worker's mutex_ hold that released the cleanup wait's permit, so no
    // queued task can start and no new one be admitted after the failure: sets `exhausted`, then sweep_locked().
    void exhaust_locked(bool& exhausted) noexcept;
    // Under mutex_, once per runtime: the failure (starved_, and the limit unless one was recorded first: submit
    // refuses from here on), the queue swapped into spare_, each swept task's admission and charge released once; the
    // calling thread becomes starver_, whose phase 2 records the offset and marks the swept records failed.
    void sweep_locked() noexcept;
    void release_locked(std::size_t charge) noexcept {  // C140a: an over-release is our defect: fail loudly
        if (charge > reserved_) std::abort();
        reserved_ -= charge;
    }
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable resume_cv_;
    std::deque<std::shared_ptr<ParallelTaskRecord>> queue_;
    std::vector<std::thread> workers_;
    std::vector<std::weak_ptr<ParallelTaskRecord>> records_;
    std::size_t active_ = 0, computing_ = 0, blocked_ = 0, resuming_ = 0;
    std::size_t native_blocked_ = 0, transient_blocked_ = 0;
    std::size_t reserved_ = 0;  // C140 (b): runtime-wide sum of admitted records' reserved_bytes
    bool limit_failed_ = false, starved_ = false;
    std::atomic<const char*> fatal_{nullptr};  // C141: written under mutex_ with limit_failed_, read lock-free
    std::string limit_message_;
    // C141: built up front, so the one sweep swaps, never allocates; written only by sweep_locked (once, under
    // mutex_), then read and cleared only by the starve that claims swept_pending_ (under mutex_).
    std::deque<std::shared_ptr<ParallelTaskRecord>> spare_;
    bool swept_pending_ = false, offset_pending_ = false;
    std::thread::id starver_;  // C141a: the thread whose phase 1 recorded the limit and swept; it alone runs phase 2's
                               // part for them (the offset, the swept records)
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
    std::atomic<int> refusals_for_test_{0};
    std::size_t parked_for_test_ = 0;  // under mutex_: worker threads waiting in worker_loop's cv_.wait
#endif
    std::uint32_t limit_offset_ = 0;
    std::size_t blocking_capacity_ = kBlockedCapacity;
    std::size_t limit_ = 8, capacity_ = kTaskCapacity;
    const std::size_t root_heap_budget_;
    bool stopping_ = false;
};

// Blocking operations return their CPU permit while retaining their isolate's
// stack on its thread. Reusable compensating workers are bounded by admission.
class ParallelBlock {
public:
    // `offset`: the source position a cleanup wait reports if it starves the runtime (C141).
    explicit ParallelBlock(ParallelWaitKind kind = ParallelWaitKind::Blocking, std::uint32_t offset = 0);
    ~ParallelBlock();
    bool admitted() const noexcept { return admitted_; }
    // The refusal when not admitted: the capacity, or (C140 (a)) no worker thread could be created.
    const char* refusal() const noexcept {
        return exhausted_ ? kParallelResourcesExhausted : "parallel blocking wait limit exceeded";
    }
    ParallelBlock(const ParallelBlock&) = delete;
    ParallelBlock& operator=(const ParallelBlock&) = delete;
private:
    ParallelRuntime* runtime_ = nullptr;
    ParallelWaitKind kind_;
    bool admitted_ = true, exhausted_ = false;
};

// Stable transfer identity of a record. Assigns one on first use under the
// record's mutex; zero only for a null record.
std::uint64_t parallel_task_identity(const std::shared_ptr<ParallelTaskRecord>& record);
// Registers `state` so completion/cancellation of `record` wakes it.
void add_parallel_wait_state(const std::shared_ptr<ParallelTaskRecord>& record,
                             const std::shared_ptr<ParallelWait>& state);
// Wakes every registered scheduler wait state (completion or cancellation).
void notify_parallel_wait_states(const std::shared_ptr<ParallelTaskRecord>& record);

// Runs one record's isolate task on the calling worker thread, publishes its
// wire result/error/output, wakes waiters and releases the admission lane.
void run_parallel_task(std::shared_ptr<ParallelTaskRecord> record);

}  // namespace nift::rt
