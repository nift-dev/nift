#include "rt/handles/table.h"

#include <atomic>
#include <exception>

namespace nift::rt::handles {

namespace {

std::uint32_t next_table_tag() {
    static std::atomic<std::uint32_t> next{1};
    std::uint32_t tag = next.fetch_add(1, std::memory_order_relaxed);
    if (tag == 0) tag = next.fetch_add(1, std::memory_order_relaxed);  // 0 is never a table tag
    return tag;
}

void append(std::vector<CleanupFailure>& to, std::vector<CleanupFailure>& from) {
    for (CleanupFailure& f : from) to.push_back(std::move(f));
    from.clear();
}

} // namespace

HandleTable::HandleTable(TableOptions options) : store_(next_table_tag(), options.capacity) {}

HandleTable::~HandleTable() { shutdown(); }

HandleStatus HandleTable::register_kind(HandleKind kind, KindSpec spec) {
    if (kind == HandleKind::None) return HandleStatus::UnknownKind;
    std::lock_guard<std::mutex> lock(mu_);
    KindEntry& entry = kinds_[static_cast<std::uint8_t>(kind)];
    if (entry.registered) return HandleStatus::KindRegistered;
    entry.spec = std::move(spec);
    entry.registered = true;
    return HandleStatus::Ok;
}

const KindSpec* HandleTable::kind_spec(HandleKind kind) const {
    std::lock_guard<std::mutex> lock(mu_);
    const KindEntry& entry = kinds_[static_cast<std::uint8_t>(kind)];
    return entry.registered ? &entry.spec : nullptr;
}

HandleStatus HandleTable::open_context(ContextKind kind, ContextId parent, ContextId& out) {
    std::lock_guard<std::mutex> lock(mu_);
    return contexts_.open(kind, parent, out);
}

HandleStatus HandleTable::create(ContextId owner, HandleKind kind, std::shared_ptr<void> resource, Value& out,
                                 CreateOptions options) {
    std::unique_lock<std::mutex> lock(mu_);
    HandleStatus status = HandleStatus::Ok;
    if (!kinds_[static_cast<std::uint8_t>(kind)].registered) status = HandleStatus::UnknownKind;
    else if (!resource) status = HandleStatus::NullResource;
    ExecContext* c = status == HandleStatus::Ok ? contexts_.find(owner, status) : nullptr;
    if (c && c->state != ContextState::Open) status = HandleStatus::ContextClosed;
    if (status == HandleStatus::Ok) status = store_.allocate(kind, resource, owner, options.counted, c->handles, out);
    if (status == HandleStatus::Ok) ++created_;
    lock.unlock();  // a refused resource is destroyed without the lock
    return status;
}

HandleStatus HandleTable::resolve(const Value& handle, HandleKind kind, ContextId accessor,
                                  std::shared_ptr<void>& out) const {
    return resolve_checked(handle, kind, accessor, nullptr, out);
}

HandleStatus HandleTable::resolve_checked(const Value& handle, HandleKind kind, ContextId accessor,
                                          const void* type_tag, std::shared_ptr<void>& out) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    HandleStatus status = store_.classify(handle, kind, index);
    if (status != HandleStatus::Ok) return status;
    const Slot& s = store_.slot(index);
    status = contexts_.may_access(accessor, s.owner);
    if (status != HandleStatus::Ok) return status;
    const KindSpec& spec = kinds_[static_cast<std::uint8_t>(kind)].spec;
    if (type_tag && spec.type_tag != type_tag) return HandleStatus::TypeMismatch;
    out = s.resource;
    return HandleStatus::Ok;
}

HandleStatus HandleTable::owner_of(const Value& handle, HandleKind kind, ContextId& owner) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    const HandleStatus status = store_.classify(handle, kind, index);
    if (status == HandleStatus::Ok) owner = store_.slot(index).owner;
    return status;
}

OwnerList* HandleTable::list_of(ContextId owner) {
    if (owner == kDetached) return &detached_;
    ExecContext* c = contexts_.find(owner);
    return c ? &c->handles : nullptr;
}

Taken HandleTable::take_locked(std::uint32_t index) {
    const ContextId owner = store_.slot(index).owner;
    Taken taken = store_.take(index, *list_of(owner));
    if (owner == kDetached) ++detached_in_flight_;
    else ++contexts_.find(owner)->in_flight;
    return taken;
}

void HandleTable::finish_locked(ContextId owner) {
    ++cleaned_;
    if (owner == kDetached) --detached_in_flight_;
    else if (ExecContext* c = contexts_.find(owner)) --c->in_flight;
    idle_.notify_all();
}

bool HandleTable::run_hook(const Taken& taken, ReleaseReason reason, CleanupFailure& failure) const {
    const KindSpec& spec = kinds_[static_cast<std::uint8_t>(taken.kind)].spec;  // immutable once registered
    if (!spec.cleanup) return true;
    CleanupOutcome outcome;
    try {
        outcome = spec.cleanup(CleanupEvent{taken.kind, taken.handle, taken.owner, reason, taken.resource});
    } catch (const std::exception& e) {
        outcome = CleanupOutcome::failure("internal.handle_cleanup_threw", e.what());
    } catch (...) {
        outcome = CleanupOutcome::failure("internal.handle_cleanup_threw", "unknown exception");
    }
    if (outcome.ok) return true;
    failure = CleanupFailure{taken.handle, taken.kind, taken.owner, reason,
                             outcome.code.empty() ? handle_status_code(HandleStatus::CleanupFailed) : outcome.code,
                             std::move(outcome.message)};
    return false;
}

HandleStatus HandleTable::release(const Value& handle, HandleKind kind, ContextId expected_owner,
                                  CleanupFailure* failure) {
    std::unique_lock<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    const HandleStatus status = store_.classify(handle, kind, index);
    if (status != HandleStatus::Ok) return status;
    const Slot& s = store_.slot(index);
    if (s.owner != expected_owner) return HandleStatus::NotOwned;
    if (s.counted && s.refs > 1) return HandleStatus::InUse;  // includes saturated (immortal) counts
    Taken taken = take_locked(index);
    lock.unlock();
    CleanupFailure f;
    const bool ok = run_hook(taken, ReleaseReason::Explicit, f);
    taken.resource.reset();  // the last table reference dies outside the lock
    lock.lock();
    finish_locked(taken.owner);
    if (ok) return HandleStatus::Ok;
    if (failure) *failure = std::move(f);
    return HandleStatus::CleanupFailed;
}

HandleStatus HandleTable::detach(const Value& handle, HandleKind kind, ContextId expected_owner) {
    std::lock_guard<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    HandleStatus status = store_.classify(handle, kind, index);
    if (status != HandleStatus::Ok) return status;
    if (expected_owner == kDetached || store_.slot(index).owner != expected_owner) return HandleStatus::NotOwned;
    ExecContext* c = contexts_.find(expected_owner, status);
    if (!c) return status;
    if (c->state != ContextState::Open) return HandleStatus::ContextClosed;
    if (c->build_family) return HandleStatus::DetachForbidden;
    store_.move(index, c->handles, detached_, kDetached);
    return HandleStatus::Ok;
}

HandleStatus HandleTable::transfer(const Value& handle, HandleKind kind, ContextId expected_owner,
                                   ContextId new_owner) {
    std::lock_guard<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    HandleStatus status = store_.classify(handle, kind, index);
    if (status != HandleStatus::Ok) return status;
    if (store_.slot(index).owner != expected_owner) return HandleStatus::NotOwned;
    if (expected_owner == kDetached || new_owner == kDetached) return HandleStatus::TransferForbidden;
    ExecContext* from = contexts_.find(expected_owner, status);
    if (!from) return status;
    ExecContext* to = contexts_.find(new_owner, status);
    if (!to) return status;
    if (from->state != ContextState::Open || to->state != ContextState::Open) return HandleStatus::ContextClosed;
    if (from->build_family && !to->build_family) return HandleStatus::TransferForbidden;
    if (new_owner != expected_owner) store_.move(index, from->handles, to->handles, new_owner);
    return HandleStatus::Ok;
}

HandleStatus HandleTable::retain(const Value& handle) {
    std::lock_guard<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    const HandleStatus status = store_.classify(handle, handle.handle_kind(), index);
    if (status != HandleStatus::Ok) return status;
    Slot& s = store_.slot(index);
    if (!s.counted) return HandleStatus::NotCounted;
    if (s.refs != SlotStore::kImmortalRefs) ++s.refs;  // saturates: never abandoned (a leak, not a use-after-free)
    return HandleStatus::Ok;
}

HandleStatus HandleTable::drop(const Value& handle) {
    std::unique_lock<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    const HandleStatus status = store_.classify(handle, handle.handle_kind(), index);
    if (status != HandleStatus::Ok) return status;
    Slot& s = store_.slot(index);
    if (!s.counted) return HandleStatus::NotCounted;
    if (s.refs == SlotStore::kImmortalRefs || --s.refs > 0) return HandleStatus::Ok;
    Taken taken = take_locked(index);
    lock.unlock();
    CleanupFailure f;
    const bool ok = run_hook(taken, ReleaseReason::Abandoned, f);
    taken.resource.reset();
    lock.lock();
    if (!ok) {  // queued for the owner's CloseReport: an abandoned handle has nobody to report to now
        ExecContext* c = taken.owner == kDetached ? nullptr : contexts_.find(taken.owner);
        (c ? c->pending : detached_failures_).push_back(std::move(f));
    }
    finish_locked(taken.owner);
    return HandleStatus::Ok;
}

void HandleTable::close_one(ContextId id, CloseReport& report, std::unique_lock<std::mutex>& lock) {
    ExecContext* c = contexts_.find(id);
    if (!c) return;  // already ended by a concurrent close
    append(report.failures, c->pending);
    while (c && c->handles.tail != kNil) {
        Taken taken = take_locked(c->handles.tail);  // newest first
        lock.unlock();
        CleanupFailure f;
        const bool ok = run_hook(taken, ReleaseReason::ContextEnd, f);
        taken.resource.reset();
        lock.lock();
        finish_locked(taken.owner);
        ++report.cleaned;
        if (!ok) report.failures.push_back(std::move(f));
        c = contexts_.find(id);
    }
    // Hooks other threads started (release, last drop, a concurrent close)
    // finish before the context is gone.
    idle_.wait(lock, [&] { c = contexts_.find(id); return !c || c->in_flight == 0; });
    if (!c) return;
    append(report.failures, c->pending);
    contexts_.erase(id);
    ++report.contexts;
    idle_.notify_all();
}

CloseReport HandleTable::close_context(ContextId id) {
    CloseReport report;
    std::unique_lock<std::mutex> lock(mu_);
    ExecContext* c = contexts_.find(id, report.status);
    if (!c) return report;
    if (c->state != ContextState::Open) { report.status = HandleStatus::ContextClosed; return report; }
    for (const ContextId each : contexts_.begin_close(id)) close_one(each, report, lock);
    return report;
}

CloseReport HandleTable::shutdown() {
    CloseReport report;
    std::unique_lock<std::mutex> lock(mu_);
    // Repeats until nothing is left: a hook may open contexts or create handles.
    for (;;) {
        const std::vector<ContextId> roots = contexts_.roots();
        ContextId newest_open = kDetached;
        for (auto it = roots.rbegin(); it != roots.rend() && newest_open == kDetached; ++it) {
            const ExecContext* c = contexts_.find(*it);
            if (c && c->state == ContextState::Open) newest_open = *it;
        }
        if (newest_open != kDetached) {
            for (const ContextId each : contexts_.begin_close(newest_open)) close_one(each, report, lock);
            continue;
        }
        if (!roots.empty()) { idle_.wait(lock); continue; }  // closes running on other threads
        if (detached_.tail != kNil) {
            Taken taken = take_locked(detached_.tail);
            lock.unlock();
            CleanupFailure f;
            const bool ok = run_hook(taken, ReleaseReason::Shutdown, f);
            taken.resource.reset();
            lock.lock();
            finish_locked(kDetached);
            ++report.cleaned;
            if (!ok) report.failures.push_back(std::move(f));
            continue;
        }
        if (detached_in_flight_ == 0) break;
        idle_.wait(lock);
    }
    append(report.failures, detached_failures_);
    return report;
}

TableStats HandleTable::stats() const {
    std::lock_guard<std::mutex> lock(mu_);
    TableStats s;
    s.live = store_.live();
    s.slots = store_.size();
    s.retired = store_.retired();
    s.detached = detached_.count;
    s.contexts = contexts_.size();
    s.created = created_;
    s.cleaned = cleaned_;
    return s;
}

bool HandleTable::set_free_generation_for_test(std::uint32_t index, std::uint32_t generation) {
    std::lock_guard<std::mutex> lock(mu_);
    return store_.set_free_generation_for_test(index, generation);
}

} // namespace nift::rt::handles
