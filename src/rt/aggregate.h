#pragma once
// Aggregates (CP09; D-040; D-NEXT-refsem; Phase B B2): arrays and
// insertion-ordered objects, struct instances and (rt/aggregate_collection.h)
// map/set/stack collections are all references (B2, b2-refrep.md): copying a
// Value shares the object, and a mutation changes the shared object in place,
// visibly through every alias (copy-on-write and unshare are gone). Mutating
// an existing aggregate goes through the mutation protocol (rt/mutation.h);
// the storage primitives here are its commit layer, and otherwise build fresh
// values (rt-lint `storage-write`).
//
// Reference lifetime contract. Every reference or pointer returned here
// (array_items, array_mut, object_entries, object_get, and references to
// elements obtained through them) points into the aggregate's current heap
// object and is invalidated by ANY mutation of that object (through this Value
// or any alias: they share it), and by assigning to or destroying the last
// Value holding it. array_push/object_set additionally reallocate storage.
// Callers (the VM) must re-fetch after any of these. Copy a child Value out
// before mutating its parent (`Value v = array_items(a)[0]; array_push(a, v);`
// is safe; `array_push(a, array_items(a)[0])` is safe too because the
// argument is copied before the call). Checked builds relocate storage on
// mutation so a stale use faults (aggregate_store.h).
#include "rt/atom.h"
#include "rt/heap.h"
#include "rt/value.h"

#include <cstddef>
#include <type_traits>
#include <vector>

namespace nift::rt {

Value make_array(std::size_t reserve = 0);
const std::vector<Value>& array_items(const Value& array);
// Transient internal views (review round 2): the same storage, for a reader
// that copies what it needs out before anything mutates the aggregate (a VM
// element read, a length). They are not hand-outs, so they do not make the
// next mutation relocate in checked builds (aggregate_store.h); anything that
// keeps a reference across a call uses array_items / object_entries.
const std::vector<Value>& array_view(const Value& array);
const std::vector<std::pair<Atom, Value>>& object_view(const Value& object);
const Value* object_find(const Value& object, Atom key);
std::size_t array_size(const Value& array);

// Construction of a FRESH value: rt/build.h (builders; tooling checkpoint,
// component 2). An existing aggregate is mutated only through the mutation
// protocol (rt/mutation.h), whose commits are the private members of Storage
// below.

Value make_object();
// B2: a new outer array/object with the same elements (identity kept), O(n); arrays: snapshots only.
Value array_shallow_clone(const Value& array);
Value object_shallow_clone(const Value& object);
std::size_t object_size(const Value& object);
const std::vector<std::pair<Atom, Value>>& object_entries(const Value& object);
const Value* object_get(const Value& object, Atom key);

// Struct instances (D-NEXT-refsem #1): fields in declaration order (private
// ones included). Every Value holding an instance refers to the same object;
// the protocol mutates it in place, visibly through every alias (never
// cloned). Construction: rt/build.h (build::Struct).
Value make_struct(const std::shared_ptr<const StructType>& type, std::size_t reserve = 0);
// B3: the descriptor the instance was constructed from (never null).
const StructType& struct_type(const Value& instance);
const std::shared_ptr<const StructType>& struct_type_ref(const Value& instance);
const std::vector<std::pair<Atom, Value>>& struct_fields(const Value& instance);
const Value* struct_get(const Value& instance, Atom field);

namespace mutation {
class Protocol;
}
struct StorageTestAccess;  // tests/rt/storage_access.h only; rt-lint: test-access-decl

// B2 (b2-refrep.md, owner requirement B): the storage commits on an EXISTING
// aggregate. Every member is private: the mutation protocol's commit layer
// (mutation::Protocol, rt/mutation.h) is the only production friend, so a
// receiver's storage can be written only after the protocol's validation
// (kind, immutability, bounds, cycle, budget). The second friend is the
// storage-level unit tests' accessor, which no production file may name.
// Bookkeeping (note_store, heap_retally, checked-build relocation) is the
// same as construction's. The slot functions return the child slot of an
// aggregate (null when missing) for a nested in-place store; the parent is
// conservatively marked as possibly holding references.
class Storage {
    friend class mutation::Protocol;
    friend struct StorageTestAccess;  // rt-lint: test-access-decl (defined only under tests/)
    static std::vector<Value>& array_mut(Value& array);
    static void array_push(Value& array, Value item);
    static void array_set(Value& array, std::size_t index, Value item);  // index < size
    static Value* array_slot_mut(Value& array, std::size_t index);
    static void object_set(Value& object, Atom key, Value value);
    static bool object_remove(Value& object, Atom key);
    static Value* object_slot_mut(Value& object, Atom key);
    static bool struct_set(Value& instance, Atom field, Value value);  // false: no such field
    static Value* struct_slot_mut(Value& instance, Atom field);
    static void map_set(Value& map, Value key, Value value);
    static void set_add(Value& set, Value member);
    static void collection_erase(Value& collection, std::size_t position);
    static void stack_push(Value& stack, Value item);
    static void stack_pop(Value& stack);
    static void collection_clear(Value& collection);
};

// O(1): an identity-bearing value (a callable, a handle, a cell, a future, a
// struct instance or a collection: D-NEXT-refsem makes the last two shared
// references), or an array/object whose summary bit is set. False means the
// value certainly is and contains no identity-bearing value. B2 (plan 5.1):
// the bit also marks an array/object holding a nested array or object, so a
// false answer also means "holds no reference at all": storing it can close
// a cycle only if it is the receiver itself (D-040 V16, D-NEXT-refsem #4).
inline bool may_contain_identity(const Value& value) noexcept {
    switch (value.kind()) {
        case Kind::Function: case Kind::Handle: case Kind::Cell: case Kind::Future: case Kind::Struct:
        case Kind::Collection:
            return true;
        case Kind::Array: case Kind::Object: return may_hold_identity(value.heap());
        default: return false;
    }
}


} // namespace nift::rt

#include "rt/aggregate_collection.h"  // IWYU pragma: export
#include "rt/build.h"  // IWYU pragma: export (builders; the production-unit tail)
