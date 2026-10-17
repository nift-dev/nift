#include "rt/heap_account.h"

#include "rt/heap.h"

namespace nift::rt {

namespace {

thread_local std::size_t live_bytes = 0;

constexpr std::size_t kMaxGranules = (std::size_t(1) << 24) - 1;

// Dynamic storage of a std::string beyond its in-object buffer (libstdc++
// keeps up to 15 characters inline).
std::size_t text_storage(const std::string& text) {
    return text.capacity() > 15 ? text.capacity() + 1 : 0;
}

template <class T> std::size_t vector_storage(const std::vector<T>& items) { return items.capacity() * sizeof(T); }

std::size_t raw_footprint(const HeapObject* object) {
    switch (object->kind) {
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

} // namespace

std::size_t live_heap_bytes() noexcept { return live_bytes; }

std::size_t heap_footprint(const HeapObject* object) noexcept {
    return granules_of(raw_footprint(object)) * kHeapGranule;
}

void heap_retally(HeapObject* object) noexcept {
    const std::size_t now = granules_of(raw_footprint(object));
    const std::size_t before = recorded(object);
    live_bytes += (now - before) * kHeapGranule;  // modular: correct for shrinking too
    record(object, now);
}

void heap_untally(HeapObject* object) noexcept {
    live_bytes -= recorded(object) * kHeapGranule;
    record(object, 0);
}

} // namespace nift::rt
