#include "rt/handles/lifecycle.h"

namespace nift::rt::handles {

bool is_settled(LifeState state) {
    return state == LifeState::Completed || state == LifeState::Failed || state == LifeState::Lost;
}

TerminationCause cause_of(ReleaseReason reason) {
    switch (reason) {
        case ReleaseReason::Explicit: return TerminationCause::Close;
        case ReleaseReason::Abandoned: return TerminationCause::Abandoned;
        case ReleaseReason::ContextEnd: return TerminationCause::ContextEnd;
        case ReleaseReason::Shutdown: return TerminationCause::Shutdown;
    }
    return TerminationCause::None;
}

std::uint64_t LifecycleStore::add(HandleKind kind, const Value& handle, ContextId owner) {
    const std::uint64_t id = next_++;
    LifeRecord& r = records_[id];
    r.kind = kind;
    r.handle = handle;
    r.owner = owner;
    return id;
}

LifeRecord* LifecycleStore::find(std::uint64_t id) {
    const auto it = records_.find(id);
    return it == records_.end() ? nullptr : &it->second;
}

const LifeRecord* LifecycleStore::find(std::uint64_t id) const {
    const auto it = records_.find(id);
    return it == records_.end() ? nullptr : &it->second;
}

namespace {

// Lost always means "termination unconfirmed"; Failed never does.
FailureDetail normalized(const CleanupOutcome& outcome) {
    if (outcome.result == CleanupOutcome::Result::Lost) return FailureDetail::Unconfirmed;
    if (outcome.detail == FailureDetail::None || outcome.detail == FailureDetail::Unconfirmed) {
        return FailureDetail::Other;
    }
    return outcome.detail;
}

const char* default_code(FailureDetail detail) {
    switch (detail) {
        case FailureDetail::Unconfirmed: return "internal.handle_cleanup_lost";
        case FailureDetail::ExitedCleanupIncomplete: return "internal.handle_cleanup_incomplete";
        default: return "internal.handle_cleanup_failed";
    }
}

const char* default_message(FailureDetail detail) {
    switch (detail) {
        case FailureDetail::AliveAfterKill: return "still alive after forced termination";
        case FailureDetail::Unconfirmed: return "termination could not be confirmed";
        case FailureDetail::ExitedCleanupIncomplete: return "exited, but cleanup could not be completed";
        default: return "cleanup failed";
    }
}

} // namespace

bool LifecycleStore::settle(LifeRecord& record, CleanupOutcome&& outcome, CleanupFailure& failure, bool& failed) {
    failed = false;
    if (is_settled(record.state)) return false;
    switch (outcome.result) {
        case CleanupOutcome::Result::Completed: record.state = LifeState::Completed; return true;
        case CleanupOutcome::Result::Lost: record.state = LifeState::Lost; break;
        default: record.state = LifeState::Failed; break;
    }
    failed = true;
    const FailureDetail detail = normalized(outcome);
    failure = CleanupFailure{record.handle, record.kind, record.owner, record.reason, record.cause, record.state,
                             detail, {},
                             outcome.code.empty() ? default_code(detail) : std::move(outcome.code),
                             outcome.message.empty() ? default_message(detail) : std::move(outcome.message)};
    return true;
}

} // namespace nift::rt::handles
