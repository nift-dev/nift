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

} // namespace

HandleTable::HandleTable(TableOptions options)
    : store_(next_table_tag(), options.capacity), invalid_setting_(std::move(options.invalid_setting)) {}

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

HandleStatus HandleTable::cancel_context(ContextId id) {
    std::lock_guard<std::mutex> lock(mu_);
    return contexts_.cancel(id);
}

HandleStatus HandleTable::context_in_build(ContextId id, bool& in_build) const {
    std::lock_guard<std::mutex> lock(mu_);
    HandleStatus status;
    const ExecContext* c = contexts_.find(id, status);
    if (c) in_build = c->build_family;
    return status;
}

HandleStatus HandleTable::create(ContextId owner, HandleKind kind, std::shared_ptr<void> resource, Value& out,
                                 CreateOptions options) {
    std::unique_lock<std::mutex> lock(mu_);
    HandleStatus status = HandleStatus::Ok;
    if (!invalid_setting_.empty()) status = HandleStatus::InvalidSetting;
    else if (!kinds_[static_cast<std::uint8_t>(kind)].registered) status = HandleStatus::UnknownKind;
    else if (!resource) status = HandleStatus::NullResource;
    ExecContext* c = status == HandleStatus::Ok ? contexts_.find(owner, status) : nullptr;
    if (c && c->state != ContextState::Open) status = HandleStatus::ContextClosed;
    if (status == HandleStatus::Ok) status = store_.allocate(kind, resource, owner, options.counted, c->handles, out);
    if (status == HandleStatus::Ok) {
        std::uint32_t index = 0;
        store_.classify(out, kind, index);
        store_.slot(index).record = records_.add(kind, out, owner);
        ++created_;
    }
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

HandleStatus HandleTable::lifecycle(const Value& handle, HandleKind kind, LifeState& state,
                                    TerminationCause& cause) const {
    std::lock_guard<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    const HandleStatus status = store_.classify(handle, kind, index);
    if (status != HandleStatus::Ok) return status;
    const LifeRecord* r = records_.find(store_.slot(index).record);
    state = r->state;
    cause = r->cause;
    return status;
}

OwnerList* HandleTable::list_of(ContextId owner) {
    if (owner == kDetached) return &detached_;
    ExecContext* c = contexts_.find(owner);
    return c ? &c->handles : nullptr;
}

std::size_t* HandleTable::obligations_of(ContextId owner) {
    if (owner == kDetached) return &detached_obligations_;
    ExecContext* c = contexts_.find(owner);
    return c ? &c->obligations : nullptr;
}

std::vector<CleanupFailure>& HandleTable::failures_of(ContextId owner) {
    ExecContext* c = owner == kDetached ? nullptr : contexts_.find(owner);
    return c ? c->failures : detached_failures_;
}

HandleTable::Started HandleTable::begin_cleanup_locked(std::uint32_t index, ReleaseReason reason) {
    const ContextId owner = store_.slot(index).owner;
    const std::uint64_t id = store_.slot(index).record;
    Started started{store_.take(index, *list_of(owner)), reason, LifeState::Active, TerminationCause::None, false};
    if (owner == kDetached) ++detached_in_flight_;
    else ++contexts_.find(owner)->in_flight;
    LifeRecord& r = *records_.find(id);
    started.state = r.state;
    started.cause = r.cause;
    r.slot_live = false;
    r.reason = reason;
    if (r.state == LifeState::Active) {   // this cleanup starts termination and holds the ticket
        r.state = LifeState::Terminating;
        r.cause = cause_of(reason);
        r.ticket_issued = started.ticket = true;
    }
    if (is_settled(r.state)) {
        records_.erase(id);               // nothing left to wait for
    } else {
        r.obligation = true;              // the owner waits until the resource settles
        ++*obligations_of(owner);
        ++obligations_;
    }
    return started;
}

void HandleTable::finish_locked(ContextId owner) {
    ++cleaned_;
    if (owner == kDetached) --detached_in_flight_;
    else if (ExecContext* c = contexts_.find(owner)) --c->in_flight;
    idle_.notify_all();
}

bool HandleTable::run_cleanup(Started& started, std::unique_lock<std::mutex>& lock, CleanupFailure* failure) {
    Taken& t = started.taken;
    const KindSpec& spec = kinds_[static_cast<std::uint8_t>(t.kind)].spec;  // immutable once registered
    CompletionTicket ticket = started.ticket ? CompletionTicket(this, t.record) : CompletionTicket();
    lock.unlock();
    CleanupOutcome outcome = CleanupOutcome::completed();
    CleanupEvent event{t.kind, t.handle, t.owner, started.reason, t.resource, started.state, started.cause,
                       started.ticket ? &ticket : nullptr};
    bool threw = false;
    if (spec.cleanup) {
        try {
            outcome = spec.cleanup(event);
        } catch (const std::exception& e) {
            outcome = CleanupOutcome::failure("internal.handle_cleanup_threw", e.what());
            threw = true;
        } catch (...) {
            outcome = CleanupOutcome::failure("internal.handle_cleanup_threw", "unknown exception");
            threw = true;
        }
    }
    bool failed = false;
    if (ticket && (threw || outcome.result != CleanupOutcome::Result::Pending)) {
        failed = outcome.result != CleanupOutcome::Result::Completed;
        if (failed && failure) {
            const bool lost = outcome.result == CleanupOutcome::Result::Lost;
            *failure = CleanupFailure{t.handle, t.kind, t.owner, started.reason, cause_of(started.reason),
                                      lost ? LifeState::Lost : LifeState::Failed, outcome.code, outcome.message};
        }
        ticket.complete(std::move(outcome));
    } else if (!started.ticket && threw) {  // a bookkeeping-only hook threw: still loud
        lock.lock();
        failures_of(t.owner).push_back(CleanupFailure{t.handle, t.kind, t.owner, started.reason, started.cause,
                                                      LifeState::Failed, outcome.code, outcome.message});
        lock.unlock();
    }
    // A ticket the hook left behind with pending() settles here as unresolved.
    { CompletionTicket leftover(std::move(ticket)); }
    t.resource.reset();  // the table's reference dies outside the lock (the reaper may keep its own)
    lock.lock();
    finish_locked(t.owner);
    return failed;
}

HandleStatus HandleTable::close_locked(std::uint32_t index, std::unique_lock<std::mutex>& lock,
                                       CleanupFailure* failure) {
    Started started = begin_cleanup_locked(index, ReleaseReason::Explicit);
    return run_cleanup(started, lock, failure) ? HandleStatus::CleanupFailed : HandleStatus::Ok;
}

HandleStatus HandleTable::close(const Value& handle, HandleKind kind, ContextId accessor, CleanupFailure* failure) {
    std::unique_lock<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    HandleStatus status = store_.classify(handle, kind, index);
    if (status == HandleStatus::Closed) return HandleStatus::Ok;  // close() is idempotent (D-035)
    if (status != HandleStatus::Ok) return status;
    status = contexts_.may_access(accessor, store_.slot(index).owner);
    if (status != HandleStatus::Ok) return status;
    return close_locked(index, lock, failure);
}

HandleStatus HandleTable::release(const Value& handle, HandleKind kind, ContextId expected_owner,
                                  CleanupFailure* failure) {
    std::unique_lock<std::mutex> lock(mu_);
    std::uint32_t index = 0;
    const HandleStatus status = store_.classify(handle, kind, index);
    if (status != HandleStatus::Ok) return status;
    if (store_.slot(index).owner != expected_owner) return HandleStatus::NotOwned;
    return close_locked(index, lock, failure);
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
    Started started = begin_cleanup_locked(index, ReleaseReason::Abandoned);
    run_cleanup(started, lock, nullptr);  // the hook schedules; failures settle into the owner's report
    return HandleStatus::Ok;
}

bool HandleTable::settle(std::uint64_t id, CleanupOutcome&& outcome) {
    std::lock_guard<std::mutex> lock(mu_);
    LifeRecord* r = records_.find(id);
    if (!r) return false;
    CleanupFailure failure;
    bool failed = false;
    if (!LifecycleStore::settle(*r, std::move(outcome), failure, failed)) return false;
    ++settled_;
    if (failed) failures_of(r->owner).push_back(std::move(failure));
    if (r->obligation) {
        r->obligation = false;
        if (std::size_t* n = obligations_of(r->owner)) --*n;
        --obligations_;
    }
    if (!r->slot_live) records_.erase(id);
    idle_.notify_all();
    return true;
}

HandleStatus HandleTable::begin_termination(const Value& handle, HandleKind kind, TerminationCause cause,
                                            CompletionTicket& ticket, TerminationStart* info) {
    CompletionTicket issued;
    TerminationStart start;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::uint32_t index = 0;
        const HandleStatus status = store_.classify(handle, kind, index);
        if (status != HandleStatus::Ok) return status;
        LifeRecord& r = *records_.find(store_.slot(index).record);
        start.state = r.state;
        start.cause = r.cause;
        if (r.state == LifeState::Active) {
            r.state = LifeState::Terminating;
            r.cause = cause;
            r.ticket_issued = start.started = true;
            issued = CompletionTicket(this, store_.slot(index).record);
        }
    }
    if (info) *info = start;
    if (issued) ticket = std::move(issued);  // outside the lock: replacing a ticket may settle it
    return HandleStatus::Ok;
}

TableStats HandleTable::stats() const {
    std::lock_guard<std::mutex> lock(mu_);
    TableStats s;
    s.live = store_.live();
    s.slots = store_.size();
    s.retired = store_.retired();
    s.detached = detached_.count;
    s.contexts = contexts_.size();
    s.records = records_.size();
    s.pending_obligations = obligations_;
    s.created = created_;
    s.cleaned = cleaned_;
    s.settled = settled_;
    return s;
}

bool HandleTable::set_free_generation_for_test(std::uint32_t index, std::uint32_t generation) {
    std::lock_guard<std::mutex> lock(mu_);
    return store_.set_free_generation_for_test(index, generation);
}

} // namespace nift::rt::handles
