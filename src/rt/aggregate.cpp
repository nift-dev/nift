#include "rt/aggregate.h"

namespace nift::rt {

namespace {

// Open-addressing index for larger objects: slots hold position + 1 (0 =
// empty); capacity is a power of two at least twice the entry count.
std::size_t slot_of(Atom key, std::size_t mask) {
    std::uint32_t h = key.id * 2654435761u;  // Knuth multiplicative hash
    return h & mask;
}

void rebuild_index(ObjectObject& object) {
    if (object.entries.size() < ObjectObject::kIndexThreshold) { object.index.clear(); return; }
    std::size_t capacity = 16;
    while (capacity < object.entries.size() * 2) capacity <<= 1;
    object.index.assign(capacity, 0);
    const std::size_t mask = capacity - 1;
    for (std::size_t i = 0; i < object.entries.size(); ++i) {
        std::size_t slot = slot_of(object.entries[i].first, mask);
        while (object.index[slot] != 0) slot = (slot + 1) & mask;
        object.index[slot] = static_cast<std::uint32_t>(i + 1);
    }
}

std::ptrdiff_t find_position(const ObjectObject& object, Atom key) {
    if (object.index.empty()) {
        for (std::size_t i = 0; i < object.entries.size(); ++i)
            if (object.entries[i].first == key) return static_cast<std::ptrdiff_t>(i);
        return -1;
    }
    const std::size_t mask = object.index.size() - 1;
    for (std::size_t slot = slot_of(key, mask);; slot = (slot + 1) & mask) {
        const std::uint32_t at = object.index[slot];
        if (at == 0) return -1;
        if (object.entries[at - 1].first == key) return static_cast<std::ptrdiff_t>(at - 1);
    }
}

ObjectObject& object_mut(Value& object) {
    unshare(object);
    return *object.heap_as<ObjectObject>();
}

} // namespace

bool unshare(Value& value) {
    if (value.refcount() <= 1) return false;
    if (value.is(Kind::Array)) {
        auto* copy = new ArrayObject();
        copy->items = value.heap_as<ArrayObject>()->items;
        value.replace_heap(copy);
        return true;
    }
    if (value.is(Kind::Object)) {
        auto* copy = new ObjectObject();
        const auto* source = value.heap_as<ObjectObject>();
        copy->entries = source->entries;
        copy->index = source->index;
        value.replace_heap(copy);
        return true;
    }
    return false;
}

Value make_array(std::size_t reserve) {
    auto* array = new ArrayObject();
    array->items.reserve(reserve);
    return Value::adopt(array);
}

const std::vector<Value>& array_items(const Value& array) { return array.heap_as<ArrayObject>()->items; }

std::vector<Value>& array_mut(Value& array) {
    unshare(array);
    return array.heap_as<ArrayObject>()->items;
}

void array_push(Value& array, Value item) { array_mut(array).push_back(static_cast<Value&&>(item)); }

std::size_t array_size(const Value& array) { return array_items(array).size(); }

Value make_object() { return Value::adopt(new ObjectObject()); }

std::size_t object_size(const Value& object) { return object.heap_as<ObjectObject>()->entries.size(); }

const std::vector<std::pair<Atom, Value>>& object_entries(const Value& object) {
    return object.heap_as<ObjectObject>()->entries;
}

const Value* object_get(const Value& object, Atom key) {
    const auto* o = object.heap_as<ObjectObject>();
    const std::ptrdiff_t at = find_position(*o, key);
    return at < 0 ? nullptr : &o->entries[static_cast<std::size_t>(at)].second;
}

void object_set(Value& object, Atom key, Value value) {
    ObjectObject& o = object_mut(object);
    const std::ptrdiff_t at = find_position(o, key);
    if (at >= 0) { o.entries[static_cast<std::size_t>(at)].second = static_cast<Value&&>(value); return; }
    o.entries.emplace_back(key, static_cast<Value&&>(value));
    if (o.entries.size() == ObjectObject::kIndexThreshold || o.entries.size() * 2 > o.index.size()) {
        rebuild_index(o);
        return;
    }
    if (o.index.empty()) return;
    const std::size_t mask = o.index.size() - 1;
    std::size_t slot = slot_of(key, mask);
    while (o.index[slot] != 0) slot = (slot + 1) & mask;
    o.index[slot] = static_cast<std::uint32_t>(o.entries.size());
}

bool object_remove(Value& object, Atom key) {
    if (find_position(*object.heap_as<ObjectObject>(), key) < 0) return false;
    ObjectObject& o = object_mut(object);
    const std::ptrdiff_t at = find_position(o, key);
    o.entries.erase(o.entries.begin() + at);
    rebuild_index(o);
    return true;
}

} // namespace nift::rt
