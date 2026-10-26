#pragma once
// CP16 Host layer, POSIX internals: the state of one ProcessGroup (stages,
// descriptors, buffers, cancellation and cleanup phases, lifecycle seam),
// shared by the launch, event-loop, lifecycle and public-method files.
#ifndef _WIN32
#include "rt/host/process.h"
#include "rt/host/process_runtime.h"

#include <csignal>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sys/types.h>
#include <vector>

namespace nift::rt::host {

namespace posix {
constexpr std::size_t kChunk = 64 * 1024;

struct Buffer {
    std::string data;
    std::size_t pos = 0;  // consumed by read()/read_line()
    std::size_t retained() const { return data.size() - pos; }
    void compact() { if (pos > 0 && pos * 2 >= data.size()) { data.erase(0, pos); pos = 0; } }
};

struct StageRt {
    pid_t pid = -1;
    int pidfd = -1;
    bool reaped = false;
    bool exec_failed = false;
    StageResult result;
};
} // namespace posix

struct ProcessGroup::State {
    SpawnRequest req;
    std::vector<posix::StageRt> st;
    pid_t pgid = -1;
    bool detached = false;
    int out_fd = -1, err_fd = -1, in_fd = -1, out_file = -1, err_file = -1;
    posix::Buffer out, err;
    std::string in_pending;
    std::size_t in_pos = 0;
    bool stdin_closed = false;          // closed by the user, by cancellation or by EPIPE
    bool close_stdin_when_drained = false;
    bool waiting = false;               // wait() lifts the interactive soft cap
    std::optional<Clock::time_point> deadline;
    enum class Phase { None, Term, Kill, Done } phase = Phase::None;
    Clock::time_point phase_at;
    Millis kill_after{2000};
    CancelScope scope = CancelScope::Pipeline;
    std::size_t child_stage = 0;
    CancelReason reason = CancelReason::None;
    bool cancelled = false, timed_out = false, lost = false;
    bool limit_hit = false;
    std::string limit_stream;
    bool leftover_started = false;
    bool finished = false;
    bool launch_failed = false;
    std::string parent_error;
    bool handoff = false;
    bool pidfds_ok = true;
    // Cleanup of leftover group members after the stages are reaped (C1).
    bool cleanup_requested = false;
    Phase left_phase = Phase::None;
    Clock::time_point left_at;
    std::vector<long> left_members;
    bool leftover_failed = false;
    // stdin delivery accounting (ProcessResult::stdin_*)
    std::uint64_t stdin_total = 0;
    std::uint64_t stdin_written = 0;
    bool stdin_incomplete = false;
    std::string stdin_error;
    // Concurrency and the host loop.
    mutable std::mutex mu;
    std::weak_ptr<ProcessGroup> self;
    std::vector<runtime::TaskId> loop_tasks;
    // Lifecycle seam (process.h): events are collected under the lock and
    // delivered after it is released.
    std::shared_ptr<LifecycleSink> sink;
    bool term_notified = false;
    bool settle_notified = false;
    ProcessResult result;

    bool all_reaped() const {
        for (const auto& s : st) if (s.pid > 0 && !s.reaped) return false;
        return true;
    }
    std::vector<long> unreaped_pids() const {
        std::vector<long> out;
        for (const auto& s : st) if (s.pid > 0 && !s.reaped) out.push_back(s.pid);
        return out;
    }
};

namespace posix {
using State = ProcessGroup::State;
} // namespace posix

} // namespace nift::rt::host
#endif  // !_WIN32
