#include "rt/heap_account.h"

#include "rt/heap.h"

#include <limits>

namespace nift::rt {

namespace {

thread_local std::size_t live_bytes = 0;
thread_local std::size_t charged_bytes = 0;  // heap_charge (atoms), included in live_bytes

// Objects whose footprint reaches the granule field's maximum are counted
// exactly here instead (keyed by object): the header records only the
// sentinel kMaxGranules, so the live total, and with it the VM's heap budget,
// always sees the real size however large one object is or however many
// saturate (memory investigation concern 3: budgets must not be bypassed by
// large values). Only objects of 1 GiB or more ever enter this table.
//
// The table is a fixed array: accounting runs inside noexcept paths
// (destruction, retally after a resize) and must never allocate (memory
// investigation C1 and its follow-up). More than kSaturatedSlots live objects
// of 1 GiB or more cannot be counted exactly; that case sets `overflowed`,
// after which live_heap_bytes() reports the maximum, so every budget check
// fails ("memory budget exceeded") instead of under-counting.
struct Saturated {
    const HeapObject* object;
    std::size_t bytes;
};
// Tests build with a smaller table (-DNIFT_RT_SATURATED_SLOTS=1) to reach
// the overflow without reserving hundreds of GiB.
#ifndef NIFT_RT_SATURATED_SLOTS
#define NIFT_RT_SATURATED_SLOTS 256
#endif
constexpr std::size_t kSaturatedSlots = NIFT_RT_SATURATED_SLOTS;
thread_local Saturated saturated[kSaturatedSlots];
thread_local std::size_t saturated_count = 0;
thread_local bool overflowed = false;

Saturated* find_saturated(const HeapObject* object) noexcept {
    for (std::size_t i = 0; i < saturated_count; ++i)
        if (saturated[i].object == object) return &saturated[i];
    return nullptr;
}

// False when the table is full (the caller marks the accounting overflowed).
bool set_saturated(const HeapObject* object, std::size_t bytes) noexcept {
    if (Saturated* s = find_saturated(object)) { s->bytes = bytes; return true; }
    if (saturated_count == kSaturatedSlots) return false;
    saturated[saturated_count++] = Saturated{object, bytes};
    return true;
}

void erase_saturated(const HeapObject* object) noexcept {
    if (Saturated* s = find_saturated(object)) *s = saturated[--saturated_count];
}

// 24 bits (1 GiB); larger objects are counted exactly in `saturated`.
constexpr std::size_t kMaxGranules = (std::size_t(1) << 24) - 1;

// Dynamic storage of a std::string beyond its in-object buffer (libstdc++
// keeps up to 15 characters inline).
std::size_t text_storage(const std::string& text) {
    return text.capacity() > 15 ? text.capacity() + 1 : 0;
}

template <class T> std::size_t vector_storage(const std::vector<T>& items) { return items.capacity() * sizeof(T); }

// Inlined into heap_retally (hot: every push and object insert).
#if defined(__GNUC__)
__attribute__((always_inline)) inline
#endif
std::size_t raw_footprint(const HeapObject* object) {
    switch (object->kind()) {
        case Kind::String: return sizeof(StringObject) + text_storage(static_cast<const StringObject*>(object)->text);
        case Kind::Decimal:
            return sizeof(DecimalObject) + text_storage(static_cast<const DecimalObject*>(object)->spelling);
        case Kind::Bytes: return sizeof(BytesObject) + vector_storage(static_cast<const BytesObject*>(object)->data);
        case Kind::Array: return sizeof(ArrayObject) + vector_storage(static_cast<const ArrayObject*>(object)->items);
        case Kind::Object: {
            const auto* o = static_cast<const ObjectObject*>(object);
            return sizeof(ObjectObject) + vector_storage(o->entries) + vector_storage(o->index);
        }
        case Kind::Function: {
            const auto* f = static_cast<const FunctionObject*>(object);
            return sizeof(FunctionObject) + text_storage(f->name) + vector_storage(f->upvalues);
        }
        case Kind::Struct:
            return sizeof(StructObject) + vector_storage(static_cast<const StructObject*>(object)->fields);
        case Kind::Error: {
            const auto* e = static_cast<const ErrorObject*>(object);
            return sizeof(ErrorObject) + text_storage(e->message) + text_storage(e->code) +
                   text_storage(e->category) + text_storage(e->source);
        }
        case Kind::Cell: return sizeof(CellObject);  // the child Value is counted separately
        default: return 0;
    }
}

std::size_t granules_of(std::size_t bytes) {
    const std::size_t granules = (bytes + kHeapGranule - 1) / kHeapGranule;
    return granules < kMaxGranules ? granules : kMaxGranules;
}

std::size_t recorded(const HeapObject* object) {
    return std::size_t(object->counted[0]) | (std::size_t(object->counted[1]) << 8) |
           (std::size_t(object->counted[2]) << 16);
}

void record(HeapObject* object, std::size_t granules) {
    object->counted[0] = static_cast<std::uint8_t>(granules);
    object->counted[1] = static_cast<std::uint8_t>(granules >> 8);
    object->counted[2] = static_cast<std::uint8_t>(granules >> 16);
}

std::size_t exact_bytes(std::size_t raw) noexcept { return ((raw + kHeapGranule - 1) / kHeapGranule) * kHeapGranule; }

// The bytes currently counted for `object` (exact for saturated objects).
std::size_t counted_bytes(const HeapObject* object) noexcept {
    const std::size_t granules = recorded(object);
    if (granules != kMaxGranules) return granules * kHeapGranule;
    const Saturated* s = find_saturated(object);
    return s ? s->bytes : granules * kHeapGranule;  // not found: counted at the field value (overflow)
}

} // namespace

std::size_t live_heap_bytes() noexcept {
    return overflowed ? std::numeric_limits<std::size_t>::max() : live_bytes;
}

std::size_t heap_saturated_objects() noexcept { return saturated_count; }

bool heap_accounting_overflowed() noexcept { return overflowed; }

void heap_charge(std::size_t bytes) noexcept {
    live_bytes += bytes;
    charged_bytes += bytes;
}

std::size_t heap_charged_bytes() noexcept { return charged_bytes; }

std::size_t heap_footprint(const HeapObject* object) noexcept { return exact_bytes(raw_footprint(object)); }

namespace {

// Objects at or above the field's limit, or leaving it: exact side table.
// Kept out of line so heap_retally's common path stays a leaf (no frame).
#if defined(__GNUC__)
__attribute__((noinline))
#endif
void retally_saturated(HeapObject* object, std::size_t raw, std::size_t granules) noexcept {
    const std::size_t before = counted_bytes(object);
    std::size_t now = granules * kHeapGranule;
    if (granules == kMaxGranules) {
        if (set_saturated(object, exact_bytes(raw))) now = exact_bytes(raw);
        else overflowed = true;  // cannot count exactly: report over budget from now on
    } else {
        erase_saturated(object);
    }
    live_bytes += now - before;  // modular: correct for shrinking too
    record(object, granules);
}

} // namespace

void heap_retally(HeapObject* object) noexcept {
    const std::size_t raw = raw_footprint(object);
    const std::size_t granules = granules_of(raw);
    const std::size_t before = recorded(object);
    if (granules == kMaxGranules || before == kMaxGranules) {
        retally_saturated(object, raw, granules);
        return;
    }
    live_bytes += (granules - before) * kHeapGranule;  // modular: correct for shrinking too
    record(object, granules);
}

void heap_untally(HeapObject* object) noexcept {
    live_bytes -= counted_bytes(object);
    if (recorded(object) == kMaxGranules) erase_saturated(object);
    record(object, 0);
}

} // namespace nift::rt
