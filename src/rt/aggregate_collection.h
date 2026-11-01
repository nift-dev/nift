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
// Map: insert at the end or replace the value in place (position kept).
void map_set(Value& map, Value key, Value value);
// Set: append `member` unless an equal member is present.
void set_add(Value& set, Value member);
// Map/set: erase entry/member `position` (order of the others kept).
void collection_erase(Value& collection, std::size_t position);
void stack_push(Value& stack, Value item);
void stack_pop(Value& stack);
void collection_clear(Value& collection);
const char* collection_kind_name(CollectionKind kind);
// Rebuilds the key index from `items` (decoders that fill items directly).
void rebuild_collection_index(CollectionObject& collection);
// A new collection of the same kind with the same items (keys, values,
// members, elements shared, not copied): the shallow copy of copy() and the
// D5 loop snapshot. The caller reserves the budget.
Value collection_shallow_clone(const Value& collection);

} // namespace nift::rt
