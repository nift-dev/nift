#include "rt/parallel_task.h"

#include "HostTrace.h"
#include "rt/error.h"
#include "rt/future.h"
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/heap_account.h"
#include "rt/shared_state_internal.h"
#include <chrono>
#include "rt/shared_state.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>
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
    if (output.empty()) output.swap(pending);
    else output += pending;
    pending.clear();
}

std::shared_ptr<ParallelRuntime> ParallelRuntime::from_environment(std::size_t root_heap_budget) {
    std::size_t limit = std::thread::hardware_concurrency();
    limit = std::max<std::size_t>(1, std::min<std::size_t>(8, limit));
    const char* text = std::getenv("NIFT_VM_PARALLEL_WORKERS");
    if (text != nullptr && *text != '\0') {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(text, &end, 10);
        if (end != text && *end == '\0' && parsed > 0 && parsed <= 256)
            limit = static_cast<std::size_t>(parsed);
    }
    return std::make_shared<ParallelRuntime>(limit, kTaskCapacity, kBlockedCapacity, root_heap_budget);
}

namespace {
thread_local ParallelRuntime* running_pool = nullptr;
thread_local std::size_t timer_help_depth = 0;
constexpr std::size_t kTimerHelpDepth = 32;
}

ParallelRuntime::ParallelRuntime(std::size_t limit, std::size_t task_capacity, std::size_t blocking_capacity,
                                 std::size_t root_heap_budget)
    : blocking_capacity_(std::max<std::size_t>(1, std::min(kBlockedCapacity, blocking_capacity))),
      limit_(std::max<std::size_t>(1, std::min<std::size_t>(256, limit))),
      capacity_(std::max<std::size_t>(1, std::min(kTaskCapacity, task_capacity))),
      root_heap_budget_(root_heap_budget) {
    (void)shared_detail::registry(); // initialize allocating CV state before any noexcept teardown
    workers_.reserve(limit_ + blocking_capacity_);
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
    cv_.notify_all(); resume_cv_.notify_all(); shared_detail::notify_shared_cancellation();
    for (auto& thread : workers_) if (thread.joinable()) thread.join();
    shared_collect(); NIFT_CHECK_DRAINED(reserved_);
}
void ParallelRuntime::ensure_worker_locked() {
    if (!stopping_ && !queue_.empty() && workers_.size() < limit_ + blocking_capacity_ &&
        workers_.size() < std::min(limit_, active_) + native_blocked_ +
                           std::min(kTimerWorkerCapacity, transient_blocked_))
        workers_.emplace_back((NIFT_INJECT_BAD_ALLOC("nothread"), [this] { worker_loop(); }));
}
bool ParallelRuntime::submit(std::shared_ptr<ParallelTaskRecord> record, std::string& error) {
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
    // C141a (N1), test-only: with NIFT_TEST_SUBMIT_AFTER_LIMIT set, a spawn by the main program while a task is
    // active first waits (polled under mutex_, at most 10 s) until a limit has been recorded.
    if (!running_pool && std::getenv("NIFT_TEST_SUBMIT_AFTER_LIMIT"))
        wait_for_test([this] { std::lock_guard<std::mutex> held(mutex_); return active_ == 0 || limit_failed_; });
#endif
    std::unique_lock<std::mutex> lock(mutex_);
    if (stopping_) { error = "parallel runtime is shutting down"; return false; }
    if ((kNoteAdmits ? starved_ : limit_failed_) && !kStarveAdmits) return refuse_fatal(*record, error);  // C141a N1
    // Admission is bounded independently of CPU slots. Saturation is an
    // explicit refusal; blocking workers release CPU permits.
    if (active_ >= capacity_) { error = "parallel task admission limit exceeded"; return false; }
    // C140 (b): every unfinished isolate's charge counts against the root heap budget, awaited or not.
    const std::size_t charge = record->reserved_bytes;
    if (charge > root_heap_budget_ || reserved_ > root_heap_budget_ - charge) {
        record->failure_code = "limit.exceeded"; error = "parallel task memory budget exceeded"; return false;
    }
    queue_.push_back(record);
    bool admitted = false;
    try {
        records_.erase(std::remove_if(records_.begin(), records_.end(),
                       [](const auto& p) { return p.expired(); }), records_.end());
        records_.push_back(record); ++active_; admitted = true;
        NIFT_INJECT_BAD_ALLOC("submit");
        ensure_worker_locked();
    } catch (...) {
        queue_.pop_back(); if (admitted) --active_; throw;
    }
#if !(defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT == 1)  // test-only: no charge
    reserved_ += charge;
#endif
    cv_.notify_all();
    return true;
}
void ParallelRuntime::worker_loop() {
    running_pool = this;
    for (;;) {
        std::shared_ptr<ParallelTaskRecord> record;
        {
            std::unique_lock<std::mutex> lock(mutex_);
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
            ++parked_for_test_;  // test-only (C141a, S1): idle workers parked here (parallel_block.cpp's hook)
#endif
            cv_.wait(lock, [&] { return (stopping_ && queue_.empty()) ||
                                      (!queue_.empty() && computing_ < limit_ && resuming_ == 0); });
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
            --parked_for_test_;
#endif
            if (stopping_ && queue_.empty()) break;
            record = std::move(queue_.front()); queue_.pop_front(); ++computing_;
        }
        execute_record(std::move(record), true);
    }
    running_pool = nullptr;
}
void ParallelRuntime::execute_record(std::shared_ptr<ParallelTaskRecord> record, bool owns_cpu) {
    try { run_parallel_task(record); }
    catch (...) {
        record->options.current_task.reset();
        {
            std::lock_guard<std::mutex> lock(record->mutex);
            if (record->state == ParallelTaskState::Pending) {
                try { record->failure_message = "task failed"; }
                catch (const std::bad_alloc&) { record->failure_message.clear(); }
                record->failure_code.clear();
                record->failure_catchable = false;
                record->failure_reason = StopReason::Internal;
                record->state = ParallelTaskState::Failed;
            }
        }
        notify_parallel_wait_states(record); record->cv.notify_all();
    }
    const std::size_t charge = record->reserved_bytes;
    record.reset();
    shared_collect();
    { std::lock_guard<std::mutex> lock(mutex_); --active_; if (owns_cpu) --computing_;
      for (int n = 0; n < kCompletionReleases; ++n) release_locked(charge); }
    if (owns_cpu) resume_cv_.notify_one();
    cv_.notify_all();
}
bool ParallelRuntime::help_timer(std::chrono::steady_clock::time_point deadline) {
    auto* pool = running_pool;
    if (!pool || timer_help_depth == kTimerHelpDepth ||
        std::chrono::steady_clock::now() >= deadline) return false;
    std::shared_ptr<ParallelTaskRecord> record;
    {
        std::lock_guard<std::mutex> lock(pool->mutex_);
        // This OS thread already owns one CPU permit. A resumed thread gets
        // priority; do not consume a second permit for nested execution.
        if (pool->stopping_ || pool->queue_.empty() || pool->resuming_ ||
            !pool->queue_.front()->timer_help_safe) return false;
        // Nested VMs share this OS thread's heap accounting. Small immutable
        // inputs and fixed headroom prevent an otherwise valid isolate from
        // failing its own budget merely because it was selected as a helper.
        constexpr std::size_t headroom = 4u << 20;
        const auto budget = pool->queue_.front()->options.heap_budget;
        if (budget < headroom || live_heap_bytes() > budget - headroom) return false;
        record = std::move(pool->queue_.front()); pool->queue_.pop_front();
    }
    ++timer_help_depth;
    pool->execute_record(std::move(record), false);
    --timer_help_depth;
    return true;
}
ParallelBlock::ParallelBlock(ParallelWaitKind kind, std::uint32_t offset) : runtime_(running_pool), kind_(kind) {
    bool* starved = kind_ == ParallelWaitKind::Cleanup ? &exhausted_ : nullptr;
    try { if (runtime_ && !runtime_->block_worker(kind_, starved)) { admitted_ = false; runtime_ = nullptr; } }
    catch (const std::system_error&) {  // C140 (a): no worker thread for a blocking wait: refused like the capacity
        if (kThreadFailureInternal || kind_ != ParallelWaitKind::Blocking) throw;
        admitted_ = false; exhausted_ = true; runtime_ = nullptr;
    }
    if (starved && exhausted_ && runtime_) runtime_->starve(offset);  // C141: noexcept, allocation-free
}
ParallelBlock::~ParallelBlock() { if (runtime_) runtime_->resume_worker(kind_); }
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
std::size_t ParallelRuntime::executing() const { std::lock_guard<std::mutex> lock(mutex_); return computing_; }
std::size_t ParallelRuntime::blocked() const { std::lock_guard<std::mutex> lock(mutex_); return blocked_; }
std::size_t ParallelRuntime::worker_count() const { std::lock_guard<std::mutex> lock(mutex_); return workers_.size(); }
bool ParallelRuntime::cancel_queued(const std::shared_ptr<ParallelTaskRecord>& record) {
    std::string message;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto at = std::find(queue_.begin(), queue_.end(), record);
        if (at == queue_.end()) return false;
        message = "cancelled";  // finish fallible preparation before taking ownership from the queue
        queue_.erase(at); --active_;
#if !(defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT == 6)  // test-only: un-awaited cancel keeps it
        release_locked(record->reserved_bytes);
#endif
    }
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        record->cancelled = true;
        record->failure_message = std::move(message); record->failure_catchable = false;
        record->state = ParallelTaskState::Cancelled;
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
    for (const auto& observer : record->wait_observers) notify_wait_state(observer.state);
}

void run_parallel_task(std::shared_ptr<ParallelTaskRecord> record) {
    AtomTableScope symbols;
    nift::trace::ContextGuard guard(record->label);
    record->options.current_task = record;
    if (!record->options.parallel_wait) record->options.parallel_wait = record->wait_state;
    IsolateTaskOutcome outcome;
    bool worker_allocation_failed = false;
    if (!task_cancelled(record)) {
        try {
            NIFT_INJECT_BAD_ALLOC("snapshot");
            outcome = execute_isolate_task(*record->program, record->input, record->options);
        }
        catch (const std::bad_alloc&) {
            outcome.status = IsolateTaskStatus::ExecutionError;
            if (kAllocFailureInternal) {
                outcome.result.reason = StopReason::Internal;
                outcome.result.message = "parallel task execution failed: std::bad_alloc";
            } else {
                // Publish the fixed diagnostic without allocating text in the worker.
                worker_allocation_failed = true;
                outcome.result.reason = StopReason::Error;
                outcome.result.offset = record->spawn_offset;
            }
        }
        catch (const std::system_error&) {  // C140 (a): no worker thread for a timer or cleanup wait
            outcome.status = IsolateTaskStatus::ExecutionError; outcome.result.code = "limit.exceeded";
            outcome.result.reason = kThreadFailureInternal ? StopReason::Internal : StopReason::Error;
            outcome.result.message = kParallelResourcesExhausted; outcome.result.offset = record->spawn_offset;
        }
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
    note_parallel_limit(*record, outcome.result, worker_allocation_failed);
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
        ParallelTaskState terminal;
        if (succeeded) {
            terminal = ParallelTaskState::Done;
            std::swap(record->result, outcome.return_value);
            record->has_result = outcome.has_return_value;
        } else if (cancelled_completion) {
            terminal = ParallelTaskState::Cancelled;
            record->failure_message = "task cancelled";
            record->failure_code = "runtime.cancelled";
            record->failure_catchable = outcome.has_error_value;
            if (outcome.has_error_value) {
                std::swap(record->error_value, outcome.error_value);
                record->has_error_value = true;
            }
        } else {
            terminal = ParallelTaskState::Failed;
            record->failure_reason = outcome.result.reason == StopReason::Internal
                                         ? StopReason::Internal : StopReason::Error;
            record->failure_message =
                outcome.result.reason != StopReason::None ? outcome.result.message : outcome.error;
            record->failure_code = outcome.result.code;
            record->failure_resources_exhausted = worker_allocation_failed;
            record->failure_offset = outcome.result.offset;
            record->failure_error_origin = outcome.result.error_origin;
            record->failure_in_loop_path = outcome.result.error_in_loop_path;
            record->failure_catchable = outcome.result.catchable && outcome.has_error_value;
            std::swap(record->error_value, outcome.error_value);
            record->has_error_value = outcome.has_error_value;
        }
        record->output = std::move(outcome.output);
        // Publish only after the complete wire payload is available. Swapped
        // out graphs are destroyed with outcome, outside the task mutex.
        record->state = terminal;
    }
    record->cv.notify_all();
    notify_parallel_wait_states(record);

}

} // namespace nift::rt
