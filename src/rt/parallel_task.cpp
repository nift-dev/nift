#include "rt/parallel_task.h"

#include "HostTrace.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>
#include <thread>

namespace nift::rt {

ProgramTransferMap::ProgramTransferMap(const Program& program) {
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
    if (limit < 8) limit = 8;
    const char* text = std::getenv("NIFT_VM_PARALLEL_WORKERS");
    if (text != nullptr && *text != '\0') {
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(text, &end, 10);
        if (end != text && *end == '\0' && parsed > 0 && parsed <= (1ul << 20))
            limit = static_cast<std::size_t>(parsed);
    }
    return std::make_shared<ParallelRuntime>(limit);
}

bool ParallelRuntime::try_acquire(std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ >= limit_) {
        error = "parallel worker limit exceeded (" + std::to_string(limit_) + " busy)";
        return false;
    }
    ++active_;
    return true;
}

void ParallelRuntime::release() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ != 0) --active_;
}

std::size_t ParallelRuntime::active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_;
}

namespace {

void notify_wait_state(const std::shared_ptr<ParallelWait>& state) {
    if (!state) return;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->generation;
    }
    state->cv.notify_all();
}

} // namespace

void run_parallel_task(std::shared_ptr<ParallelTaskRecord> record) {
    nift::trace::ContextGuard guard(record->label);
    IsolateTaskOutcome outcome = execute_isolate_task(*record->program, record->input, record->options);
    {
        std::lock_guard<std::mutex> lock(record->mutex);
        if (outcome.status == IsolateTaskStatus::Ok && outcome.result.reason == StopReason::None) {
            record->state = ParallelTaskState::Done;
            record->result = std::move(outcome.return_value);
            record->has_result = outcome.has_return_value;
        } else {
            record->state = ParallelTaskState::Failed;
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
    record->cv.notify_all();
    notify_wait_state(record->wait_state);
    if (record->runtime) record->runtime->release();
}

bool start_parallel_task(std::shared_ptr<ParallelTaskRecord> record, std::string& error) {
    if (!record->runtime) {
        error = "parallel runtime is not available";
        return false;
    }
    if (!record->runtime->try_acquire(error)) return false;
    try {
        std::thread(run_parallel_task, std::move(record)).detach();
    } catch (const std::system_error& failure) {
        record->runtime->release();
        error = std::string("cannot start parallel worker: ") + failure.what();
        return false;
    }
    return true;
}

}  // namespace nift::rt
