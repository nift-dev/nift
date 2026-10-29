#pragma once
// Helpers shared by the CP23 transfer graph's object decoding (rt/isolate_transfer.cpp).
#include "rt/aggregate.h"
#include "rt/heap.h"

#include <cstddef>
#include <cstdint>

namespace nift::rt::transfer_detail {

inline std::size_t object_slot(Atom key, std::size_t mask) {
    return (key.id * 2654435761u) & mask;
}
inline void rebuild_object_index(ObjectObject& object) {
    if (object.entries.size() < ObjectObject::kIndexThreshold) { object.index.clear(); return; }
    std::size_t capacity = 16;
    while (capacity < object.entries.size() * 2) capacity <<= 1;
    object.index.assign(capacity, 0);
    const std::size_t mask = capacity - 1;
    for (std::size_t i = 0; i < object.entries.size(); ++i) {
        std::size_t slot = object_slot(object.entries[i].first, mask);
        while (object.index[slot] != 0) slot = (slot + 1) & mask;
        object.index[slot] = static_cast<std::uint32_t>(i + 1);
    }
}
inline void note_child(HeapObject* parent, const Value& child) {
    if (child.kind() >= Kind::Array) mark_may_hold_identity(parent);
}

}  // namespace nift::rt::transfer_detail
