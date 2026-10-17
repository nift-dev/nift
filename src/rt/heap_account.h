#pragma once
// Exact live-heap accounting for the Runtime 2.0 value heap (CP11 review
// round 2, R5).
//
// Every heap object records the footprint it was last counted with (its
// header's `counted` granules); the isolate (thread) keeps the sum over its
// live objects. Code that creates a heap object, or changes the capacity of
// one it owns (string append, array push, object insert/remove,
// copy-on-write clone), calls heap_retally afterwards; destruction removes
// the recorded amount. Because the recorded amount travels with the object,
// the total can never drift: an object never retallied counts 0 and removes
// 0. Footprint = the object itself plus the dynamic storage it owns
// (string/vector capacities), rounded up to 64-byte granules and saturating
// at 1 GiB per object; child values are separate objects counted on their
// own, so nested values are covered without a scan.
#include "rt/value.h"

#include <cstddef>

namespace nift::rt {

inline constexpr std::size_t kHeapGranule = 64;

// Bytes held by the live heap objects of this isolate.
std::size_t live_heap_bytes() noexcept;

// Recounts `object` after it was created or resized.
void heap_retally(HeapObject* object) noexcept;

// Removes `object`'s recorded footprint (called just before it is freed).
void heap_untally(HeapObject* object) noexcept;

// The footprint heap_retally would record now, in bytes (granule-rounded).
std::size_t heap_footprint(const HeapObject* object) noexcept;

} // namespace nift::rt
