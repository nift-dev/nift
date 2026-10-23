#pragma once
// Copy-on-write aggregates (CP09; D-040): arrays, insertion-ordered objects
// and struct instances, all with value semantics.
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
// Unshares first. Raw storage: the array is conservatively marked as possibly
// holding identity-bearing values (rt/heap.h); prefer array_set for one slot.
std::vector<Value>& array_mut(Value& array);
void array_push(Value& array, Value item);
// Replaces element `index` (< size); unshares first.
void array_set(Value& array, std::size_t index, Value item);
std::size_t array_size(const Value& array);

Value make_object();
std::size_t object_size(const Value& object);
const std::vector<std::pair<Atom, Value>>& object_entries(const Value& object);
const Value* object_get(const Value& object, Atom key);
// Inserts at the end or replaces in place (insertion order is kept).
void object_set(Value& object, Atom key, Value value);
bool object_remove(Value& object, Atom key);

// Struct instances (D-040 V9): fields in declaration order (private ones
// included). struct_add_field is for construction; struct_set replaces an
// existing field and returns false when the struct has no such field.
Value make_struct(Atom type_name, std::size_t reserve = 0);
Atom struct_type(const Value& instance);
const std::vector<std::pair<Atom, Value>>& struct_fields(const Value& instance);
const Value* struct_get(const Value& instance, Atom field);
void struct_add_field(Value& instance, Atom field, Value value);
bool struct_set(Value& instance, Atom field, Value value);

// In-place mutation of nested places (`a[0].push(x)`, `o["k"]["m"] = v`,
// `s.q.v = 5`): each returns the child slot of an aggregate after making that
// aggregate unique, so a path is made unique top-down by chaining them and
// the final write touches no other value. The returned pointer follows the
// lifetime contract above (any later mutation of the parent invalidates it),
// and the parent is conservatively marked as possibly holding identity-bearing
// values, since the caller may store anything into the slot.
// Null when the index is out of range, the key or field is missing.
Value* array_slot_mut(Value& array, std::size_t index);
Value* object_slot_mut(Value& object, Atom key);
Value* struct_slot_mut(Value& instance, Atom field);

// Makes `value` (Array, Object or Struct) uniquely owned; returns true if it
// cloned. The clone keeps the identity summary bit.
bool unshare(Value& value);

// O(1): a callable, a handle, or an aggregate whose identity summary bit is
// set. False means the value certainly contains no callable or handle, so
// storing it can never close a cycle (D-040 V16).
inline bool may_contain_identity(const Value& value) noexcept {
    switch (value.kind()) {
        case Kind::Function: case Kind::Handle: case Kind::Cell: return true;
        case Kind::Array: case Kind::Object: case Kind::Struct: return may_hold_identity(value.heap());
        default: return false;
    }
}

} // namespace nift::rt
