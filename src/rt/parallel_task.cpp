#include "rt/parallel_task.h"

#include "HostTrace.h"
#include "rt/error.h"
#include "rt/future.h"
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/shared_state_internal.h"
#include <chrono>
#include "rt/shared_state.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <system_error>
#include <thread>

namespace nift::rt {

void ParallelOutput::publish(std::string& text) {
    if (text.empty()) return;
    std::lock_guard<std::mutex> lock(mutex);
    pending += text;
    text.clear();
}
void ParallelOutput::drain(std::string& output) {
    std::lock_guard<std::mutex> lock(mutex);
    output += pending;
    pending.clear();
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

std::shared_ptr<ParallelRuntime> ParallelRuntime::from_environment() {
    std::size_t limit = std::thread::hardware_concurrency();
    limit = std::max<std::size_t>(1, std::min<std::size_t>(8, limit));
    const char* text = std::getenv("NIFT_VM_PARALLEL_WORKERS");
    if (text != nullptr && *text != '\0') {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(text, &end, 10);
        if (end != text && *end == '\0' && parsed > 0 && parsed <= 256)
            limit = static_cast<std::size_t>(parsed);
    }
    return std::make_shared<ParallelRuntime>(limit);
}

namespace { thread_local ParallelRuntime* running_pool = nullptr; }

ParallelRuntime::ParallelRuntime(std::size_t limit)
    : limit_(std::max<std::size_t>(1, std::min<std::size_t>(256, limit))),
      capacity_(std::max<std::size_t>(64, std::min<std::size_t>(1024, limit_ * 64))) {
    (void)shared_detail::registry(); // initialize allocating CV state before any noexcept teardown
    workers_.reserve(capacity_);
}
ParallelRuntime::~ParallelRuntime() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    // Submission is now closed, so records_ is stable. Do not allocate in
    // shutdown (including when construction or a task failed under OOM).
    for (auto& weak : records_) if (auto record = weak.lock()) {
        { std::lock_guard<std::mutex> lock(record->mutex);
          if (record->state == ParallelTaskState::Pending) record->cancelled = true; }
        notify_parallel_wait_states(record);
    }
    cv_.notify_all(); shared_detail::registry().cv.notify_all();
    for (auto& thread : workers_) if (thread.joinable()) thread.join();
    shared_collect();
}
void ParallelRuntime::ensure_worker_locked() {
    if (!stopping_ && !queue_.empty() && workers_.size() < capacity_ &&
        workers_.size() < std::min(limit_, active_) + blocked_)
        workers_.emplace_back([this] { worker_loop(); });
}
bool ParallelRuntime::submit(std::shared_ptr<ParallelTaskRecord> record, std::string& error) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (stopping_) { error = "parallel runtime is shutting down"; return false; }
    // Admission is bounded independently of CPU slots. Saturation is an
    // explicit refusal; blocking workers release CPU permits.
    if (active_ >= capacity_) { error = "parallel task admission limit exceeded"; return false; }
    queue_.push_back(record);
    bool admitted = false;
    try {
        records_.erase(std::remove_if(records_.begin(), records_.end(),
                       [](const auto& p) { return p.expired(); }), records_.end());
        records_.push_back(record); ++active_; admitted = true;
        ensure_worker_locked();
    } catch (...) {
        queue_.pop_back(); if (admitted) --active_; throw;
    }
    cv_.notify_all();
    return true;
}
void ParallelRuntime::worker_loop() {
    running_pool = this;
    for (;;) {
        std::shared_ptr<ParallelTaskRecord> record;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&] { return (stopping_ && queue_.empty()) ||
                                      (!queue_.empty() && computing_ < limit_ && resuming_ == 0); });
            if (stopping_ && queue_.empty()) break;
            record = std::move(queue_.front()); queue_.pop_front(); ++computing_;
        }
        try { run_parallel_task(record); }
        catch (...) {
            record->options.current_task.reset();
            { std::lock_guard<std::mutex> lock(record->mutex);
              record->state = ParallelTaskState::Failed;
              record->failure_message = "task failed";
              record->failure_code.clear(); record->failure_catchable = false;
              record->failure_reason = StopReason::Internal; }
            notify_parallel_wait_states(record); record->cv.notify_all();
        }
        record.reset();  // destroy neutral task storage before idle is visible
        shared_collect();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --computing_; --active_;
        }
        cv_.notify_all();
    }
    running_pool = nullptr;
}
void ParallelRuntime::block_worker() {
    std::lock_guard<std::mutex> lock(mutex_);
    --computing_; ++blocked_;
    try { ensure_worker_locked(); } catch (...) { ++computing_; --blocked_; throw; }
    cv_.notify_all();
}
void ParallelRuntime::resume_worker() {
    std::unique_lock<std::mutex> lock(mutex_);
    ++resuming_;
    cv_.wait(lock, [&] { return computing_ < limit_; });
    --resuming_; --blocked_; ++computing_;
}
ParallelBlock::ParallelBlock() : runtime_(running_pool) { if (runtime_) runtime_->block_worker(); }
ParallelBlock::~ParallelBlock() { if (runtime_) runtime_->resume_worker(); }
bool ParallelRuntime::try_acquire(std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ >= limit_) { error = "parallel worker limit exceeded"; return false; }
    ++active_; return true;
}
void ParallelRuntime::release() {
    { std::lock_guard<std::mutex> lock(mutex_); if (active_) --active_; }
    cv_.notify_all();
}
std::size_t ParallelRuntime::active() const { std::lock_guard<std::mutex> lock(mutex_); return active_; }
std::size_t ParallelRuntime::worker_count() const { std::lock_guard<std::mutex> lock(mutex_); return workers_.size(); }
bool ParallelRuntime::cancel_queued(const std::shared_ptr<ParallelTaskRecord>& record) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto at = std::find(queue_.begin(), queue_.end(), record);
        if (at == queue_.end()) return false;
        queue_.erase(at); --active_;
    }
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        record->cancelled = true; record->state = ParallelTaskState::Cancelled;
        record->failure_message = "cancelled"; record->failure_catchable = false;
    }
    notify_parallel_wait_states(record); record->cv.notify_all(); cv_.notify_all();
    return true;
}
void ParallelRuntime::wait_idle() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&] { return active_ == 0; });
}

namespace {

std::atomic<std::uint64_t> next_parallel_identity{1};

void notify_wait_state(const std::shared_ptr<ParallelWait>& state) {
    if (!state) return;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->generation;
    }
    state->cv.notify_all();
}

} // namespace

ParallelTaskRecord::~ParallelTaskRecord() {
    if (!shared_id) return;
    auto& r = shared_detail::registry();
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    if (shared_id) r.tasks.erase(shared_id);
}
std::uint64_t parallel_task_identity(const std::shared_ptr<ParallelTaskRecord>& record) {
    if (!record) return 0;
    auto& r = shared_detail::registry(); // may allocate; do not publish an id first
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        if (record->shared_id == 0) record->shared_id = next_parallel_identity.fetch_add(1);
        id = record->shared_id;
    }
    std::lock_guard<std::recursive_mutex> lock(r.mutex);
    r.tasks[id] = record;
    return id;
}

void add_parallel_wait_state(const std::shared_ptr<ParallelTaskRecord>& record,
                             const std::shared_ptr<ParallelWait>& state) {
    if (!record || !state) return;
    std::lock_guard<std::mutex> lock(record->mutex);
    for (const auto& existing : record->wait_states)
        if (existing.get() == state.get()) return;
    record->wait_states.push_back(state);
}

void notify_parallel_wait_states(const std::shared_ptr<ParallelTaskRecord>& record) {
    if (!record) return;
    std::lock_guard<std::mutex> lock(record->mutex);
    for (const auto& state : record->wait_states) notify_wait_state(state);
}

void run_parallel_task(std::shared_ptr<ParallelTaskRecord> record) {
    AtomTableScope symbols;
    nift::trace::ContextGuard guard(record->label);
    record->options.current_task = record;
    if (!record->options.parallel_wait) record->options.parallel_wait = record->wait_state;
    IsolateTaskOutcome outcome;
    if (!task_cancelled(record)) {
        try { outcome = execute_isolate_task(*record->program, record->input, record->options); }
        catch (const std::exception& e) {
            outcome.status = IsolateTaskStatus::ExecutionError;
            outcome.result.reason = StopReason::Internal;
            outcome.result.message = std::string("parallel task execution failed: ") + e.what();
        }
    } else {
        outcome.status = IsolateTaskStatus::ExecutionError;
        outcome.result.reason = StopReason::Error;
        outcome.result.message = "task cancelled";
        outcome.result.code = "runtime.cancelled";
    }
    if (record->output_root && record->options.parallel_output)
        record->options.parallel_output->drain(outcome.output);
    record->options.current_task.reset();  // the worker Vm is gone; no self-reference may survive
    bool cancelled_completion;
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        record->completion_started = true;
        // An observed script failure (including its original Error) or fatal
        // Internal failure wins over a concurrent cancellation request.
        cancelled_completion = outcome.result.reason != StopReason::Internal &&
            (outcome.result.code == "runtime.cancelled" ||  // rt-lint: allow-string-compare cancellation code
             (record->cancelled && outcome.status == IsolateTaskStatus::Ok &&
              outcome.result.reason == StopReason::None));
    }
    if (cancelled_completion && !outcome.has_error_value) {
        Value error;
        std::string why;
        if (make_system_error("runtime.cancelled", "task cancelled", SourceOrigin{}, error, why)) {
            std::string transfer_error;
            if (record->proto_map &&
                outcome.error_value.encode(error, *record->proto_map, transfer_error) == TransferStatus::Ok) {
                outcome.has_error_value = true;
            } else {
                outcome.error_value.clear();
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        const bool succeeded = outcome.status == IsolateTaskStatus::Ok &&
                               outcome.result.reason == StopReason::None && !cancelled_completion;
        if (succeeded) {
            record->state = ParallelTaskState::Done;
            record->result = std::move(outcome.return_value);
            record->has_result = outcome.has_return_value;
        } else if (cancelled_completion) {
            record->state = ParallelTaskState::Cancelled;
            record->failure_message = "task cancelled";
            record->failure_code = "runtime.cancelled";
            record->failure_catchable = true;
            if (outcome.has_error_value) {
                record->error_value = std::move(outcome.error_value);
                record->has_error_value = true;
            }
        } else {
            record->state = ParallelTaskState::Failed;
            record->failure_reason = outcome.result.reason == StopReason::Internal
                                         ? StopReason::Internal : StopReason::Error;
            record->failure_message =
                outcome.result.reason != StopReason::None ? outcome.result.message : outcome.error;
            record->failure_code = outcome.result.code;
            record->failure_offset = outcome.result.offset;
            record->failure_error_origin = outcome.result.error_origin;
            record->failure_in_loop_path = outcome.result.error_in_loop_path;
            record->failure_catchable = outcome.result.catchable && outcome.has_error_value;
            record->error_value = std::move(outcome.error_value);
            record->has_error_value = outcome.has_error_value;
        }
        record->output = std::move(outcome.output);
    }
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        record->failure_catchable = record->failure_catchable && record->has_error_value;
    }
    record->cv.notify_all();
    notify_parallel_wait_states(record);

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
            Value seeds = make_array();
            const auto g = record->program->main().nregs;
            for (std::size_t i = 0; i < g; ++i)
                array_push(seeds, i < record->input.globals.size() ? record->input.globals[i] : Value());
            for (const auto& v : record->input.args) array_push(seeds, v);
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
    try { return runtime->submit(record, error); }
    catch (const std::bad_alloc&) { throw; }
    catch (const std::exception& failure) {
        record->failure_reason = StopReason::Internal;
        error = std::string("cannot enqueue parallel task: ") + failure.what(); return false;
    }
}

} // namespace nift::rt
