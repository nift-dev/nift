#pragma once
// The mutation protocol (Phase B, checkpoint B1; docs/runtime-2/checkpoints/
// b1-mutation.md). Every mutation of an existing array, object, struct
// instance or map/set/stack, from a VM instruction or a native, goes through
// these stages in this fixed order:
//
//   1. evaluate    the compiler has put the receiver and every operand in
//                  registers before the mutating instruction (the F2 rule);
//   2. validate    writing nothing, in order:
//                    2.1 the receiver kind (an ordinary error, never internal);
//                    2.2 the kImmutable check ("cannot mutate const array: <receiver>");
//                    2.3 bounds and argument kinds;
//                    2.4 the cycle check, for each stored value in operand order;
//                    2.5 the budget reservation (growth, plus any clone);
//   3. commit      exactly one structural change (splice and sort build their
//                  result on the side and swap it in);
//   4. bookkeeping note_store and the identity summary bit, heap_retally,
//                  the checked-build relocate_storage (the D9 schedule,
//                  unchanged, applied as the commit starts) and kStorageLent.
//
// A failed validation leaves the receiver byte-identical, the live heap and
// every refcount unchanged (tests/rt/mutation_protocol.cpp). Argument side
// effects already performed in stage 1 are not undone (F2).
//
// The validated entry points below run all four stages; the VM's mutating
// instructions call them (or, where legacy's texts interleave with the
// stages, the stage functions in the same order, then a commit_* function).
// The storage-level commit primitives (array_push, object_set, struct_set,
// map_set, ...) live in aggregate.cpp / aggregate_collection.cpp and are only
// called on an existing receiver from here; elsewhere they build fresh values
// (rt-lint rule `storage-write`).
//
// B1 transition rules (arrays and objects are still copy-on-write values):
//   * 2.2 applies to an array or object only when the commit would write the
//     object itself (refcount <= 1); a shared one is cloned by the commit, so
//     the immutable object is still never written. B2 drops the condition.
//   * 2.4 runs for struct and collection receivers; arrays and objects are
//     acyclic by construction while they are copied, so it is skipped for
//     them (closes_cycle itself accepts every aggregate target).
#include "rt/atom.h"
#include "rt/heap.h"
#include "rt/value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt {
class CycleGraph;
}

namespace nift::rt::mutation {

enum class Failure : std::uint8_t { None, Kind, Immutable, Bounds, Argument, Cycle, Budget };

struct Outcome {
    Failure failure = Failure::None;
    std::string text;       // Kind, Immutable, Bounds, Argument: the ordinary error's text
    std::size_t operand = 0;  // Cycle: the index of the stored operand that would close it
    bool ok() const noexcept { return failure == Failure::None; }
};

struct Env {
    const CycleGraph* graph = nullptr;  // null: the default graph (no captures, no payloads)
    std::size_t heap_limit = static_cast<std::size_t>(-1);
    std::string_view receiver_text;     // the receiver's source text, for the immutable error
};

// The kImmutable bit on a value's heap object (false for non-heap values).
bool is_immutable(const Value& value) noexcept;
void make_immutable(Value& value) noexcept;

// Stage 2.2.
bool writes_in_place(const Value& receiver) noexcept;
Outcome check_mutable(const Value& receiver, std::string_view receiver_text);
// Stage 2.4.
bool cycle_applies(const Value& receiver) noexcept;
bool closes_cycle(const HeapObject* receiver, const Value& stored, const CycleGraph& graph);
// Stage 2.5: the bytes an array or collection mutation reserves before it
// commits (SIZE_MAX: the size computation overflows), and the reservation.
std::size_t array_reserve_bytes(const Value& array, std::size_t growth) noexcept;
std::size_t collection_reserve_bytes(const Value& collection, std::size_t step) noexcept;
bool reserve(std::size_t heap_limit, std::size_t bytes) noexcept;

// Stage 3 + 4 for array edits; the caller has run stage 2.
enum class ArrayEdit : std::uint8_t { Pop, Remove, Insert, Clear, Reverse, SpliceRemove, SpliceInsert };
// Remove/SpliceRemove: `index`, `count`; Insert: `operand` at `index`;
// SpliceInsert: the elements of the array `operand` at `index`.
void commit_array(Value& array, ArrayEdit edit, std::size_t index, std::size_t count, const Value& operand);
// Plain sort's single commit: element i of the result is element order[i]
// (an Int) of the receiver. `order` is a permutation built on the side.
void commit_permutation(Value& array, std::vector<Value>& order);
// The comparator sort's working storage: the receiver made unique (stage 3
// for a copy-on-write receiver: the clone is the side buffer, swapped into
// the holder by the compiler's put). See b1-mutation.md, "sort receiver".
std::vector<Value>& sort_storage(Value& array);

// Validated entry points (stages 2-4).
Outcome push(Value& array, const Value& item, const Env& env);
Outcome set(Value& array, std::size_t index, const Value& item, const Env& env);
Outcome insert(Value& array, std::size_t index, const Value& item, const Env& env);
Outcome remove(Value& array, std::size_t index, Value* removed, const Env& env);
Outcome pop(Value& array, Value* removed, const Env& env);
Outcome reverse(Value& array, const Env& env);
Outcome splice(Value& array, std::size_t index, std::size_t count, const Value& replacement, Value* removed,
               const Env& env);
Outcome object_put(Value& object, Atom key, const Value& value, const Env& env);
Outcome object_erase(Value& object, Atom key, const Env& env);
Outcome field_set(Value& instance, Atom field, const Value& value, const Env& env);
Outcome map_put(Value& map, const Value& key, const Value& value, const Env& env);
Outcome set_insert(Value& set, const Value& member, const Env& env);
// Map: the entry for `key` (removed: its value); set: the member (missing: no-op).
Outcome collection_remove(Value& collection, const Value& key, Value* removed, const Env& env);
Outcome stack_push(Value& stack, const Value& item, const Env& env);
Outcome stack_pop(Value& stack, Value* removed, const Env& env);
// Arrays and collections.
Outcome clear(Value& receiver, const Env& env);

} // namespace nift::rt::mutation
