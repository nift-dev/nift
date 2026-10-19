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

bool LifecycleStore::settle(LifeRecord& record, CleanupOutcome&& outcome, CleanupFailure& failure, bool& failed) {
    failed = false;
    if (is_settled(record.state)) return false;
    switch (outcome.result) {
        case CleanupOutcome::Result::Completed: record.state = LifeState::Completed; return true;
        case CleanupOutcome::Result::Lost: record.state = LifeState::Lost; break;
        default: record.state = LifeState::Failed; break;
    }
    failed = true;
    const bool lost = record.state == LifeState::Lost;
    failure = CleanupFailure{record.handle, record.kind, record.owner, record.reason, record.cause, record.state,
                             outcome.code.empty() ? (lost ? "internal.handle_cleanup_lost"
                                                          : "internal.handle_cleanup_failed")
                                                  : std::move(outcome.code),
                             outcome.message.empty() ? (lost ? "termination could not be confirmed"
                                                             : "cleanup failed")
                                                     : std::move(outcome.message)};
    return true;
}

} // namespace nift::rt::handles
