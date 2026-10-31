#pragma once
// Store bookkeeping shared by the aggregate implementations (aggregate.cpp,
// aggregate_collection.cpp), internal: checked-build storage relocation, the
// identity summary bit on stores, and the self-store backstop.
#include "rt/aggregate.h"
#include "rt/heap.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

// Reference-safety enforcement (CP11 review round 2, owner item 6): in
// sanitizer builds (or with NIFT_RT_CHECKED_REFS) a mutation entry point first
// moves the aggregate's storage to a fresh allocation and frees the old one,
// so an element reference or pointer obtained before the mutation (the
// contract in aggregate.h) points into freed memory and AddressSanitizer
// aborts on its next use. Release builds compile this away.
#if defined(__SANITIZE_ADDRESS__) || defined(NIFT_RT_CHECKED_REFS)
#define NIFT_RT_RELOCATE_ON_MUTATION 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NIFT_RT_RELOCATE_ON_MUTATION 1
#endif
#endif

namespace nift::rt::aggregate_detail {

// Amortised schedule (D9, D-NEXT-refsem). Moving the whole storage on every
// mutation made a growing container quadratic in checked builds (200,000
// inserts: about 770 s under ASan). Storage of at most kAlwaysRelocate
// elements is still moved on EVERY mutation, so a stale reference into a
// small aggregate faults deterministically at its next use (every stale-
// reference control and fixture is small). Larger storage is moved once every
// size / kAlwaysRelocate mutations (a per-thread tick), so the moved volume
// stays at most about 2 * kAlwaysRelocate elements per mutation and a stale
// reference into a large aggregate still faults within that many mutations.
inline constexpr std::size_t kAlwaysRelocate = 256;

inline bool relocation_due(std::size_t size) noexcept {
#if defined(NIFT_RT_RELOCATION_MUTANT)
    (void)size;
    return false;  // test-only: relocation off (the stale-reference controls must then pass silently)
#else
    if (size <= kAlwaysRelocate) return true;
    thread_local std::uint64_t tick = 0;
    return ++tick % (size / kAlwaysRelocate + 1) == 0;
#endif
}

template <class T> void relocate(std::vector<T>& items) {
#ifdef NIFT_RT_RELOCATE_ON_MUTATION
    std::vector<T> fresh;
    fresh.reserve(items.capacity());
    for (auto& item : items) fresh.push_back(static_cast<T&&>(item));
    items.swap(fresh);
#else
    (void)items;
#endif
}

inline void relocate_storage(HeapObject* object) {
#ifdef NIFT_RT_RELOCATE_ON_MUTATION
    switch (object->kind()) {
        case Kind::Array: {
            auto& items = static_cast<ArrayObject*>(object)->items;
            if (relocation_due(items.size())) relocate(items);
            return;
        }
        case Kind::Struct: relocate(static_cast<StructObject*>(object)->fields); return;  // small by construction
        case Kind::Object: {
            auto* o = static_cast<ObjectObject*>(object);
            if (!relocation_due(o->entries.size())) return;
            relocate(o->entries);
            relocate(o->index);
            return;
        }
        case Kind::Collection: {
            auto* c = static_cast<CollectionObject*>(object);
            if (!relocation_due(c->items.size())) return;
            relocate(c->items);
            relocate(c->index);
            return;
        }
        default: return;
    }
#else
    (void)object;
#endif
}

// Checked builds verify that a store never makes an object contain itself
// directly: the VM refuses every cycle before storing (D-NEXT-refsem #4), so
// reaching this is a VM defect.
inline void check_not_self(const HeapObject* receiver, const Value& stored) {
#ifdef NIFT_RT_RELOCATE_ON_MUTATION
    if (stored.is_heap() && stored.heap() == receiver) std::abort();
#else
    (void)receiver; (void)stored;
#endif
}

// Hot path (every push/set): scalars and strings are decided by one compare,
// since every kind that can carry identity sorts at or after Kind::Array.
inline void note_store(HeapObject* receiver, const Value& stored) {
    if (stored.kind() < Kind::Array) return;
    if (may_contain_identity(stored)) mark_may_hold_identity(receiver);
}

inline void copy_summary(const HeapObject* from, HeapObject* to) {
    if (may_hold_identity(from)) mark_may_hold_identity(to);
}

} // namespace nift::rt::aggregate_detail
