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
struct Fault;
namespace natives {
struct Context;
}
// The three sort functions that commit a sort (rt/collection_sort.h,
// rt/collection_plain_sort.h), declared here so SortKey can befriend them.
bool collection_sort_init(Value*, const natives::Context&, Fault&, std::size_t site);
bool collection_sort_resume(Value*, bool answer, bool draining);
bool collection_plain_sort(Value*, const natives::Context&, Fault&);
}

namespace nift::rt::mutation {

// B2 (b2-refrep.md 18.1, re-check R1): the authority to use the protocol's
// sort commits. Defined here, completely, so no translation unit can define
// it (a second definition does not compile), and required by the commits
// themselves, so no friendship is involved: a self-defined helper class gains
// nothing. Its only constructor is private and user-provided (not an
// aggregate: `SortKey{}` does not compile), and its only friends are the three
// sort functions, which run after the sort instruction's own stage 2.2.
class SortKey {
    SortKey() {}
    // Tooling checkpoint (component 4, RECHECK2 N3): a user-provided
    // destructor and no copy or move. SortKey is then not trivially copyable,
    // so __builtin_bit_cast/std::bit_cast to it do not compile, and not an
    // implicit-lifetime type, so malloc'd or char-buffer storage is never a
    // SortKey object; with no copy, a key cannot be copied out of a forged
    // lvalue. Zero cost: the key is only ever bound to `const SortKey&`.
    ~SortKey() {}
    SortKey(const SortKey&) = delete;
    SortKey& operator=(const SortKey&) = delete;
    friend bool nift::rt::collection_sort_init(Value*, const natives::Context&, Fault&, std::size_t);
    friend bool nift::rt::collection_sort_resume(Value*, bool, bool);
    friend bool nift::rt::collection_plain_sort(Value*, const natives::Context&, Fault&);
};

enum class Failure : std::uint8_t { None, Kind, Immutable, Bounds, Argument, Cycle, Budget, SortProtected };
// B2 (owner E): the refusal text of a mutation of an array whose comparator
// sort is running (Failure::SortProtected). The VM raises it as the catchable
// runtime.sort_receiver_mutated (Vm::handle_bail); no other source emits it.
inline constexpr const char* kSortReceiverMutated =
    "sort receiver mutated: the array is being sorted by a comparator sort";

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
// B2 (owner E): also while a comparator sort of the array runs (kSortProtected);
// one test of the tag byte either way.
inline bool refuses_mutation(const Value& receiver) noexcept {
    return receiver.is_heap() && (receiver.heap()->tag & (kImmutable | kSortProtected)) != 0;
}
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

namespace detail {
template <class Text> std::string_view text_of(const Text& text) {
    if constexpr (std::is_invocable_v<const Text&>) return text();
    else return std::string_view(text);
}
NIFT_MUTATION_COLD Outcome cycle_failure(std::size_t operand);
} // namespace detail

// B2 (b2-refrep.md, owner requirement B): the protocol is one class, the only
// production friend of rt::Storage, whose private members are the storage
// commits. Every public member is a validated entry point (stages 2-4, in
// order); the sort commits are private, for the sort controller only. The VM's mutating instructions (Push,
// SetIndex, ArrayMutate, KeyedMutate, FieldSet, CollectionSort) and the
// natives call these; nothing else can reach a receiver's storage
// (tests/rt/compile_fail: a bypass does not compile; rt-lint storage-write
// stays as defence in depth). The namespace-level functions below are the
// same entry points under their B1 names.
class Protocol {
public:
    // push and set, the per-element hot paths, are inline: stages 2.1-2.2
    // (and set's 2.3), 2.4 (the inline exits of `closes`, so a scalar costs
    // one compare), then the commit, whose bookkeeping (note_store,
    // heap_retally, relocate_storage) is unchanged; both count their growth
    // after the step (no 2.5), as before. `receiver_text` is a string_view or
    // a callable returning one (read only by a refusal); `graph` is the VM's
    // cycle graph (null: the default).
    template <class Text>
    static NIFT_MUTATION_INLINE Outcome push(Value& array, const Value& item, const Text& receiver_text,
                                             const CycleGraph* graph) {
#if defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 1
        // M-B1a: the commit before the validation. The mutant validates a copy of
        // the operand (B2: the operand may be an element of the receiver itself,
        // which the early commit can move), so it fails the checks, never with
        // undefined behaviour of its own.
        const Value operand = item;
        if (array.is(Kind::Array)) Storage::array_push(array, operand);
#else
        const Value& operand = item;
#endif
        if (!admits(array, Kind::Array))
            return detail::admit_failure(array, Kind::Array, "push", "an array", detail::text_of(receiver_text));
        if (closes(array, operand, graph)) return detail::cycle_failure(0);
#if !(defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 1)
        Storage::array_push(array, operand);
#endif
        return {};
    }
    // Stage 2.3 for set: the index is in range (the caller maps the refusal
    // to legacy's text).
    template <class Text>
    static NIFT_MUTATION_INLINE Outcome set(Value& array, std::size_t index, const Value& item,
                                            const Text& receiver_text, const CycleGraph* graph) {
        if (!admits(array, Kind::Array))
            return detail::admit_failure(array, Kind::Array, "set", "an array", detail::text_of(receiver_text));
        if (index >= array.heap_as<ArrayObject>()->items.size())
            return detail::bounds_failure("set: index out of range");
        if (closes(array, item, graph)) return detail::cycle_failure(0);
        Storage::array_set(array, index, item);
        return {};
    }
    static Outcome insert(Value& array, std::size_t index, const Value& item, const Env& env);
    static Outcome remove(Value& array, std::size_t index, Value* removed, const Env& env);
    static Outcome pop(Value& array, Value* removed, const Env& env);
    static Outcome reverse(Value& array, const Env& env);
    // splice = splice_remove (validating the replacement first, when given)
    // then splice_insert: the VM's two instructions (Phase A F2: every
    // argument is evaluated, then the removal, then the insertion). `index`
    // and `count` are clamped to the array.
    static Outcome splice(Value& array, std::size_t index, std::size_t count, const Value& replacement,
                          Value* removed, const Env& env);
    static Outcome splice_remove(Value& array, std::size_t index, std::size_t count, const Value* replacement,
                                 Value* removed, const Env& env);
    static Outcome splice_insert(Value& array, std::size_t index, const Value& replacement, const Env& env);
    static Outcome object_put(Value& object, Atom key, const Value& value, const Env& env);
    static Outcome object_erase(Value& object, Atom key, const Env& env);
    static Outcome field_set(Value& instance, Atom field, const Value& value, const Env& env);
    static Outcome map_put(Value& map, const Value& key, const Value& value, const Env& env);
    static Outcome set_insert(Value& set, const Value& member, const Env& env);
    static Outcome collection_remove(Value& collection, const Value& key, Value* removed, const Env& env);
    static Outcome stack_push(Value& stack, const Value& item, const Env& env);
    static Outcome stack_pop(Value& stack, Value* removed, const Env& env);
    static Outcome clear(Value& receiver, const Env& env);
    // B2 review M1, re-check R1: the sort commits hand out or rewrite a
    // receiver's storage after the sort instruction's own stage 2.2, so each
    // requires a SortKey (above), which only the three sort functions can make
    // (tests/rt/compile_fail: absent.cpp, bypass_sort_*.cpp).
    // Plain sort's single commit (after the instruction's stage 2.2 and its
    // incomparable check): element i of the result is element order[i] (an
    // Int) of the receiver. `order` is a permutation built on the side.
    static void commit_permutation(const SortKey&, Value& array, std::vector<Value>& order);
    // The comparator sort's working storage: the receiver's own storage, in
    // place, after the sort's stage 2.2 at its start (b2-refrep.md, E).
    static std::vector<Value>& sort_storage(const SortKey&, Value& array);
    // B5 (owner Q7 ruling 2026-11-07: copy-sort-commit): the comparator sort's single commit of
    // its sorted working copy (same elements, same length) into the receiver, once, on success.
    static void commit_sorted(const SortKey&, Value& array, std::vector<Value>& sorted);
    // The same storage while the sort's controller runs (no relocation and no
    // allocation: the drain on an error unwind must not allocate).
    static std::vector<Value>& sort_items(const SortKey&, Value& array) noexcept {
        return array.heap_as<ArrayObject>()->items;
    }

private:

    enum class ArrayEdit : std::uint8_t { Pop, Remove, Insert, Clear, Reverse, SpliceRemove, SpliceInsert };
    static void commit_array(Value& array, ArrayEdit edit, std::size_t index, std::size_t count,
                             const Value& operand);
};

// The entry points under their B1 names.
template <class Text>
NIFT_MUTATION_INLINE Outcome push(Value& array, const Value& item, const Text& receiver_text,
                                  const CycleGraph* graph = nullptr) {
    return Protocol::push(array, item, receiver_text, graph);
}
NIFT_MUTATION_INLINE Outcome push(Value& array, const Value& item, const Env& env) {
    return Protocol::push(array, item, env.receiver_text, env.graph);
}
template <class Text>
NIFT_MUTATION_INLINE Outcome set(Value& array, std::size_t index, const Value& item, const Text& receiver_text,
                                 const CycleGraph* graph = nullptr) {
    return Protocol::set(array, index, item, receiver_text, graph);
}
NIFT_MUTATION_INLINE Outcome set(Value& array, std::size_t index, const Value& item, const Env& env) {
    return Protocol::set(array, index, item, env.receiver_text, env.graph);
}
inline Outcome insert(Value& a, std::size_t i, const Value& v, const Env& e) { return Protocol::insert(a, i, v, e); }
inline Outcome remove(Value& a, std::size_t i, Value* out, const Env& e) { return Protocol::remove(a, i, out, e); }
inline Outcome pop(Value& a, Value* removed, const Env& e) { return Protocol::pop(a, removed, e); }
inline Outcome reverse(Value& a, const Env& e) { return Protocol::reverse(a, e); }
inline Outcome splice(Value& a, std::size_t i, std::size_t n, const Value& r, Value* removed, const Env& e) {
    return Protocol::splice(a, i, n, r, removed, e);
}
inline Outcome object_put(Value& o, Atom k, const Value& v, const Env& e) { return Protocol::object_put(o, k, v, e); }
inline Outcome object_erase(Value& o, Atom k, const Env& e) { return Protocol::object_erase(o, k, e); }
inline Outcome field_set(Value& s, Atom f, const Value& v, const Env& e) { return Protocol::field_set(s, f, v, e); }
inline Outcome map_put(Value& m, const Value& k, const Value& v, const Env& e) { return Protocol::map_put(m, k, v, e); }
inline Outcome set_insert(Value& s, const Value& m, const Env& e) { return Protocol::set_insert(s, m, e); }
// Map: the entry for `key` (removed: its value); set: the member (missing: no-op).
inline Outcome collection_remove(Value& c, const Value& k, Value* removed, const Env& e) {
    return Protocol::collection_remove(c, k, removed, e);
}
inline Outcome stack_push(Value& s, const Value& v, const Env& e) { return Protocol::stack_push(s, v, e); }
inline Outcome stack_pop(Value& s, Value* removed, const Env& e) { return Protocol::stack_pop(s, removed, e); }
inline Outcome clear(Value& r, const Env& e) { return Protocol::clear(r, e); }  // arrays and collections

} // namespace nift::rt::mutation
