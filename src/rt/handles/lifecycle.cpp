#include "rt/handles/lifecycle.h"

namespace nift::rt::handles {

bool is_settled(LifeState state) {
    return state == LifeState::Completed || state == LifeState::Failed || state == LifeState::Lost ||
           state == LifeState::CleanupIncomplete;
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
    std::uint64_t id;
    if (!free_.empty()) {
        id = free_.back();
        free_.pop_back();
    } else {
        records_.emplace_back();
        id = records_.size();
    }
    LifeRecord& r = records_[id - 1];
    r = LifeRecord();
    r.kind = kind;
    r.handle = handle;
    r.owner = owner;
    r.in_use = true;
    ++live_;
    return id;
}

LifeRecord* LifecycleStore::find(std::uint64_t id) {
    if (id == 0 || id > records_.size() || !records_[id - 1].in_use) return nullptr;
    return &records_[id - 1];
}

const LifeRecord* LifecycleStore::find(std::uint64_t id) const {
    if (id == 0 || id > records_.size() || !records_[id - 1].in_use) return nullptr;
    return &records_[id - 1];
}

void LifecycleStore::erase(std::uint64_t id) {
    LifeRecord* r = find(id);
    if (!r) return;
    r->in_use = false;
    r->handle = Value();
    free_.push_back(id);
    --live_;
}

LifeState settled_state(CleanupOutcome::Result result) {
    switch (result) {
        case CleanupOutcome::Result::Completed: return LifeState::Completed;
        case CleanupOutcome::Result::Failed: return LifeState::Failed;
        case CleanupOutcome::Result::CleanupIncomplete: return LifeState::CleanupIncomplete;
        default: return LifeState::Lost;  // Lost, and Pending reported as final (unconfirmed)
    }
}

namespace {

const char* default_code(LifeState state) {
    switch (state) {
        case LifeState::Failed: return "internal.handle_cleanup_failed";
        case LifeState::CleanupIncomplete: return "internal.handle_cleanup_incomplete";
        default: return "internal.handle_cleanup_lost";
    }
}

const char* default_message(LifeState state) {
    switch (state) {
        case LifeState::Failed: return "still alive after forced termination";
        case LifeState::CleanupIncomplete: return "ended, but cleanup could not be completed";
        default: return "termination could not be confirmed";
    }
}

} // namespace

bool LifecycleStore::settle(LifeRecord& record, CleanupOutcome&& outcome, CleanupFailure& failure, bool& failed) {
    failed = false;
    if (is_settled(record.state)) return false;
    record.state = settled_state(outcome.result);
    if (record.state == LifeState::Completed) return true;
    failed = true;
    failure = CleanupFailure{record.handle, record.kind, record.owner, record.reason, record.cause, record.state, {},
                             outcome.code.empty() ? default_code(record.state) : std::move(outcome.code),
                             outcome.message.empty() ? default_message(record.state) : std::move(outcome.message)};
    return true;
}

} // namespace nift::rt::handles
