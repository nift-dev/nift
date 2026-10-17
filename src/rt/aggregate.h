#pragma once
// Copy-on-write arrays and insertion-ordered objects (CP09).
//
// Reading never copies. Mutation goes through array_mut/object_mut (or the
// helpers below), which first make the aggregate unshared: if another Value
// holds the same object, the receiver gets a shallow clone (children are
// shared, their refcounts bumped). Copying a Value is therefore O(1), and the
// legacy "arguments and returns are copies" semantics cost nothing until a
// copy is actually mutated.
//
// Reference lifetime contract. Every reference or pointer returned here
// (array_items, array_mut, object_entries, object_get, and references to
// elements obtained through them) points into the aggregate's current heap
// object and is invalidated by ANY of the following on the same Value or on
// an alias that shares its object:
//   * array_push, object_set, object_remove, unshare, array_mut/object_mut
//     (which may clone the object and switch this Value to the clone, so an
//     earlier array_mut reference keeps writing into the *other* sharer's
//     storage, breaking copy semantics, or into freed memory once that
//     sharer is gone);
//   * copying the Value and then mutating either copy;
//   * assigning to or destroying the Value (or the last alias).
// array_push/object_set additionally reallocate storage. Callers (the VM)
// must re-fetch after any of these and must not hold a mutable reference
// across a copy of the aggregate. Copy a child Value out before mutating its
// parent (`Value v = array_items(a)[0]; array_push(a, v);` is safe;
// `array_push(a, array_items(a)[0])` is safe too because the argument is
// copied before the call). There is no runtime check (see the CP09 record).
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
