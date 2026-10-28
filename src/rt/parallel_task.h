#pragma once
// CP23: thread-safe completion records and the bounded worker admission for
// parallel `async`/`thread()` tasks. Records live outside isolate heaps; a
// parent FutureObject references one, while the worker owns only immutable
// compiled code and TransferGraph wire data.
#include "rt/isolate_task.h"
#include "rt/proto.h"

#include <condition_variable>
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

struct ParallelTaskRecord {
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
    std::uint32_t failure_offset = 0;
    bool failure_error_origin = false;
    bool failure_in_loop_path = false;
    bool cancelled = false;
    std::string output;
    bool output_merged = false;
    std::uint32_t owner_task = 0;
    std::vector<std::uint32_t> waiters;  // parent-scheduler task ids blocked on it
    std::string label;
    std::shared_ptr<const Program> program;
    std::shared_ptr<const TransferProtoMap> proto_map;
    IsolateTaskInput input;
    VmOptions options;
    std::shared_ptr<ParallelRuntime> runtime;
    std::shared_ptr<ParallelWait> wait_state;

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
    bool index_of(const Proto* proto, std::uint32_t& index) const override;
    const Proto* proto_of(std::uint32_t index) const override;

private:
    std::vector<const Proto*> by_index_;
    std::unordered_map<const Proto*, std::uint32_t> by_pointer_;
};

// Bounded thread-per-task admission. One lane is one running OS worker; the
// default is max(8, hardware_concurrency), overridable with
// NIFT_VM_PARALLEL_WORKERS. A full runtime refuses new tasks cleanly.
class ParallelRuntime {
public:
    static std::shared_ptr<ParallelRuntime> from_environment();
    explicit ParallelRuntime(std::size_t limit) : limit_(limit ? limit : 1) {}
    bool try_acquire(std::string& error);
    void release();
    std::size_t limit() const noexcept { return limit_; }
    std::size_t active() const;

private:
    mutable std::mutex mutex_;
    std::size_t active_ = 0;
    std::size_t limit_ = 8;
};

// Runs one record's isolate task on the calling worker thread, publishes its
// wire result/error/output, wakes waiters and releases the admission lane.
void run_parallel_task(std::shared_ptr<ParallelTaskRecord> record);
// Acquires a lane and starts the detached worker. False with `error` when the
// limit is exhausted or the OS thread cannot be created.
bool start_parallel_task(std::shared_ptr<ParallelTaskRecord> record, std::string& error);

}  // namespace nift::rt
