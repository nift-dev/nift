#include "rt/aggregate.h"

#include "rt/compare.h"
#include "rt/heap_account.h"

#include <cstdlib>
#include <memory>

namespace nift::rt {

namespace {

struct CloneDeleter {
    void operator()(HeapObject* object) const noexcept { destroy_heap_object(object); }
};

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
    if (object->kind() == Kind::Collection) {
        auto* c = static_cast<CollectionObject*>(object);
        relocate(c->items);
        relocate(c->index);
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
    // Own every partial clone before copying or checked-reference relocation.
    std::unique_ptr<HeapObject, CloneDeleter> copy;
    if (value.is(Kind::Array)) {
        auto* a = new ArrayObject();
        copy.reset(a);
        a->items = value.heap_as<ArrayObject>()->items;
    } else if (value.is(Kind::Object)) {
        auto* o = new ObjectObject();
        copy.reset(o);
        const auto* source = value.heap_as<ObjectObject>();
        o->entries = source->entries;
        o->index = source->index;
    } else if (value.is(Kind::Struct)) {
        const auto* source = value.heap_as<StructObject>();
        auto* st = new StructObject(source->type_name);
        copy.reset(st);
        st->fields = source->fields;
    } else if (value.is(Kind::Collection)) {
        const auto* source = value.heap_as<CollectionObject>();
        auto* c = new CollectionObject(source->sub);
        copy.reset(c);
#if defined(NIFT_COLLECTIONS_MUTANT) && NIFT_COLLECTIONS_MUTANT == 1
        // M1: an observable copy-on-write: the "clone" is the shared original.
        copy.reset();
        return false;
#endif
        c->items = source->items;
        c->index = source->index;
    } else {
        return false;
    }
    copy_summary(value.heap(), copy.get());
    relocate_storage(value.heap());  // stale references into the shared original fault too
    heap_retally(copy.get());
    value.replace_heap(copy.release());
    return true;
}


Value make_array(std::size_t reserve) {
    // Own the object before reserve: embedding contains bad_alloc and keeps
    // the process alive, so a failed reserve must release the partial array.
    Value owned = Value::adopt(new ArrayObject());
    auto* array = owned.heap_as<ArrayObject>();
    array->items.reserve(reserve);
    heap_retally(array);
    return owned;
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

// ---- Weekend collections (D-063) ----
namespace nift::rt {
namespace {

bool keyed(const CollectionObject& c) { return c.sub != CollectionKind::Stack; }
std::size_t stride(const CollectionObject& c) { return c.sub == CollectionKind::Map ? 2 : 1; }
std::size_t entries(const CollectionObject& c) { return c.items.size() / stride(c); }
const Value& key_at(const CollectionObject& c, std::size_t entry) { return c.items[entry * stride(c)]; }

void reindex(CollectionObject& c) {
    const std::size_t n = entries(c);
    if (!keyed(c) || n < CollectionObject::kIndexThreshold) { c.index.clear(); return; }
    std::size_t capacity = 16;
    while (capacity < n * 2) capacity <<= 1;
    c.index.assign(capacity, 0);
    const std::size_t mask = capacity - 1;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t slot = static_cast<std::size_t>(hash(key_at(c, i))) & mask;
        while (c.index[slot] != 0) slot = (slot + 1) & mask;
        c.index[slot] = static_cast<std::uint32_t>(i + 1);
    }
}

std::ptrdiff_t find_entry(const CollectionObject& c, const Value& key) {
    if (!keyed(c)) return -1;
    if (c.index.empty()) {
        for (std::size_t i = 0, n = entries(c); i < n; ++i)
            if (equal(key_at(c, i), key)) return static_cast<std::ptrdiff_t>(i);
        return -1;
    }
    const std::size_t mask = c.index.size() - 1;
    for (std::size_t slot = static_cast<std::size_t>(hash(key)) & mask;; slot = (slot + 1) & mask) {
        const std::uint32_t at = c.index[slot];
        if (at == 0) return -1;
        if (equal(key_at(c, at - 1), key)) return static_cast<std::ptrdiff_t>(at - 1);
    }
}

// A new entry was appended: index it (or build the index at the threshold).
void index_appended(CollectionObject& c) {
    const std::size_t n = entries(c);
    if (n == CollectionObject::kIndexThreshold || n * 2 > c.index.size()) { reindex(c); return; }
    if (c.index.empty()) return;
    const std::size_t mask = c.index.size() - 1;
    std::size_t slot = static_cast<std::size_t>(hash(key_at(c, n - 1))) & mask;
    while (c.index[slot] != 0) slot = (slot + 1) & mask;
    c.index[slot] = static_cast<std::uint32_t>(n);
}

CollectionObject& collection_mut(Value& collection) {
    if (!unshare(collection)) relocate_storage(collection.heap());
    return *collection.heap_as<CollectionObject>();
}

} // namespace

Value make_collection(CollectionKind kind) {
    auto* c = new CollectionObject(kind);
    heap_retally(c);
    return Value::adopt(c);
}

CollectionKind collection_kind(const Value& collection) { return collection.heap_as<CollectionObject>()->sub; }

std::size_t collection_size(const Value& collection) { return entries(*collection.heap_as<CollectionObject>()); }

const std::vector<Value>& collection_items(const Value& collection) {
    return collection.heap_as<CollectionObject>()->items;
}

std::ptrdiff_t collection_find(const Value& collection, const Value& key) {
    return find_entry(*collection.heap_as<CollectionObject>(), key);
}

void map_set(Value& map, Value key, Value value) {
    const std::ptrdiff_t at = find_entry(*map.heap_as<CollectionObject>(), key);
    CollectionObject& c = collection_mut(map);
    check_not_self(&c, value);
    note_store(&c, value);
    if (at >= 0) {
        c.items[static_cast<std::size_t>(at) * 2 + 1] = static_cast<Value&&>(value);
        heap_retally(map.heap());
        return;
    }
    note_store(&c, key);  // a key may be a callable or a collection holding one
    c.items.push_back(static_cast<Value&&>(key));
    c.items.push_back(static_cast<Value&&>(value));
    index_appended(c);
#if !(defined(NIFT_COLLECTIONS_MUTANT) && NIFT_COLLECTIONS_MUTANT == 5)
    heap_retally(map.heap());  // M5 drops this: the map's growth is never counted
#endif
}

void set_add(Value& set, Value member) {
    if (find_entry(*set.heap_as<CollectionObject>(), member) >= 0) return;
    CollectionObject& c = collection_mut(set);
    check_not_self(&c, member);
    note_store(&c, member);  // members may be callables or collections
    c.items.push_back(static_cast<Value&&>(member));
    index_appended(c);
    heap_retally(set.heap());
}

void collection_erase(Value& collection, std::size_t position) {
    CollectionObject& c = collection_mut(collection);
    const std::size_t s = stride(c);
    c.items.erase(c.items.begin() + static_cast<std::ptrdiff_t>(position * s),
                  c.items.begin() + static_cast<std::ptrdiff_t>(position * s + s));
    reindex(c);
    heap_retally(collection.heap());
}

void stack_push(Value& stack, Value item) {
    CollectionObject& c = collection_mut(stack);
    check_not_self(&c, item);
    note_store(&c, item);
    c.items.push_back(static_cast<Value&&>(item));
    heap_retally(stack.heap());
}

void stack_pop(Value& stack) {
    CollectionObject& c = collection_mut(stack);
    c.items.pop_back();
    heap_retally(stack.heap());
}

void collection_clear(Value& collection) {
    CollectionObject& c = collection_mut(collection);
    c.items.clear();
    c.index.clear();
    heap_retally(collection.heap());
}

void rebuild_collection_index(CollectionObject& collection) { reindex(collection); }

const char* collection_kind_name(CollectionKind kind) {
    switch (kind) {
        case CollectionKind::Map: return "map";
        case CollectionKind::Set: return "set";
        case CollectionKind::Stack: return "stack";
    }
    return "collection";
}

} // namespace nift::rt
