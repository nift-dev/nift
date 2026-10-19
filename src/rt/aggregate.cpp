#include "rt/aggregate.h"

#include "rt/heap_account.h"

#include <cstdlib>

namespace nift::rt {

namespace {

// Reference-safety enforcement (CP11 review round 2, owner item 6): in
// sanitizer builds (or with NIFT_RT_CHECKED_REFS) every mutation entry point
// first moves the aggregate's storage to a fresh allocation and frees the old
// one, so ANY element reference or pointer obtained before the mutation (see
// the contract in aggregate.h) points into freed memory and AddressSanitizer
// aborts on its next use, deterministically, instead of only when a real
// reallocation happens to occur. Release builds compile this away.
#if defined(__SANITIZE_ADDRESS__) || defined(NIFT_RT_CHECKED_REFS)
#define NIFT_RT_RELOCATE_ON_MUTATION 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NIFT_RT_RELOCATE_ON_MUTATION 1
#endif
#endif

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

void relocate_storage(HeapObject* object) {
    if (object->kind() == Kind::Array) relocate(static_cast<ArrayObject*>(object)->items);
    if (object->kind() == Kind::Struct) relocate(static_cast<StructObject*>(object)->fields);
    if (object->kind() == Kind::Object) {
        auto* o = static_cast<ObjectObject*>(object);
        relocate(o->entries);
        relocate(o->index);
    }
}

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

// Retain-before-unique (D-040 V16): the stored value is owned by the callee's
// parameter before the receiver is made unique, so a value stored into
// itself (`a.push(a)`) is the receiver's previous value and unshare clones
// the receiver. Checked builds verify that a store never makes an object
// contain itself directly.
void check_not_self(const HeapObject* receiver, const Value& stored) {
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

void copy_summary(const HeapObject* from, HeapObject* to) {
    if (may_hold_identity(from)) mark_may_hold_identity(to);
}

std::ptrdiff_t field_position(const StructObject& s, Atom field) {
    for (std::size_t i = 0; i < s.fields.size(); ++i)
        if (s.fields[i].first == field) return static_cast<std::ptrdiff_t>(i);
    return -1;
}

ObjectObject& object_mut(Value& object) {
    if (!unshare(object)) relocate_storage(object.heap());
    return *object.heap_as<ObjectObject>();
}

} // namespace

bool unshare(Value& value) {
    if (value.refcount() <= 1) return false;
    HeapObject* copy = nullptr;
    if (value.is(Kind::Array)) {
        auto* a = new ArrayObject();
        a->items = value.heap_as<ArrayObject>()->items;
        copy = a;
    } else if (value.is(Kind::Object)) {
        auto* o = new ObjectObject();
        const auto* source = value.heap_as<ObjectObject>();
        o->entries = source->entries;
        o->index = source->index;
        copy = o;
    } else if (value.is(Kind::Struct)) {
        const auto* source = value.heap_as<StructObject>();
        auto* st = new StructObject(source->type_name);
        st->fields = source->fields;
        copy = st;
    } else {
        return false;
    }
    copy_summary(value.heap(), copy);
    relocate_storage(value.heap());  // stale references into the shared original fault too
    heap_retally(copy);
    value.replace_heap(copy);
    return true;
}


Value make_array(std::size_t reserve) {
    auto* array = new ArrayObject();
    array->items.reserve(reserve);
    heap_retally(array);
    return Value::adopt(array);
}

const std::vector<Value>& array_items(const Value& array) { return array.heap_as<ArrayObject>()->items; }

std::vector<Value>& array_mut(Value& array) {
    if (!unshare(array)) relocate_storage(array.heap());
    mark_may_hold_identity(array.heap());  // raw storage: anything may be stored
    return array.heap_as<ArrayObject>()->items;
}

void array_push(Value& array, Value item) {
    if (!unshare(array)) relocate_storage(array.heap());
    check_not_self(array.heap(), item);
    note_store(array.heap(), item);
    array.heap_as<ArrayObject>()->items.push_back(static_cast<Value&&>(item));
    heap_retally(array.heap());
}

void array_set(Value& array, std::size_t index, Value item) {
    if (!unshare(array)) relocate_storage(array.heap());
    check_not_self(array.heap(), item);
    note_store(array.heap(), item);
    array.heap_as<ArrayObject>()->items[index] = static_cast<Value&&>(item);
}

Value* array_slot_mut(Value& array, std::size_t index) {
    if (index >= array_size(array)) return nullptr;
    return &array_mut(array)[index];
}

std::size_t array_size(const Value& array) { return array_items(array).size(); }

Value make_object() {
    auto* object = new ObjectObject();
    heap_retally(object);
    return Value::adopt(object);
}

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
    check_not_self(&o, value);
    note_store(&o, value);
    const std::ptrdiff_t at = find_position(o, key);
    if (at >= 0) { o.entries[static_cast<std::size_t>(at)].second = static_cast<Value&&>(value); return; }
    o.entries.emplace_back(key, static_cast<Value&&>(value));
    if (o.entries.size() == ObjectObject::kIndexThreshold || o.entries.size() * 2 > o.index.size()) {
        rebuild_index(o);
    } else if (!o.index.empty()) {
        const std::size_t mask = o.index.size() - 1;
        std::size_t slot = slot_of(key, mask);
        while (o.index[slot] != 0) slot = (slot + 1) & mask;
        o.index[slot] = static_cast<std::uint32_t>(o.entries.size());
    }
    heap_retally(object.heap());
}

bool object_remove(Value& object, Atom key) {
    if (find_position(*object.heap_as<ObjectObject>(), key) < 0) return false;
    ObjectObject& o = object_mut(object);
    const std::ptrdiff_t at = find_position(o, key);
    o.entries.erase(o.entries.begin() + at);
    rebuild_index(o);
    heap_retally(object.heap());
    return true;
}

Value* object_slot_mut(Value& object, Atom key) {
    if (find_position(*object.heap_as<ObjectObject>(), key) < 0) return nullptr;
    ObjectObject& o = object_mut(object);
    mark_may_hold_identity(&o);
    return &o.entries[static_cast<std::size_t>(find_position(o, key))].second;
}

Value make_struct(Atom type_name, std::size_t reserve) {
    auto* instance = new StructObject(type_name);
    instance->fields.reserve(reserve);
    heap_retally(instance);
    return Value::adopt(instance);
}

Atom struct_type(const Value& instance) { return instance.heap_as<StructObject>()->type_name; }

const std::vector<std::pair<Atom, Value>>& struct_fields(const Value& instance) {
    return instance.heap_as<StructObject>()->fields;
}

const Value* struct_get(const Value& instance, Atom field) {
    const auto* s = instance.heap_as<StructObject>();
    const std::ptrdiff_t at = field_position(*s, field);
    return at < 0 ? nullptr : &s->fields[static_cast<std::size_t>(at)].second;
}

void struct_add_field(Value& instance, Atom field, Value value) {
    if (!unshare(instance)) relocate_storage(instance.heap());
    check_not_self(instance.heap(), value);
    note_store(instance.heap(), value);
    instance.heap_as<StructObject>()->fields.emplace_back(field, static_cast<Value&&>(value));
    heap_retally(instance.heap());
}

bool struct_set(Value& instance, Atom field, Value value) {
    if (field_position(*instance.heap_as<StructObject>(), field) < 0) return false;
    if (!unshare(instance)) relocate_storage(instance.heap());
    auto* s = instance.heap_as<StructObject>();
    check_not_self(s, value);
    note_store(s, value);
    s->fields[static_cast<std::size_t>(field_position(*s, field))].second = static_cast<Value&&>(value);
    return true;
}

Value* struct_slot_mut(Value& instance, Atom field) {
    if (field_position(*instance.heap_as<StructObject>(), field) < 0) return nullptr;
    if (!unshare(instance)) relocate_storage(instance.heap());
    auto* s = instance.heap_as<StructObject>();
    mark_may_hold_identity(s);
    return &s->fields[static_cast<std::size_t>(field_position(*s, field))].second;
}

} // namespace nift::rt
