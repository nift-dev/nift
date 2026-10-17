#include "rt/handle.h"

namespace nift::rt {

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

std::shared_ptr<void> HandleRegistry::resolve(const Value& handle) const {
    if (!handle.is(Kind::Handle) || handle.handle_kind() != kind_) return nullptr;
    const std::uint64_t id = handle.handle_id();
    if (id >= slots_.size()) return nullptr;
    const Slot& slot = slots_[id];
    if (slot.generation != handle.handle_generation()) return nullptr;
    return slot.resource;
}

bool HandleRegistry::release(const Value& handle) {
    if (!resolve(handle)) return false;
    Slot& slot = slots_[handle.handle_id()];
    slot.resource.reset();
    --live_;
    if (slot.generation == kMaxGeneration) {  // retire: a further bump would wrap to a reissued generation
        ++retired_;
        return true;
    }
    ++slot.generation;  // invalidates every outstanding copy of the handle
    free_.push_back(handle.handle_id());
    return true;
}

bool HandleRegistry::set_free_slot_generation_for_test(std::uint64_t id, std::uint32_t generation) {
    if (id >= slots_.size() || slots_[id].resource) return false;
    bool is_free = false;
    for (const std::uint64_t f : free_) is_free = is_free || f == id;
    if (!is_free) return false;
    slots_[id].generation = generation;
    return true;
}

} // namespace nift::rt
