#include "rt/aggregate_collection.h"

#include "rt/aggregate_store.h"
#include "rt/compare.h"
#include "rt/heap_account.h"
#include "rt/number.h"

#include <algorithm>
#include <memory>
#include <new>

// Map/set/stack storage (D-063) with reference semantics (D-NEXT-refsem #1):
// every mutator changes the shared object in place.
namespace nift::rt {
namespace {

using namespace aggregate_detail;


bool keyed(const CollectionObject& c) { return c.sub != CollectionKind::Stack; }
std::size_t stride(const CollectionObject& c) { return c.sub == CollectionKind::Map ? 2 : 1; }
std::size_t entries(const CollectionObject& c) { return c.items.size() / stride(c); }
const Value& key_at(const CollectionObject& c, std::size_t entry) { return c.items[entry * stride(c)]; }

int sorted_key_order(const Value& left, const Value& right) {
    int order = 0;
    if (compare_relational(left, right, order)) return order;
    // Legacy's collection sort places NaN after all other numbers.
    if (left.is_number() && right.is_number())
        return compare_decimal_forms(number_form(left), number_form(right));
    return 0;
}

// The key index of `c`'s items, built into `out` (may throw: nothing of `c`
// changes), empty below the threshold.
void build_index(const CollectionObject& c, std::vector<std::uint32_t>& out) {
    const std::size_t n = entries(c);
    out.clear();
    if (!keyed(c) || c.sorted || n < CollectionObject::kIndexThreshold) return;
    std::size_t capacity = 16;
    while (capacity < n * 2) capacity <<= 1;
    out.assign(capacity, 0);
    const std::size_t mask = capacity - 1;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t slot = static_cast<std::size_t>(hash(key_at(c, i))) & mask;
        while (out[slot] != 0) slot = (slot + 1) & mask;
        out[slot] = static_cast<std::uint32_t>(i + 1);
    }
}

void reindex(CollectionObject& c) {
    std::vector<std::uint32_t> fresh;
    build_index(c, fresh);
    c.index.swap(fresh);
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

// In place (D-NEXT-refsem #1): never cloned, whoever else holds the object.
CollectionObject& collection_mut(Value& collection) {
    relocate_storage(collection.heap());
    return *collection.heap_as<CollectionObject>();
}

} // namespace

namespace {
thread_local std::uint64_t next_collection_tag_serial = 1;

std::uint8_t tag_length_for(std::uint64_t serial) {
    std::uint8_t digits = 1;
    while (serial >= 10) {
        ++digits;
        serial /= 10;
    }
    return static_cast<std::uint8_t>(17 + digits);
}
} // namespace

void reset_collection_tag_serial() { next_collection_tag_serial = 1; }

Value make_collection(CollectionKind kind, bool sorted) {
    auto* c = new CollectionObject(kind, sorted);
    c->tag_length = tag_length_for(next_collection_tag_serial++);
    heap_retally(c);
    return Value::adopt(c);
}

CollectionKind collection_kind(const Value& collection) { return collection.heap_as<CollectionObject>()->sub; }
bool collection_sorted(const Value& collection) { return collection.heap_as<CollectionObject>()->sorted; }
std::uint8_t collection_tag_length(const Value& collection) {
    return collection.heap_as<CollectionObject>()->tag_length;
}

std::size_t collection_size(const Value& collection) { return entries(*collection.heap_as<CollectionObject>()); }

const std::vector<Value>& collection_items(const Value& collection) {
    note_lent(collection.heap());
    return collection.heap_as<CollectionObject>()->items;
}

const std::vector<Value>& collection_view(const Value& collection) {
    return collection.heap_as<CollectionObject>()->items;
}

std::size_t collection_item_count(const Value& collection) {
    return collection.heap_as<CollectionObject>()->items.size();
}

std::ptrdiff_t collection_find(const Value& collection, const Value& key) {
    return find_entry(*collection.heap_as<CollectionObject>(), key);
}

namespace {

// Owner requirement D (b2-refrep.md): every step that can throw (the
// checked-build relocation, the storage reservation, a rebuilt key index)
// runs before the first observable change, or is undone: a failed insertion
// leaves the collection unchanged and never holds a key without its value.
// Hashing and comparison of the keys (bool, number, string, callable) cannot
// throw. NIFT_MUTATION_MUTANT 6 appends the key before reserving (the defect
// the allocation-failure test, tests/rt/mutation_protocol.cpp, must see).

// The index after an append of `added` entries: in place (no allocation) or a
// new index built on the side (may throw) and swapped in.
void index_after_append(CollectionObject& c, std::size_t added) {
    const std::size_t n = entries(c);
    if (n >= CollectionObject::kIndexThreshold && (n - added < CollectionObject::kIndexThreshold ||
                                                   n * 2 > c.index.size())) {
        std::vector<std::uint32_t> fresh;  // built on the side (may throw), then swapped in
        build_index(c, fresh);
        c.index.swap(fresh);
        return;
    }
    for (std::size_t k = n - added; k < n; ++k) {
        if (c.index.empty()) return;
        const std::size_t mask = c.index.size() - 1;
        std::size_t slot = static_cast<std::size_t>(hash(key_at(c, k))) & mask;
        while (c.index[slot] != 0) slot = (slot + 1) & mask;
        c.index[slot] = static_cast<std::uint32_t>(k + 1);
    }
}

// The storage for `extra` more values, allocated before any change and
// grown as push_back grows it (libstdc++: size + max(size, n)), so the
// capacities, the heap accounting and the amortised cost are push_back's.
void reserve_for(std::vector<Value>& items, std::size_t extra) {
    if (items.size() + extra <= items.capacity()) return;
    items.reserve(items.size() + std::max(items.size(), extra));
}

// Appends `values` (a map's key and value, or one member) atomically.
template <std::size_t N> void append_entry(Value& collection, Value (&values)[N]) {
    CollectionObject& c = collection_mut(collection);
    std::size_t insertion = c.items.size();
    if (c.sorted) {
        while (insertion != 0) {
            if (sorted_key_order(values[0], c.items[insertion - N]) >= 0) break;
            insertion -= N;
        }
    }
#if defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 6
    c.items.push_back(values[0]);  // D mutant: the key lands before the reservation that may throw
    c.items.pop_back();
    c.items.push_back(values[0]);
    if (N > 1) reserve_for(c.items, N - 1);
    for (std::size_t k = 1; k < N; ++k) c.items.push_back(static_cast<Value&&>(values[k]));
#else
    reserve_for(c.items, N);  // the only storage allocation: before any change
    for (Value& v : values) c.items.push_back(static_cast<Value&&>(v));  // no allocation: cannot throw
#endif
    if (!c.sorted && c.sub != CollectionKind::Stack) {
        try {
            index_after_append(c, 1);
        } catch (...) {
            for (std::size_t k = 0; k < N; ++k) c.items.pop_back();  // undone: the collection is unchanged
            throw;
        }
    }
    for (std::size_t k = c.items.size() - N; k < c.items.size(); ++k) {
        check_not_self(&c, c.items[k]);
        note_store(&c, c.items[k]);
    }
#if !defined(NIFT_COLLECTIONS_MUTANT) || NIFT_COLLECTIONS_MUTANT != 17
    if (c.sorted && insertion != c.items.size() - N)
        std::rotate(c.items.begin() + static_cast<std::ptrdiff_t>(insertion), c.items.end() - N, c.items.end());
#endif
#if !(defined(NIFT_COLLECTIONS_MUTANT) && NIFT_COLLECTIONS_MUTANT == 5)
    heap_retally(collection.heap());  // M5 drops this: the growth is never counted
#endif
}

void put_entry(Value& map, Value key, Value value) {
    const std::ptrdiff_t at = find_entry(*map.heap_as<CollectionObject>(), key);
    if (at >= 0) {  // replace in place: no allocation
        CollectionObject& c = collection_mut(map);
        check_not_self(&c, value);
        note_store(&c, value);
        c.items[static_cast<std::size_t>(at) * 2 + 1] = static_cast<Value&&>(value);
        heap_retally(map.heap());
        return;
    }
    Value pair[2] = {static_cast<Value&&>(key), static_cast<Value&&>(value)};
    append_entry(map, pair);
}

void add_member(Value& set, Value member) {
    if (find_entry(*set.heap_as<CollectionObject>(), member) >= 0) return;
    Value one[1] = {static_cast<Value&&>(member)};
    append_entry(set, one);
}

void push_item(Value& stack, Value item) {
    Value one[1] = {static_cast<Value&&>(item)};
    append_entry(stack, one);
}

} // namespace

void Storage::map_set(Value& map, Value key, Value value) {
    put_entry(map, static_cast<Value&&>(key), static_cast<Value&&>(value));
}
void Storage::set_add(Value& set, Value member) { add_member(set, static_cast<Value&&>(member)); }
void Storage::stack_push(Value& stack, Value item) { push_item(stack, static_cast<Value&&>(item)); }

void Construction::map_set(Value& map, Value key, Value value) {
    check_fresh(map);
    put_entry(map, static_cast<Value&&>(key), static_cast<Value&&>(value));
}
void Construction::set_add(Value& set, Value member) {
    check_fresh(set);
    add_member(set, static_cast<Value&&>(member));
}
void Construction::stack_push(Value& stack, Value item) {
    check_fresh(stack);
    push_item(stack, static_cast<Value&&>(item));
}

void Storage::collection_erase(Value& collection, std::size_t position) {
    CollectionObject& c = collection_mut(collection);
    const std::size_t s = stride(c);
    c.items.erase(c.items.begin() + static_cast<std::ptrdiff_t>(position * s),
                  c.items.begin() + static_cast<std::ptrdiff_t>(position * s + s));
    try {
        reindex(c);
    } catch (const std::bad_alloc&) {
        c.index.clear();  // no index: lookups scan linearly, still exact (the next append rebuilds it)
    }
    heap_retally(collection.heap());
}

void Storage::stack_pop(Value& stack) {
    CollectionObject& c = collection_mut(stack);
    c.items.pop_back();
    heap_retally(stack.heap());
}

void Storage::collection_clear(Value& collection) {
    CollectionObject& c = collection_mut(collection);
    c.items.clear();
    c.index.clear();
    heap_retally(collection.heap());
}

void rebuild_collection_index(CollectionObject& collection) { reindex(collection); }

Value collection_shallow_clone(const Value& collection) {
    const auto* source = collection.heap_as<CollectionObject>();
    Value owned = Value::adopt(new CollectionObject(source->sub, source->sorted));
    auto* c = owned.heap_as<CollectionObject>();
    c->tag_length = source->tag_length;
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
