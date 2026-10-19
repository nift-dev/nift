#include "rt/handles/types.h"

namespace nift::rt::handles {

const char* release_reason_name(ReleaseReason reason) {
    switch (reason) {
        case ReleaseReason::Explicit: return "explicit";
        case ReleaseReason::Abandoned: return "abandoned";
        case ReleaseReason::ContextEnd: return "context-end";
        case ReleaseReason::Shutdown: return "shutdown";
    }
    return "unknown";
}

const char* failure_detail_name(FailureDetail detail) {
    switch (detail) {
        case FailureDetail::None: return "none";
        case FailureDetail::AliveAfterKill: return "alive-after-kill";
        case FailureDetail::Unconfirmed: return "termination-unconfirmed";
        case FailureDetail::ExitedCleanupIncomplete: return "exited-cleanup-incomplete";
        case FailureDetail::Other: return "other";
    }
    return "unknown";
}

const char* life_state_name(LifeState state) {
    switch (state) {
        case LifeState::Active: return "active";
        case LifeState::Terminating: return "terminating";
        case LifeState::Completed: return "completed";
        case LifeState::Failed: return "failed";
        case LifeState::Lost: return "lost";
    }
    return "unknown";
}

const char* termination_cause_name(TerminationCause cause) {
    switch (cause) {
        case TerminationCause::None: return "none";
        case TerminationCause::Exited: return "exited";
        case TerminationCause::Deadline: return "deadline";
        case TerminationCause::Cancel: return "cancel";
        case TerminationCause::Close: return "close";
        case TerminationCause::Abandoned: return "abandoned";
        case TerminationCause::ContextEnd: return "context-end";
        case TerminationCause::Shutdown: return "shutdown";
    }
    return "unknown";
}

} // namespace nift::rt::handles
