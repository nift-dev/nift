#include "rt/handles/table_contexts.h"

#include <algorithm>

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

void HandleTable::close_subtree(ContextId id, bool on_error, CloseReport& report,
                                std::unique_lock<std::mutex>& lock) {
    std::vector<ContextId> elsewhere;
    const std::uint64_t closer = next_closer_++;
    const std::vector<ContextId> order = contexts_.begin_close(id, closer, elsewhere);
    for (const ContextId each : order) {
        if (ExecContext* e = contexts_.find(each)) e->ending_on_error = e->ending_on_error || on_error;
    }
    // Descendants another close already owns end there (and are reported
    // there); ours end after them, descendants first.
    report.descendants_closed_elsewhere += elsewhere.size();
    idle_.wait(lock, [&] {
        for (const ContextId e : elsewhere) {
            if (contexts_.find(e)) return false;
        }
        return true;
    });
    // CP17 x CP18 (2026-10-27): if ending a context throws (allocation
    // failure), the contexts this close took over and has not ended go back
    // to their previous state, so a later close or shutdown() can end them
    // instead of waiting forever for this abandoned close.
    struct Restore {
        HandleTable& table;
        const std::vector<ContextId>& order;
        std::uint64_t closer;
        std::unique_lock<std::mutex>& lock;
        bool armed = true;
        ~Restore() {
            if (!armed) return;
            if (!lock.owns_lock()) lock.lock();
            for (const ContextId each : order) {
                ExecContext* c = table.contexts_.find(each);
                if (c && c->state == ContextState::Closing && c->closer == closer) {
                    c->state = c->before_close;
                    c->closer = 0;
                }
            }
            table.idle_.notify_all();
        }
    } restore{*this, order, closer, lock};
    for (const ContextId each : order) close_one(each, report, lock);
    restore.armed = false;
}

// CP17 x CP13B statement rollback (table.h).
std::size_t HandleTable::withdraw_abandoned_failures(ContextId owner, HandleKind kind, std::uint64_t since) {
    std::lock_guard<std::mutex> lock(mu_);
    if (owner == kDetached) return 0;
    ExecContext* c = contexts_.find(owner);
    if (!c) return 0;
    auto& failures = c->failures;
    const auto before = failures.size();
    const auto withdrawn = [&](const CleanupFailure& f) {
        return f.kind == kind && f.owner == owner && f.sequence > since &&
               f.reason == ReleaseReason::Abandoned && f.state == LifeState::CleanupIncomplete;
    };
    failures.erase(std::remove_if(failures.begin(), failures.end(), withdrawn), failures.end());
    return before - failures.size();
}

CloseReport HandleTable::close_context(ContextId id, bool on_error) {
    CloseReport report;
    if (in_own_hook()) { report.status = HandleStatus::ReentrantClose; return report; }
    std::unique_lock<std::mutex> lock(mu_);
    ExecContext* c = contexts_.find(id, report.status);
    if (!c) return report;
    report.label = c->label;
    report.on_error = on_error;
    if (c->state == ContextState::Closing) {  // another close owns it: wait until it is gone
        idle_.wait(lock, [&] { return contexts_.find(id) == nullptr; });
        report.status = HandleStatus::ContextClosed;
        report.closed_elsewhere = true;
        return report;
    }
    close_subtree(id, on_error, report, lock);
    return report;
}

CloseReport HandleTable::shutdown() {
    CloseReport report;
    if (in_own_hook()) { report.status = HandleStatus::ReentrantClose; return report; }
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
            close_subtree(newest, false, report, lock);
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
    append(report.detached_failures, detached_failures_);
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
    if (!accepts_handles(*c)) return HandleStatus::DetachFailed;  // the context is ending or cancelling
    if (c->build_family) return HandleStatus::DetachForbidden;
    if (!store_.slot(index).counted) return HandleStatus::NotCounted;  // its bookkeeping could never end
    LifeRecord& r = *records_.find(store_.slot(index).record);
    // Decided by what started termination, never by timing: a resource that
    // only exited (reported or not, reaped or not) detaches; one Nift is
    // terminating, or terminated, cannot be promised a lifetime.
    if (r.cause != TerminationCause::None && r.cause != TerminationCause::Exited) return HandleStatus::DetachFailed;
    store_.move(index, c->handles, detached_, kDetached);
    r.owner = kDetached;
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
        if (contexts_.may_access(accessor, owner) != HandleStatus::Ok) return HandleStatus::DetachFailed;
        // A build-family accessor may not detach even an ancestor's handle.
        const ExecContext* a = accessor == kAnyAccessor ? nullptr : contexts_.find(accessor);
        if (a && a->build_family) return HandleStatus::DetachForbidden;
    }
    // Ownership is re-checked under the lock: a concurrent change fails the detach.
    const HandleStatus status = detach(handle, kind, owner);
    return status == HandleStatus::NotOwned ? HandleStatus::DetachFailed : status;
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
