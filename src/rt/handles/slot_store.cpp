#include "rt/handles/slot_store.h"

namespace nift::rt::handles {

HandleStatus SlotStore::classify(const Value& handle, HandleKind expected, std::uint32_t& index) const {
    if (!handle.is(Kind::Handle)) return HandleStatus::NotAHandle;
    if (handle.handle_kind() != expected) return HandleStatus::KindMismatch;
    const std::uint64_t id = handle.handle_id();
    if (static_cast<std::uint32_t>(id >> 32) != tag_) return HandleStatus::Foreign;
    const std::uint32_t i = static_cast<std::uint32_t>(id);
    const std::uint32_t generation = handle.handle_generation();
    if (i >= slots_.size() || generation == 0) return HandleStatus::Forged;
    const Slot& s = slots_[i];
    switch (s.state) {
        case SlotState::Live:
            if (generation < s.generation) return HandleStatus::Closed;
            if (generation > s.generation || s.kind != expected) return HandleStatus::Forged;
            index = i;
            return HandleStatus::Ok;
        case SlotState::Free:  // the slot's generation is the next one to issue
            return generation < s.generation ? HandleStatus::Closed : HandleStatus::Forged;
        case SlotState::Retired:  // the last generation was issued and released
            return HandleStatus::Closed;
    }
    return HandleStatus::Forged;
}

HandleStatus SlotStore::allocate(HandleKind kind, std::shared_ptr<void>& resource, ContextId owner, bool counted,
                                 OwnerList& list, Value& out) {
    std::uint32_t index;
    if (!free_.empty()) {
        index = free_.back();
        free_.pop_back();
    } else {
        if (slots_.size() >= capacity_) return HandleStatus::LimitExceeded;
        index = static_cast<std::uint32_t>(slots_.size());
        slots_.emplace_back();
    }
    Slot& s = slots_[index];
    s.resource = std::move(resource);
    s.owner = owner;
    s.kind = kind;
    s.state = SlotState::Live;
    s.counted = counted;
    s.refs = counted ? 1u : 0u;
    link_tail(index, list);
    ++live_;
    out = handle_of(index);
    return HandleStatus::Ok;
}

Taken SlotStore::take(std::uint32_t index, OwnerList& list) {
    Slot& s = slots_[index];
    Taken taken{handle_of(index), s.kind, s.owner, s.record, std::move(s.resource)};
    unlink(index, list);
    s.resource.reset();
    s.owner = kDetached;
    s.record = 0;
    s.refs = 0;
    s.counted = false;
    --live_;
    if (s.generation == kMaxGeneration) {  // a bump would wrap to an issued generation
        s.state = SlotState::Retired;
        ++retired_;
    } else {
        ++s.generation;  // every outstanding copy of the handle is now stale
        s.state = SlotState::Free;
        free_.push_back(index);
    }
    return taken;
}

void SlotStore::move(std::uint32_t index, OwnerList& from, OwnerList& to, ContextId new_owner) {
    unlink(index, from);
    slots_[index].owner = new_owner;
    link_tail(index, to);
}

Value SlotStore::handle_of(std::uint32_t index) const {
    const Slot& s = slots_[index];
    return Value::handle(s.kind, (static_cast<std::uint64_t>(tag_) << 32) | index, s.generation);
}

bool SlotStore::set_free_generation_for_test(std::uint32_t index, std::uint32_t generation) {
    if (index >= slots_.size() || slots_[index].state != SlotState::Free || generation == 0) return false;
    slots_[index].generation = generation;
    return true;
}

void SlotStore::link_tail(std::uint32_t index, OwnerList& list) {
    Slot& s = slots_[index];
    s.prev = list.tail;
    s.next = kNil;
    if (list.tail != kNil) slots_[list.tail].next = index;
    else list.head = index;
    list.tail = index;
    ++list.count;
}

void SlotStore::unlink(std::uint32_t index, OwnerList& list) {
    Slot& s = slots_[index];
    if (s.prev != kNil) slots_[s.prev].next = s.next;
    else list.head = s.next;
    if (s.next != kNil) slots_[s.next].prev = s.prev;
    else list.tail = s.prev;
    s.prev = s.next = kNil;
    --list.count;
}

} // namespace nift::rt::handles
