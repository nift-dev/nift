#include "rt/handles/table_contexts.h"

namespace nift::rt::handles {

namespace {

void append(std::vector<CleanupFailure>& to, std::vector<CleanupFailure>& from) {
    for (CleanupFailure& f : from) to.push_back(std::move(f));
    from.clear();
}

bool accepts_handles(const ExecContext& c) { return c.state == ContextState::Open; }

} // namespace

void HandleTable::close_one(ContextId id, CloseReport& report, std::unique_lock<std::mutex>& lock) {
    ExecContext* c = contexts_.find(id);
    while (c && c->handles.tail != kNil) {
        Started started = begin_cleanup_locked(c->handles.tail, ReleaseReason::ContextEnd);  // newest first
        run_cleanup(started, lock, nullptr);
        ++report.cleaned;
        c = contexts_.find(id);
    }
    // Hooks other threads started finish, and every obligation of this
    // context settles (CP16 bounds the wait), before the context is gone: the
    // report is final (D-035).
    idle_.wait(lock, [&] {
        c = contexts_.find(id);
        return !c || (c->in_flight == 0 && c->obligations == 0);
    });
    if (!c) return;  // ended by a concurrent close
    append(report.failures, c->failures);
    contexts_.erase(id);
    ++report.contexts;
    idle_.notify_all();
}

CloseReport HandleTable::close_context(ContextId id) {
    CloseReport report;
    std::unique_lock<std::mutex> lock(mu_);
    ExecContext* c = contexts_.find(id, report.status);
    if (!c) return report;
    if (c->state == ContextState::Closing) { report.status = HandleStatus::ContextClosed; return report; }
    for (const ContextId each : contexts_.begin_close(id)) close_one(each, report, lock);
    return report;
}

CloseReport HandleTable::shutdown() {
    CloseReport report;
    std::unique_lock<std::mutex> lock(mu_);
    // Repeats until nothing is left: a hook may open contexts or create handles.
    for (;;) {
        const std::vector<ContextId> roots = contexts_.roots();
        ContextId newest = kDetached;
        for (auto it = roots.rbegin(); it != roots.rend() && newest == kDetached; ++it) {
            const ExecContext* c = contexts_.find(*it);
            if (c && c->state != ContextState::Closing) newest = *it;
        }
        if (newest != kDetached) {
            for (const ContextId each : contexts_.begin_close(newest)) close_one(each, report, lock);
            continue;
        }
        if (!roots.empty()) { idle_.wait(lock); continue; }  // closes running on other threads
        if (detached_.tail != kNil) {
            Started started = begin_cleanup_locked(detached_.tail, ReleaseReason::Shutdown);
            run_cleanup(started, lock, nullptr);
            ++report.cleaned;
            continue;
        }
        if (detached_in_flight_ == 0 && detached_obligations_ == 0) break;
        idle_.wait(lock);
    }
    append(report.failures, detached_failures_);
    return report;
}

HandleStatus HandleTable::detach(const Value& handle, HandleKind kind, ContextId expected_owner) {
    std::lock_guard<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    HandleStatus status = store_.classify(handle, kind, index);
    if (status != HandleStatus::Ok) return status;
    if (expected_owner == kDetached || store_.slot(index).owner != expected_owner) return HandleStatus::NotOwned;
    ExecContext* c = contexts_.find(expected_owner, status);
    if (!c) return status;
    if (!accepts_handles(*c)) return HandleStatus::ContextClosed;
    if (c->build_family) return HandleStatus::DetachForbidden;
    store_.move(index, c->handles, detached_, kDetached);
    records_.find(store_.slot(index).record)->owner = kDetached;
    return HandleStatus::Ok;
}

HandleStatus HandleTable::detach_as(const Value& handle, HandleKind kind, ContextId accessor) {
    ContextId owner = kDetached;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::uint32_t index = 0;
        HandleStatus status = store_.classify(handle, kind, index);
        if (status != HandleStatus::Ok) return status;
        owner = store_.slot(index).owner;
        if (owner == kDetached) return HandleStatus::Ok;  // already detached: nothing to give up
        status = contexts_.may_access(accessor, owner);
        if (status != HandleStatus::Ok) return status;
        // A build-family accessor may not detach even an ancestor's handle.
        const ExecContext* a = accessor == kAnyAccessor ? nullptr : contexts_.find(accessor);
        if (a && a->build_family) return HandleStatus::DetachForbidden;
    }
    // Ownership is re-checked under the lock: a concurrent change makes this NotOwned.
    return detach(handle, kind, owner);
}

HandleStatus HandleTable::transfer(const Value& handle, HandleKind kind, ContextId expected_owner,
                                   ContextId new_owner, TransferPolicy policy) {
    std::unique_lock<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    HandleStatus status = store_.classify(handle, kind, index);
    if (status != HandleStatus::Ok) return status;
    if (store_.slot(index).owner != expected_owner) return HandleStatus::NotOwned;
    if (expected_owner == kDetached || new_owner == kDetached) return HandleStatus::TransferForbidden;
    ExecContext* from = contexts_.find(expected_owner, status);
    if (!from) return status;
    if (from->state == ContextState::Closing) return HandleStatus::ContextClosed;
    ExecContext* to = contexts_.find(new_owner, status);
    if (to && !accepts_handles(*to)) status = HandleStatus::ContextClosed;
    if (status != HandleStatus::Ok) {  // destination ended or cancelling
        if (policy == TransferPolicy::ReclaimOnRefusal) {
            Started started = begin_cleanup_locked(index, ReleaseReason::ContextEnd);
            run_cleanup(started, lock, nullptr);
        }
        return status;
    }
    if (from->build_family && !to->build_family) return HandleStatus::TransferForbidden;
    if (new_owner != expected_owner) {
        store_.move(index, from->handles, to->handles, new_owner);
        records_.find(store_.slot(index).record)->owner = new_owner;
    }
    return HandleStatus::Ok;
}

} // namespace nift::rt::handles
