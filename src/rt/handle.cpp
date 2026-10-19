#include "rt/handle.h"

namespace nift::rt {

const char* handle_status_code(HandleStatus status) {
    switch (status) {
        case HandleStatus::Ok: return "";
        case HandleStatus::NotAHandle: return "internal.handle_not_a_handle";
        case HandleStatus::KindMismatch: return "internal.handle_kind_mismatch";
        case HandleStatus::Foreign: return "internal.handle_foreign";
        case HandleStatus::Forged: return "internal.handle_forged";
        case HandleStatus::Stale: return "internal.handle_stale";
        case HandleStatus::TypeMismatch: return "internal.handle_type_mismatch";
        case HandleStatus::NotOwned: return "internal.handle_not_owned";
        case HandleStatus::InUse: return "internal.handle_in_use";
        case HandleStatus::NotCounted: return "internal.handle_not_counted";
        case HandleStatus::ContextClosed: return "internal.handle_context_closed";
        case HandleStatus::UnknownContext: return "internal.handle_unknown_context";
        case HandleStatus::UnknownKind: return "internal.handle_unknown_kind";
        case HandleStatus::KindRegistered: return "internal.handle_kind_registered";
        case HandleStatus::NullResource: return "internal.handle_null_resource";
        case HandleStatus::DetachForbidden: return "runtime.detach_in_build";
        case HandleStatus::TransferForbidden: return "runtime.transfer_out_of_build";
        case HandleStatus::LimitExceeded: return "limit.exceeded";
        case HandleStatus::CleanupFailed: return "internal.handle_cleanup_failed";
    }
    return "internal.handle_unknown_status";
}

const char* handle_status_text(HandleStatus status) {
    switch (status) {
        case HandleStatus::Ok: return "ok";
        case HandleStatus::NotAHandle: return "value is not a handle";
        case HandleStatus::KindMismatch: return "handle of the wrong kind";
        case HandleStatus::Foreign: return "handle from another handle table";
        case HandleStatus::Forged: return "handle was never issued";
        case HandleStatus::Stale: return "stale handle (released)";
        case HandleStatus::TypeMismatch: return "handle resource has another type";
        case HandleStatus::NotOwned: return "handle is not owned by this context";
        case HandleStatus::InUse: return "handle still has other references";
        case HandleStatus::NotCounted: return "handle is not reference counted";
        case HandleStatus::ContextClosed: return "execution context has ended";
        case HandleStatus::UnknownContext: return "unknown execution context";
        case HandleStatus::UnknownKind: return "handle kind is not registered";
        case HandleStatus::KindRegistered: return "handle kind is already registered";
        case HandleStatus::NullResource: return "null handle resource";
        case HandleStatus::DetachForbidden: return "detaching is not allowed inside a build";
        case HandleStatus::TransferForbidden: return "transfer would move a resource out of the build";
        case HandleStatus::LimitExceeded: return "handle table capacity exceeded";
        case HandleStatus::CleanupFailed: return "handle cleanup failed";
    }
    return "unknown handle status";
}

HandleErrorClass handle_error_class(HandleStatus status) {
    switch (status) {
        case HandleStatus::Ok: return HandleErrorClass::None;
        case HandleStatus::LimitExceeded: return HandleErrorClass::Limit;
        case HandleStatus::DetachForbidden:
        case HandleStatus::TransferForbidden: return HandleErrorClass::Error;
        default: return HandleErrorClass::Internal;
    }
}

Value HandleRegistry::add(std::shared_ptr<void> resource) {
    if (!resource) return Value();  // a null slot could be neither resolved nor released
    std::uint64_t id;
    if (!free_.empty()) { id = free_.back(); free_.pop_back(); }
    else { id = slots_.size(); slots_.emplace_back(); }
    Slot& slot = slots_[id];
    slot.resource = std::move(resource);
    ++live_;
    return Value::handle(kind_, id, slot.generation);
}

HandleStatus HandleRegistry::check(const Value& handle) const {
    if (!handle.is(Kind::Handle)) return HandleStatus::NotAHandle;
    if (handle.handle_kind() != kind_) return HandleStatus::KindMismatch;
    const std::uint64_t id = handle.handle_id();
    if (id >= slots_.size()) return HandleStatus::Forged;
    const Slot& slot = slots_[id];
    const std::uint32_t generation = handle.handle_generation();
    if (generation < slot.generation) return HandleStatus::Stale;
    if (generation > slot.generation) return HandleStatus::Forged;
    if (slot.resource) return HandleStatus::Ok;
    // Same generation, no resource: a retired slot's last (released) handle,
    // or a free slot's next generation, which has not been issued yet.
    return slot.retired ? HandleStatus::Stale : HandleStatus::Forged;
}

std::shared_ptr<void> HandleRegistry::resolve(const Value& handle) const {
    if (check(handle) != HandleStatus::Ok) return nullptr;
    return slots_[handle.handle_id()].resource;
}

bool HandleRegistry::release(const Value& handle) {
    if (check(handle) != HandleStatus::Ok) return false;
    Slot& slot = slots_[handle.handle_id()];
    slot.resource.reset();
    --live_;
    if (slot.generation == kMaxGeneration) {  // retire: a further bump would wrap to a reissued generation
        slot.retired = true;
        ++retired_;
        return true;
    }
    ++slot.generation;  // invalidates every outstanding copy of the handle
    free_.push_back(handle.handle_id());
    return true;
}

bool HandleRegistry::set_free_slot_generation_for_test(std::uint64_t id, std::uint32_t generation) {
    if (id >= slots_.size() || slots_[id].resource || slots_[id].retired) return false;
    bool is_free = false;
    for (const std::uint64_t f : free_) is_free = is_free || f == id;
    if (!is_free) return false;
    slots_[id].generation = generation;
    return true;
}

} // namespace nift::rt
