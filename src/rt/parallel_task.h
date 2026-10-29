#pragma once
// CP23: thread-safe completion records and the bounded worker admission for
// parallel `async`/`thread()` tasks. Records live outside isolate heaps; a
// parent FutureObject references one, while the worker owns only immutable
// compiled code and TransferGraph wire data.
#include "rt/isolate_task.h"
#include "rt/proto.h"
#include "rt/shared_gc.h"

#include <condition_variable>
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
    bool failure_error_origin = false;
    bool failure_in_loop_path = false;
    bool cancelled = false;
    // CP23 transfer identity: a process-unique id, never a heap/FutureObject
    // pointer. TransferGraph carries it so another isolate can reference the
    // same completion record.
    std::uint64_t shared_id = 0;
    std::string output;
    bool output_merged = false;
    bool output_root = false;
    std::uint32_t owner_task = 0;
    std::vector<std::uint32_t> waiters;  // parent-scheduler task ids blocked on it
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
    std::vector<std::shared_ptr<ParallelWait>> wait_states;

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
    static std::shared_ptr<ParallelRuntime> from_environment();
    explicit ParallelRuntime(std::size_t limit);
    ~ParallelRuntime();
    bool submit(std::shared_ptr<ParallelTaskRecord> record, std::string& error);
    // Compatibility admission hooks for direct embedding probes.
    bool try_acquire(std::string& error);
    void release();
    std::size_t limit() const noexcept { return limit_; }
    std::size_t active() const;
    std::size_t worker_count() const;
    void wait_idle();
    bool cancel_queued(const std::shared_ptr<ParallelTaskRecord>& record);
    void block_worker();
    void resume_worker();
private:
    void worker_loop();
    void ensure_worker_locked();
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::shared_ptr<ParallelTaskRecord>> queue_;
    std::vector<std::thread> workers_;
    std::vector<std::weak_ptr<ParallelTaskRecord>> records_;
    std::size_t active_ = 0, computing_ = 0, blocked_ = 0, resuming_ = 0;
    std::size_t limit_ = 8, capacity_ = 512;
    bool stopping_ = false;
};

// Blocking operations return their CPU permit while retaining their isolate's
// stack on its thread. Reusable compensating workers are bounded by admission.
class ParallelBlock {
public:
    ParallelBlock();
    ~ParallelBlock();
    ParallelBlock(const ParallelBlock&) = delete;
    ParallelBlock& operator=(const ParallelBlock&) = delete;
private:
    ParallelRuntime* runtime_ = nullptr;
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
// Snapshots source Values and queues a task. False with `error` when the
// limit is exhausted or the OS thread cannot be created.
bool start_parallel_task(std::shared_ptr<ParallelTaskRecord> record, std::string& error);

}  // namespace nift::rt
