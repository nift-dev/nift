#include "rt/aggregate_collection.h"

#include "rt/aggregate_store.h"
#include "rt/compare.h"
#include "rt/heap_account.h"

#include <memory>

// Map/set/stack storage (D-063) with reference semantics (D-NEXT-refsem #1):
// every mutator changes the shared object in place.
namespace nift::rt {
namespace {

using namespace aggregate_detail;


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

// In place (D-NEXT-refsem #1): never cloned, whoever else holds the object.
CollectionObject& collection_mut(Value& collection) {
    relocate_storage(collection.heap());
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
    note_lent(collection.heap());
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

Value collection_shallow_clone(const Value& collection) {
    const auto* source = collection.heap_as<CollectionObject>();
    Value owned = Value::adopt(new CollectionObject(source->sub));  // owned before copying (bad_alloc)
    auto* c = owned.heap_as<CollectionObject>();
    c->items = source->items;
    c->index = source->index;
    copy_summary(source, c);
    heap_retally(c);
    return owned;
}

const char* collection_kind_name(CollectionKind kind) {
    switch (kind) {
        case CollectionKind::Map: return "map";
        case CollectionKind::Set: return "set";
        case CollectionKind::Stack: return "stack";
    }
    return "collection";
}

} // namespace nift::rt
