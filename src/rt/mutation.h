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
// B2 (b2-refrep.md): arrays and objects are references like every other
// aggregate, so stage 2.2 refuses an immutable receiver through every alias
// and stage 2.4 runs for every receiver kind (the B1 transition rules, the
// refcount-1 immutability condition and the skipped array/object cycle check,
// are gone). No commit clones: copy-on-write is deleted.
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/value.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
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

// Performance (b1-mutation.md, "B1-perf"): the stage tests on the success
// path are inline here, one kind compare and one header-bit test, and every
// refusal is built by an out-of-line cold function in mutation.cpp, so a
// mutation that passes validation builds no text and no frame for one. The
// stages, their order and the texts are unchanged.
#if defined(__GNUC__)
#define NIFT_MUTATION_COLD __attribute__((cold, noinline))
#define NIFT_MUTATION_INLINE inline __attribute__((always_inline))
#else
#define NIFT_MUTATION_COLD
#define NIFT_MUTATION_INLINE inline
#endif

// The kImmutable bit on a value's heap object (false for non-heap values).
inline bool is_immutable(const Value& value) noexcept { return value.is_heap() && rt::is_immutable(value.heap()); }
void make_immutable(Value& value) noexcept;

// Stage 2.2: true when the receiver refuses every mutation (whoever holds it).
inline bool refuses_mutation(const Value& receiver) noexcept { return is_immutable(receiver); }
namespace detail {
// The refusals, built only on failure (cold): the immutable error, and the
// kind error followed by the immutable one (admit's order: 2.1, then 2.2).
NIFT_MUTATION_COLD Outcome immutable_failure(const Value& receiver, std::string_view receiver_text);
NIFT_MUTATION_COLD Outcome admit_failure(const Value& receiver, Kind kind, const char* method, const char* want,
                                         std::string_view receiver_text);
NIFT_MUTATION_COLD Outcome bounds_failure(const char* text);
bool reserve_slow(std::size_t heap_limit, std::size_t bytes) noexcept;  // a non-zero reservation
} // namespace detail
inline Outcome check_mutable(const Value& receiver, std::string_view receiver_text) {
    if (refuses_mutation(receiver)) return detail::immutable_failure(receiver, receiver_text);
    return {};
}
// Stages 2.1 and 2.2 together: the receiver is a `kind` that may be written.
// Always inlined (also for the M-B1 mutant builds, whose define reaches only
// mutation.cpp and the unit test: no out-of-line copy for the linker to pick).
NIFT_MUTATION_INLINE bool admits(const Value& receiver, Kind kind) noexcept {
#if defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 4
    return receiver.is(kind);  // M-B1d: the inline stage 2.2 test is dropped
#else
    return receiver.is(kind) && !refuses_mutation(receiver);
#endif
}
// Stage 2.4. `closes` is the whole stage for one stored value: the O(1)
// exits inline (a scalar or string; a value holding no reference that is not
// the receiver itself), then the walk (closes_cycle) with `graph` (null: the
// default graph, no captures or payloads).
bool cycle_applies(const Value& receiver) noexcept;
bool closes_cycle(const HeapObject* receiver, const Value& stored, const CycleGraph* graph);
NIFT_MUTATION_INLINE bool closes(const Value& receiver, const Value& stored, const CycleGraph* graph) {
    if (stored.kind() < Kind::Array) return false;  // scalars and strings (every reference sorts after)
#if defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 5
    const bool summary = stored.is(Kind::Array) || stored.is(Kind::Object);  // M-B1e: a narrower pre-filter
#else
    const bool summary = may_contain_identity(stored) || (stored.is_heap() && stored.heap() == receiver.heap());
#endif
    return summary && cycle_applies(receiver) && closes_cycle(receiver.heap(), stored, graph);
}
// Stage 2.5: the bytes an array or collection mutation reserves before it
// commits (SIZE_MAX: the size computation overflows), and the reservation.
// B2: only growth beyond the capacity (there is no copy-on-write clone).
inline std::size_t array_reserve_bytes(const Value& array, std::size_t growth) noexcept {
    const auto& items = array.heap_as<ArrayObject>()->items;
    const std::size_t n = items.size();
    if (growth > static_cast<std::size_t>(-1) / (4 * sizeof(Value)) - n) return static_cast<std::size_t>(-1);
    const std::size_t reserve = growth && n + growth > items.capacity() ? 2 * (n + growth) : 0;
    return reserve ? reserve * sizeof(Value) + 128 : 0;
}
std::size_t collection_reserve_bytes(const Value& collection, std::size_t step) noexcept;
inline bool reserve(std::size_t heap_limit, std::size_t bytes) noexcept {
    return bytes == 0 || detail::reserve_slow(heap_limit, bytes);
}

// Stage 3 + 4 for array edits; the caller has run stage 2.
enum class ArrayEdit : std::uint8_t { Pop, Remove, Insert, Clear, Reverse, SpliceRemove, SpliceInsert };
// Remove/SpliceRemove: `index`, `count`; Insert: `operand` at `index`;
// SpliceInsert: the elements of the array `operand` at `index`.
// Inline (B1-perf): each VM case passes a constant edit, so it compiles to
// that edit alone, as the instruction's own commit did before B1.
NIFT_MUTATION_INLINE void commit_array(Value& array, ArrayEdit edit, std::size_t index, std::size_t count,
                                       const Value& operand) {
    Value keep = operand;  // the operand may share the receiver's object (a.splice(0, 0, a))
    auto& items = array_mut(array);  // in place (B2), after the checked-build D9 relocation
    switch (edit) {
        case ArrayEdit::Pop: items.pop_back(); break;
        case ArrayEdit::Remove: items.erase(items.begin() + static_cast<std::ptrdiff_t>(index)); break;
        case ArrayEdit::Insert:  // moved in: keep is already the copy insert(const&) would make
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), static_cast<Value&&>(keep));
            break;
        case ArrayEdit::Clear: items.clear(); break;
        case ArrayEdit::Reverse: std::reverse(items.begin(), items.end()); break;
        case ArrayEdit::SpliceRemove: {
            const auto from = items.begin() + static_cast<std::ptrdiff_t>(index);
            items.erase(from, from + static_cast<std::ptrdiff_t>(count));
            break;
        }
        case ArrayEdit::SpliceInsert: {
            if (keep.heap() == array.heap()) {  // a.splice(i, 0, a): never insert a vector's range into itself
                const std::vector<Value> own = items;
                items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), own.begin(), own.end());
                break;
            }
            const auto& replacement = array_view(keep);
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), replacement.begin(), replacement.end());
            break;
        }
    }
#if !(defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 3)
    heap_retally(array.heap());  // M-B1c drops this: the edit's growth is never counted
#endif
}
// Plain sort's single commit: element i of the result is element order[i]
// (an Int) of the receiver. `order` is a permutation built on the side.
void commit_permutation(Value& array, std::vector<Value>& order);
// The comparator sort's working storage: the receiver's own storage (B2: in
// place; b2-refrep.md, "comparator sort").
std::vector<Value>& sort_storage(Value& array);

// Validated entry points (stages 2-4).
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

// push and set, the per-element hot paths, are inline: stages 2.1-2.2 (and
// set's 2.3), 2.4 (B2: the inline exits of `closes`, so a scalar costs one
// compare), then the commit primitive, whose bookkeeping (note_store,
// heap_retally, relocate_storage) is unchanged. Both count their growth after
// the step (no 2.5), as before.
//
// `receiver_text` is a string_view, or a callable returning one (the VM's
// note lookup), and is read only by a refusal, so the success path computes
// no text. `graph` is the VM's cycle graph (null: the default). The Env forms
// are the general entry points.
namespace detail {
template <class Text> std::string_view text_of(const Text& text) {
    if constexpr (std::is_invocable_v<const Text&>) return text();
    else return std::string_view(text);
}
NIFT_MUTATION_COLD Outcome cycle_failure(std::size_t operand);
} // namespace detail

template <class Text>
NIFT_MUTATION_INLINE Outcome push(Value& array, const Value& item, const Text& receiver_text,
                                  const CycleGraph* graph = nullptr) {
#if defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 1
    if (array.is(Kind::Array)) array_push(array, item);  // M-B1a: the commit before the validation
#endif
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "push", "an array", detail::text_of(receiver_text));
    if (closes(array, item, graph)) return detail::cycle_failure(0);
#if !(defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 1)
    array_push(array, item);
#endif
    return {};
}
NIFT_MUTATION_INLINE Outcome push(Value& array, const Value& item, const Env& env) {
    return push(array, item, env.receiver_text, env.graph);
}

// Stage 2.3 for set: the index is in range (the caller reports legacy's text).
template <class Text>
NIFT_MUTATION_INLINE Outcome set(Value& array, std::size_t index, const Value& item, const Text& receiver_text,
                                 const CycleGraph* graph = nullptr) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "set", "an array", detail::text_of(receiver_text));
    if (index >= array.heap_as<ArrayObject>()->items.size()) return detail::bounds_failure("set: index out of range");
    if (closes(array, item, graph)) return detail::cycle_failure(0);
    array_set(array, index, item);
    return {};
}
NIFT_MUTATION_INLINE Outcome set(Value& array, std::size_t index, const Value& item, const Env& env) {
    return set(array, index, item, env.receiver_text, env.graph);
}

} // namespace nift::rt::mutation
