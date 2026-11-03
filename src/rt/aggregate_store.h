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
// inserts: about 770 s under ASan). The rules, in order:
//   * storage of at most kAlwaysRelocate elements moves on EVERY mutation;
//   * larger storage moves on the first mutation after a reference or pointer
//     into it was handed out (array_items, array_mut, object_get, ...: the
//     kStorageLent bit), so "take a reference, mutate once, use it" faults
//     deterministically at the use, as for small storage. These moves draw on
//     a per-thread credit that every mutation refills by kCredit elements
//     (kInitialCredit to start), so the moved volume stays linear in the
//     number of mutations even when reads and writes of a huge container
//     interleave;
//   * otherwise (no reference handed out, or the credit spent) once every
//     size / kAlwaysRelocate + 1 mutations (a per-thread tick).
// A pure insert loop (the 200,000-element stress fixtures) hands out nothing
// and stays linear.
inline constexpr std::size_t kAlwaysRelocate = 256;
inline constexpr std::uint64_t kCredit = 2 * kAlwaysRelocate;
inline constexpr std::uint64_t kInitialCredit = std::uint64_t{1} << 21;

inline void note_lent(const HeapObject* object) noexcept {
#ifdef NIFT_RT_RELOCATE_ON_MUTATION
    const_cast<HeapObject*>(object)->tag |= kStorageLent;  // the header's own bookkeeping (never const storage)
#else
    (void)object;
#endif
}

inline bool relocation_due(HeapObject* object, std::size_t size) noexcept {
#if defined(NIFT_RT_RELOCATION_MUTANT)
    (void)object; (void)size;
    return false;  // test-only: relocation off (the stale-reference controls must then pass silently)
#else
    thread_local std::uint64_t credit = kInitialCredit;
    credit += kCredit;
    const bool lent = (object->tag & kStorageLent) != 0;
    object->tag = static_cast<std::uint8_t>(object->tag & ~kStorageLent);
    if (size <= kAlwaysRelocate) return true;
#if !(defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 19)
    if (lent && credit >= size) {  // R19 drops this: the first mutation after a hand-out may skip the move
        credit -= size;
        return true;
    }
#endif
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
            if (relocation_due(object, items.size())) relocate(items);
            return;
        }
        case Kind::Struct:  // small by construction: always moved
            (void)relocation_due(object, 0);
            relocate(static_cast<StructObject*>(object)->fields);
            return;
        case Kind::Object: {
            auto* o = static_cast<ObjectObject*>(object);
            if (!relocation_due(object, o->entries.size())) return;
            relocate(o->entries);
            relocate(o->index);
            return;
        }
        case Kind::Collection: {
            auto* c = static_cast<CollectionObject*>(object);
            if (!relocation_due(object, c->items.size())) return;
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

// B2 (owner B): the construction API (array_push, object_set,
// struct_add_field, *_build_*) is for a value its caller alone holds and is
// still building. Checked builds abort on a shared, immutable or (re-review
// C2) sort-protected receiver: those must go through the mutation protocol
// (rt/mutation.h). Its limit: it detects sharing, not publication. A published
// value held by exactly one holder (a user binding in one register) also has
// refcount 1 and passes; the boundary there is rt-lint's per-site annotation
// (b2-refrep.md 17.2, 18).
inline void check_fresh(const Value& value) {
#ifdef NIFT_RT_RELOCATE_ON_MUTATION
    if (value.refcount() != 1 || is_immutable(value.heap()) || is_sort_protected(value.heap())) std::abort();
#else
    (void)value;
#endif
}

// Hot path (every push/set): scalars and strings are decided by one compare,
// since every kind that can carry identity sorts at or after Kind::Array.
// B2 (plan 5.1): a nested array or object is a reference too, so storing one
// marks the receiver (a flat array of scalars keeps the bit clear).
inline void note_store(HeapObject* receiver, const Value& stored) {
    if (stored.kind() < Kind::Array) return;
    if (stored.kind() <= Kind::Object || may_contain_identity(stored)) mark_may_hold_identity(receiver);
}

inline void copy_summary(const HeapObject* from, HeapObject* to) {
    if (may_hold_identity(from)) mark_may_hold_identity(to);
}

} // namespace nift::rt::aggregate_detail
