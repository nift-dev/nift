#pragma once
// B4b (docs/runtime-2/checkpoints/b4b-element-types.md, condition C1): how the
// kind inference models every mutation of an existing array or object.
//
// The list is derived from the B1 mutation protocol (rt/mutation.h): every
// entry point is classified below (kProtocolEntries), and every array method
// of rt/collection.h has its effect on the receiver's allocation sites
// (kCollectionEffects, one entry per CollectionMethod, checked by size). The
// CP14 natives (rt/natives/builtin_list.inc) do not mutate their receiver or
// arguments: no native calls the protocol. tests/runtime2/b4b_types.py checks
// all three against the sources, so a new mutator fails until it is classified.
#include "rt/collection.h"

#include <cstddef>
#include <cstdint>

namespace nift::rt::infer_detail {

// The effect of an array method on the sites of its receiver: it stores a
// value (argument 0, argument 1, or the elements of splice's replacement), or
// it moves, removes or clears elements. Every effect but None drops the sites'
// positional facts.
enum class SiteEffect : std::uint8_t { None, StoreArg0, StoreArg1, StoreSplice, Reorder, Shrink, Clear };

inline constexpr SiteEffect kCollectionEffects[] = {
    SiteEffect::StoreArg0,    // Push
    SiteEffect::Shrink,       // Pop
    SiteEffect::StoreArg1,    // Insert
    SiteEffect::Shrink,       // Remove
    SiteEffect::Clear,        // Clear
    SiteEffect::StoreSplice,  // Splice
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 20
    SiteEffect::None,         // Reverse (A20: one mutator dropped from the list)
#else
    SiteEffect::Reorder,      // Reverse
#endif
    SiteEffect::Reorder,      // Sort
    SiteEffect::Reorder,      // ComparatorSort
    SiteEffect::None,         // Map
    SiteEffect::None,         // Filter
    SiteEffect::None,         // Reduce
    SiteEffect::None,         // Any
    SiteEffect::None,         // All
    SiteEffect::None,         // Find
    SiteEffect::None,         // FindIndex
    SiteEffect::None,         // Count
    SiteEffect::None,         // SortBy
    SiteEffect::None,         // GroupBy
    SiteEffect::None,         // IndexBy
    SiteEffect::None,         // Partition
    SiteEffect::None,         // UniqueBy
    SiteEffect::None,         // MinBy
    SiteEffect::None,         // MaxBy
    SiteEffect::None,         // CountBy
    SiteEffect::None,         // GroupByEach
};
static_assert(sizeof(kCollectionEffects) / sizeof(kCollectionEffects[0]) ==
                  static_cast<std::size_t>(CollectionMethod::CountMethods),
              "every CollectionMethod has a classified effect on its receiver's sites");

inline SiteEffect collection_effect(CollectionMethod m) { return kCollectionEffects[static_cast<std::size_t>(m)]; }

// B4b 32 (review of ce0a9a44, N-1): what an array method's result is at run time, under reference
// semantics (the guard-off build; each entry verified by an identity probe that mutates the result
// and observes the receiver, b4b-element-types.md section 19). Receiver: the result is the receiver
// itself (vm_array_mutation.cpp `s[Result]=s[Source]`, collection_plain_sort.cpp, the comparator
// sort's codegen loads the receiver), so the inference gives it the receiver's sites (an alias),
// never a new allocation site. Fresh: a new array or object (its elements or members are the
// receiver's, which carry their own sites). Stored: the argument the method stores. Element: one of
// the receiver's elements. Callback: a callback's return or the initial argument (reduce). Scalar: a
// bool, int or null.
enum class ResultIdentity : std::uint8_t { Receiver, Fresh, Stored, Element, Callback, Scalar };

inline constexpr ResultIdentity kCollectionResults[] = {
    ResultIdentity::Stored,    // Push
    ResultIdentity::Element,   // Pop
    ResultIdentity::Stored,    // Insert
    ResultIdentity::Element,   // Remove
    ResultIdentity::Scalar,    // Clear
    ResultIdentity::Fresh,     // Splice (the removed elements)
    ResultIdentity::Receiver,  // Reverse
    ResultIdentity::Receiver,  // Sort
    ResultIdentity::Receiver,  // ComparatorSort
    ResultIdentity::Fresh,     // Map
    ResultIdentity::Fresh,     // Filter
    ResultIdentity::Callback,  // Reduce
    ResultIdentity::Scalar,    // Any
    ResultIdentity::Scalar,    // All
    ResultIdentity::Element,   // Find
    ResultIdentity::Scalar,    // FindIndex
    ResultIdentity::Scalar,    // Count
    ResultIdentity::Fresh,     // SortBy
    ResultIdentity::Fresh,     // GroupBy
    ResultIdentity::Fresh,     // IndexBy
    ResultIdentity::Fresh,     // Partition
    ResultIdentity::Fresh,     // UniqueBy
    ResultIdentity::Element,   // MinBy
    ResultIdentity::Element,   // MaxBy
    ResultIdentity::Fresh,     // CountBy
    ResultIdentity::Fresh,     // GroupByEach
};
static_assert(sizeof(kCollectionResults) / sizeof(kCollectionResults[0]) ==
                  static_cast<std::size_t>(CollectionMethod::CountMethods),
              "every CollectionMethod has a classified result identity");

inline ResultIdentity collection_result_identity(CollectionMethod m) {
    return kCollectionResults[static_cast<std::size_t>(m)];
}

// Every mutating entry point of the protocol and the inference's model of it.
struct ProtocolEntry {
    const char* entry;  // the rt/mutation.h function
    const char* model;  // where the inference merges the stored value into the sites
};
inline constexpr ProtocolEntry kProtocolEntries[] = {
    {"check_mutable", "validation only (stage 2.2 of a store the entries below model): no store"},
    {"push", "array push: kCollectionEffects[Push]; the `xs.push(v)` statement (infer_walk.cpp)"},
    {"set", "index store `xs[i] = v` (and compound and increment forms): site_store"},
    {"insert", "kCollectionEffects[Insert]"},
    {"remove", "kCollectionEffects[Remove]"},
    {"pop", "kCollectionEffects[Pop]"},
    {"reverse", "kCollectionEffects[Reverse]"},
    {"splice", "kCollectionEffects[Splice]"},
    {"clear", "kCollectionEffects[Clear]; map/set/stack clear (no stored value)"},
    {"collection_sort_init", "kCollectionEffects[ComparatorSort]"},
    {"collection_plain_sort", "kCollectionEffects[Sort]"},
    {"object_put", "member or index store `o.k = v`, `o[k] = v`: site_store (a non-literal key: every member)"},
    {"object_erase", "no source operation reaches it (no member deletion in the language); positions and keys "
                     "would be dropped by site_store like any store"},
    {"field_set", "a struct field's one slot (B3), shared by every instance and unit"},
    {"map_put", "map contents per site (D-NEXT-refsem, add_contents)"},
    {"set_insert", "set contents per site (D-NEXT-refsem, add_contents)"},
    {"collection_remove", "map/set removal: no stored value (contents stay a superset)"},
    {"stack_push", "stack contents per site (D-NEXT-refsem, add_contents)"},
    {"stack_pop", "stack removal: no stored value (contents stay a superset)"},
};

} // namespace nift::rt::infer_detail
