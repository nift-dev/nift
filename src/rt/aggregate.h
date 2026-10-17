#pragma once
// Copy-on-write arrays and insertion-ordered objects (CP09).
//
// Reading never copies. Mutation goes through array_mut/object_mut (or the
// helpers below), which first make the aggregate unshared: if another Value
// holds the same object, the receiver gets a shallow clone (children are
// shared, their refcounts bumped). Copying a Value is therefore O(1), and the
// legacy "arguments and returns are copies" semantics cost nothing until a
// copy is actually mutated.
#include "rt/atom.h"
#include "rt/heap.h"
#include "rt/value.h"

#include <cstddef>
#include <vector>

namespace nift::rt {

Value make_array(std::size_t reserve = 0);
const std::vector<Value>& array_items(const Value& array);
std::vector<Value>& array_mut(Value& array);  // unshares first
void array_push(Value& array, Value item);
std::size_t array_size(const Value& array);

Value make_object();
std::size_t object_size(const Value& object);
const std::vector<std::pair<Atom, Value>>& object_entries(const Value& object);
const Value* object_get(const Value& object, Atom key);
// Inserts at the end or replaces in place (insertion order is kept).
void object_set(Value& object, Atom key, Value value);
bool object_remove(Value& object, Atom key);

// Makes `value` (Array or Object) uniquely owned; returns true if it cloned.
bool unshare(Value& value);

} // namespace nift::rt
