// B2 (owner requirement B): a structural proof, independent of compiler
// diagnostics, that a receiver's storage cannot be committed outside the
// mutation protocol. This file MUST compile: each static_assert detects, by
// substitution failure, that a removed free commit does not exist (found by
// argument-dependent lookup on Value, as an ordinary call would find it) and
// that the Storage commits and the protocol's raw commit are not accessible
// here (access is checked during substitution). The compile-fail cases beside
// it (run.sh) show the same through real calls.
#include "rt/aggregate.h"
#include "rt/aggregate_collection.h"
#include "rt/mutation.h"
#include "rt/sort_controller.h"

#include <type_traits>
#include <vector>
#include <utility>

namespace probe {
using nift::rt::Atom;
using nift::rt::Value;
template <class... T> using void_t = void;

// Removed free commits (B1 names): no overload is found for a Value receiver.
#define NIFT_ABSENT_FREE(name, ...)                                                                   \
    template <class V, class = void> struct has_##name : std::false_type {};                          \
    template <class V> struct has_##name<V, void_t<decltype(name(__VA_ARGS__))>>                     \
        : std::true_type {};
NIFT_ABSENT_FREE(map_set, std::declval<V&>(), std::declval<V>(), std::declval<V>())
NIFT_ABSENT_FREE(set_add, std::declval<V&>(), std::declval<V>())
NIFT_ABSENT_FREE(stack_push, std::declval<V&>(), std::declval<V>())
NIFT_ABSENT_FREE(stack_pop, std::declval<V&>())
NIFT_ABSENT_FREE(collection_erase, std::declval<V&>(), std::size_t(0))
NIFT_ABSENT_FREE(collection_clear, std::declval<V&>())
NIFT_ABSENT_FREE(array_set, std::declval<V&>(), std::size_t(0), std::declval<V>())
NIFT_ABSENT_FREE(array_mut, std::declval<V&>())
NIFT_ABSENT_FREE(array_slot_mut, std::declval<V&>(), std::size_t(0))
NIFT_ABSENT_FREE(object_remove, std::declval<V&>(), std::declval<Atom>())
NIFT_ABSENT_FREE(object_slot_mut, std::declval<V&>(), std::declval<Atom>())
NIFT_ABSENT_FREE(struct_set, std::declval<V&>(), std::declval<Atom>(), std::declval<V>())
NIFT_ABSENT_FREE(struct_slot_mut, std::declval<V&>(), std::declval<Atom>())
#undef NIFT_ABSENT_FREE
static_assert(!has_map_set<Value>::value && !has_set_add<Value>::value && !has_stack_push<Value>::value &&
                  !has_stack_pop<Value>::value && !has_collection_erase<Value>::value &&
                  !has_collection_clear<Value>::value,
              "a removed collection commit is callable outside the protocol");
static_assert(!has_array_set<Value>::value && !has_array_mut<Value>::value && !has_array_slot_mut<Value>::value &&
                  !has_object_remove<Value>::value && !has_object_slot_mut<Value>::value &&
                  !has_struct_set<Value>::value && !has_struct_slot_mut<Value>::value,
              "a removed array/object/struct commit is callable outside the protocol");

// The Storage commits exist (as private members) but are not accessible here.
#define NIFT_INACCESSIBLE(name, ...)                                                                   \
    template <class S, class = void> struct can_##name : std::false_type {};                          \
    template <class S> struct can_##name<S, void_t<decltype(S::name(std::declval<Value&>(), __VA_ARGS__))>> \
        : std::true_type {};
NIFT_INACCESSIBLE(array_set, std::size_t(0), std::declval<Value>())
NIFT_INACCESSIBLE(array_push, std::declval<Value>())
NIFT_INACCESSIBLE(map_set, std::declval<Value>(), std::declval<Value>())
NIFT_INACCESSIBLE(object_set, std::declval<Atom>(), std::declval<Value>())
#undef NIFT_INACCESSIBLE
static_assert(!can_array_set<nift::rt::Storage>::value && !can_array_push<nift::rt::Storage>::value &&
                  !can_map_set<nift::rt::Storage>::value && !can_object_set<nift::rt::Storage>::value,
              "a Storage commit is accessible outside the protocol");

// The protocol's raw commit (stage 3 without stage 2) is private too.
template <class P, class = void> struct can_commit : std::false_type {};
// Its address, so only the function's own access decides (not ArrayEdit's).
template <class P> struct can_commit<P, void_t<decltype(&P::commit_array)>> : std::true_type {};
static_assert(!can_commit<nift::rt::mutation::Protocol>::value, "the protocol's commit is accessible");

// B2 review M1, re-check R1: the protocol's sort commits cannot be called
// without a SortKey (no friendship involved), and the B1 free re-exports
// (mutation::sort_storage, mutation::commit_permutation) are gone.
template <class P, class = void> struct can_sort_storage : std::false_type {};
template <class P>
struct can_sort_storage<P, void_t<decltype(P::sort_storage(std::declval<Value&>()))>> : std::true_type {};
template <class P, class = void> struct can_sort_items : std::false_type {};
template <class P>
struct can_sort_items<P, void_t<decltype(P::sort_items(std::declval<Value&>()))>> : std::true_type {};
template <class P, class = void> struct can_permute : std::false_type {};
template <class P>
struct can_permute<P, void_t<decltype(P::commit_permutation(std::declval<Value&>(),
                                                            std::declval<std::vector<Value>&>()))>> : std::true_type {};
static_assert(!can_sort_storage<nift::rt::mutation::Protocol>::value &&
                  !can_sort_items<nift::rt::mutation::Protocol>::value &&
                  !can_permute<nift::rt::mutation::Protocol>::value,
              "a sort commit of the protocol is callable without a SortKey");
namespace freecheck {
using namespace nift::rt::mutation;
template <class V, class = void> struct has_free_sort_storage : std::false_type {};
template <class V> struct has_free_sort_storage<V, void_t<decltype(sort_storage(std::declval<V&>()))>> : std::true_type {};
template <class V, class = void> struct has_free_permute : std::false_type {};
template <class V>
struct has_free_permute<V, void_t<decltype(commit_permutation(std::declval<V&>(), std::declval<std::vector<V>&>()))>>
    : std::true_type {};
static_assert(!has_free_sort_storage<Value>::value && !has_free_permute<Value>::value,
              "a free sort commit is callable outside the protocol");
}  // namespace freecheck

// Re-review C1: the sort controller's accessors need a SortKey, which code
// outside the three sort functions cannot make (private, user-provided
// constructor: not an aggregate, so `SortKey{}` is no way around it either).
static_assert(!std::is_default_constructible<nift::rt::mutation::SortKey>::value &&
                  !std::is_aggregate<nift::rt::mutation::SortKey>::value,
              "a SortKey can be made outside the sort controller");
// Tooling checkpoint (component 4, RECHECK2 N3): not trivially copyable (no
// bit_cast), not implicit-lifetime (a non-trivial destructor: no key appears in
// malloc'd or char storage), no copy or move, and not destructible outside its
// friends.
static_assert(!std::is_trivially_copyable<nift::rt::mutation::SortKey>::value &&
                  !std::is_copy_constructible<nift::rt::mutation::SortKey>::value &&
                  !std::is_move_constructible<nift::rt::mutation::SortKey>::value &&
                  !std::is_trivially_destructible<nift::rt::mutation::SortKey>::value &&
                  !std::is_destructible<nift::rt::mutation::SortKey>::value,
              "a SortKey can be byte-copied, copied, moved or destroyed outside the sort functions");

// Controls (the detectors can say yes): the construction API and a protocol
// entry point are found and accessible.
template <class V, class = void> struct has_build : std::false_type {};
template <class V>
struct has_build<V, void_t<decltype(map_build_set(std::declval<V&>(), std::declval<V>(), std::declval<V>()))>>
    : std::true_type {};
static_assert(has_build<Value>::value, "control: the construction API is not detected");
template <class V, class = void> struct has_entry : std::false_type {};
template <class V>
struct has_entry<V, void_t<decltype(nift::rt::mutation::map_put(std::declval<V&>(), std::declval<V>(), std::declval<V>(),
                                                                nift::rt::mutation::Env{}))>> : std::true_type {};
static_assert(has_entry<Value>::value, "control: a protocol entry point is not detected");
} // namespace probe
