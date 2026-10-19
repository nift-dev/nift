#include "rt/host/process.h"
#include "rt/host/process_runtime.h"

namespace nift::rt::host {

const char* stage_state_name(StageState state) {
    switch (state) {
        case StageState::NotStarted: return "not_started";
        case StageState::LaunchFailed: return "launch_failed";
        case StageState::Running: return "running";
        case StageState::Exited: return "exited";
        case StageState::Signaled: return "signaled";
        case StageState::Lost: return "lost";
    }
    return "?";
}

// §11.10 `status` values.
const char* group_state_name(GroupState state) {
    switch (state) {
        case GroupState::Running: return "running";
        case GroupState::Exited: return "exited";
        case GroupState::LaunchFailed: return "launch_failed";
        case GroupState::Cancelled: return "cancelled";
        case GroupState::TimedOut: return "timed_out";
        case GroupState::LimitExceeded: return "limit_exceeded";
        case GroupState::Lost: return "lost";
        case GroupState::Detached: return "detached";
    }
    return "?";
}

HostStatus ProcessGroup::validate(const SpawnRequest& req, HostError& error) {
    const std::string& b = req.builtin;
    if (runtime::shutting_down())
        return make_error(error, "host.shutting_down", b + ": Nift is shutting down (signal received); no new process is started");
    if (req.stages.empty()) return make_error(error, "runtime.invalid_argument", b + ": empty pipeline");
    if (req.mode == OutputMode::Detached) {
        if (req.in_build)
            return make_error(error, "process.detach_in_build", b + ": a detached process is not allowed during a build (use a standalone script)");
        if (req.timeout) return make_error(error, "runtime.invalid_argument", b + ": deadline is not allowed for a detached process");
        if (req.stages.size() > 1) return make_error(error, "process.unsupported", b + ": a detached pipeline is not supported");
    }
    return HostStatus::Ok;
}

} // namespace nift::rt::host
