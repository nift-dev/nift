#pragma once
// Map, set and stack collections (D-063; D-NEXT-refsem): identity-bearing
// mutable references within an isolate. Every Value holding a collection
// refers to the same object; the mutators below change it in place, visibly
// through every alias (never cloned). A store that would make the collection
// reachable from itself must be refused by the caller before it is made
// (rt/value_cycle.h); checked builds abort on a direct self-store.
//
// The reference lifetime contract of rt/aggregate.h applies: a reference into
// collection_items is invalidated by any mutator on any alias. Map keys and
// set members must be bool, number, string or a callable (the caller checks;
// legacy's error texts; mutable collections are refused as keys before any
// effect, D-NEXT-refsem #6). Keys compare with rt::equal (1 == 1.0, "1" != 1,
// true != 1, NaN never matches, so each NaN is new).
#include "rt/heap.h"
#include "rt/value.h"

#include <cstddef>
#include <vector>

namespace nift::rt {

Value make_collection(CollectionKind kind);
CollectionKind collection_kind(const Value& collection);
// Entries for a map, members/elements otherwise.
std::size_t collection_size(const Value& collection);
const std::vector<Value>& collection_items(const Value& collection);
// A transient internal view (aggregate.h, array_view): not a hand-out.
const std::vector<Value>& collection_view(const Value& collection);
// The raw item count (map: keys and values).
std::size_t collection_item_count(const Value& collection);
// Map/set: the entry (map) or member (set) position of `key`, or -1.
std::ptrdiff_t collection_find(const Value& collection, const Value& key);
// Construction of a FRESH collection (B2, owner B; rt/aggregate.h): map
// insert-or-replace, set add-unless-present, stack push. An existing
// collection is mutated only through the mutation protocol (Storage).
// Each gives the strong guarantee (owner D): a failed allocation leaves the
// collection unchanged (no key without its value).
void map_build_set(Value& map, Value key, Value value);
void set_build_add(Value& set, Value member);
void stack_build_push(Value& stack, Value item);
const char* collection_kind_name(CollectionKind kind);
// Rebuilds the key index from `items` (decoders that fill items directly).
void rebuild_collection_index(CollectionObject& collection);
// A new collection of the same kind with the same items (keys, values,
// members, elements shared, not copied): the shallow copy of copy() and the
// D5 loop snapshot. The caller reserves the budget.
Value collection_shallow_clone(const Value& collection);

} // namespace nift::rt
