#include "rt/aggregate.h"

#include "rt/aggregate_store.h"
#include "rt/compare.h"
#include "rt/heap_account.h"

#include <cstdlib>
#include <memory>

namespace nift::rt {

namespace {

using namespace aggregate_detail;

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

std::ptrdiff_t field_position(const StructObject& s, Atom field) {
    for (std::size_t i = 0; i < s.fields.size(); ++i)
        if (s.fields[i].first == field) return static_cast<std::ptrdiff_t>(i);
    return -1;
}

// B2 (b2-refrep.md): arrays and objects are references. A mutation writes the
// shared object in place, visibly through every alias (no copy-on-write
// clone; unshare is deleted), after the checked-build relocation.
void array_write(Value& array) {
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 24
    if (array.refcount() > 1) {  // R-B2: copy-on-write resurrected (a shared array is cloned on write)
        Value copy = make_array(array.heap_as<ArrayObject>()->items.size());
        copy.heap_as<ArrayObject>()->items = array.heap_as<ArrayObject>()->items;
        copy_summary(array.heap(), copy.heap());
        array = static_cast<Value&&>(copy);
    }
#endif
    relocate_storage(array.heap());
}

// B2: objects are references; a mutation writes the shared object in place
// (no copy-on-write clone), after the checked-build relocation.
ObjectObject& object_mut(Value& object) {
    relocate_storage(object.heap());
    return *object.heap_as<ObjectObject>();
}

} // namespace

Value make_array(std::size_t reserve) {
    // Own the object before reserve: embedding contains bad_alloc and keeps
    // the process alive, so a failed reserve must release the partial array.
    Value owned = Value::adopt(new ArrayObject());
    auto* array = owned.heap_as<ArrayObject>();
    array->items.reserve(reserve);
    heap_retally(array);
    return owned;
}

const std::vector<Value>& array_items(const Value& array) {
    note_lent(array.heap());
    return array.heap_as<ArrayObject>()->items;
}

const std::vector<Value>& array_view(const Value& array) { return array.heap_as<ArrayObject>()->items; }

// The storage is for the caller's own mutation (it relocated first): not a
// hand-out of the result (review round 2: setting the bit here drained the
// relocation credit on every VM mutation).
std::vector<Value>& array_mut(Value& array) {
    array_write(array);
    mark_may_hold_identity(array.heap());  // raw storage: anything may be stored
    return array.heap_as<ArrayObject>()->items;
}

void array_push(Value& array, Value item) {
    array_write(array);
    check_not_self(array.heap(), item);
    note_store(array.heap(), item);
    array.heap_as<ArrayObject>()->items.push_back(static_cast<Value&&>(item));
    heap_retally(array.heap());
}

void array_set(Value& array, std::size_t index, Value item) {
    array_write(array);
    check_not_self(array.heap(), item);
    note_store(array.heap(), item);
    array.heap_as<ArrayObject>()->items[index] = static_cast<Value&&>(item);
}

Value* array_slot_mut(Value& array, std::size_t index) {
    if (index >= array_size(array)) return nullptr;
    return &array_mut(array)[index];
}

std::size_t array_size(const Value& array) { return array.heap_as<ArrayObject>()->items.size(); }

// B2: the explicit O(n) shallow clones that replaced unshare's implicit one
// (loop and callback snapshots): a new outer object, the same elements.
Value array_shallow_clone(const Value& array) {
    Value owned = make_array(0);  // owned before copying (bad_alloc)
    auto* a = owned.heap_as<ArrayObject>();
    a->items = array.heap_as<ArrayObject>()->items;
    copy_summary(array.heap(), a);
    heap_retally(a);
    return owned;
}

Value object_shallow_clone(const Value& object) {
    Value owned = make_object();
    auto* o = owned.heap_as<ObjectObject>();
    const auto* source = object.heap_as<ObjectObject>();
    o->entries = source->entries;
    o->index = source->index;
    copy_summary(source, o);
    heap_retally(o);
    return owned;
}

Value make_object() {
    auto* object = new ObjectObject();
    heap_retally(object);
    return Value::adopt(object);
}

std::size_t object_size(const Value& object) { return object.heap_as<ObjectObject>()->entries.size(); }

const std::vector<std::pair<Atom, Value>>& object_entries(const Value& object) {
    note_lent(object.heap());
    return object.heap_as<ObjectObject>()->entries;
}

const std::vector<std::pair<Atom, Value>>& object_view(const Value& object) {
    return object.heap_as<ObjectObject>()->entries;
}

const Value* object_find(const Value& object, Atom key) {
    const auto* o = object.heap_as<ObjectObject>();
    const std::ptrdiff_t at = find_position(*o, key);
    return at < 0 ? nullptr : &o->entries[static_cast<std::size_t>(at)].second;
}

const Value* object_get(const Value& object, Atom key) {
    note_lent(object.heap());
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
    mark_may_hold_identity(&o);  // the slot is for the caller's own store: not a hand-out
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

// Structs are references (D-NEXT-refsem #1, #4): in place, never cloned.
void struct_add_field(Value& instance, Atom field, Value value) {
    relocate_storage(instance.heap());
    check_not_self(instance.heap(), value);
    note_store(instance.heap(), value);
    instance.heap_as<StructObject>()->fields.emplace_back(field, static_cast<Value&&>(value));
    heap_retally(instance.heap());
}

bool struct_set(Value& instance, Atom field, Value value) {
    if (field_position(*instance.heap_as<StructObject>(), field) < 0) return false;
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 2
    if (instance.refcount() > 1) {  // R2: a shared instance is copied on write (the old value semantics)
        const auto* source = instance.heap_as<StructObject>();
        Value copy = make_struct(source->type_name, source->fields.size());
        for (const auto& f : source->fields) struct_add_field(copy, f.first, f.second);
        instance = std::move(copy);
    }
#endif
    relocate_storage(instance.heap());
    auto* s = instance.heap_as<StructObject>();
    check_not_self(s, value);
    note_store(s, value);
    s->fields[static_cast<std::size_t>(field_position(*s, field))].second = static_cast<Value&&>(value);
    return true;
}

Value* struct_slot_mut(Value& instance, Atom field) {
    if (field_position(*instance.heap_as<StructObject>(), field) < 0) return nullptr;
    relocate_storage(instance.heap());
    auto* s = instance.heap_as<StructObject>();
    mark_may_hold_identity(s);
    return &s->fields[static_cast<std::size_t>(field_position(*s, field))].second;
}

} // namespace nift::rt

