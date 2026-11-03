#pragma once
// B2 (b2-refrep.md 14.1, 17.2, re-review C1): the sort controller's access to
// the protocol's private sort commits, closed at compile time by a passkey.
// SortController's functions require a SortKey, whose only constructor is
// private (user-provided, so SortKey is not an aggregate and `SortKey{}` does
// not compile) and whose only friends are the three sort functions that run
// after the sort instruction's own stage 2.2 (vm_collection_sort.cpp,
// vm_array_mutation.cpp). Including this header grants nothing else
// (tests/rt/compile_fail: bypass_sort_*.cpp).
#include "rt/collection_plain_sort.h"
#include "rt/collection_sort.h"
#include "rt/mutation.h"

#include <vector>

namespace nift::rt::mutation {

class SortKey {
    SortKey() {}  // user-provided and private: only the friends below can make one
    friend bool nift::rt::collection_sort_init(Value*, const natives::Context&, Fault&, std::size_t);
    friend bool nift::rt::collection_sort_resume(Value*, bool, bool);
    friend bool nift::rt::collection_plain_sort(Value*, const natives::Context&, Fault&);
};

struct SortController {
    // The comparator sort's working storage at its start (the receiver's own, in place).
    static std::vector<Value>& storage(const SortKey&, Value& array) { return Protocol::sort_storage(array); }
    // The same while the controller runs (no relocation, no allocation: the drain).
    static std::vector<Value>& items(const SortKey&, Value& array) noexcept { return Protocol::sort_items(array); }
    // The plain sort's single commit of a permutation built on the side.
    static void commit_permutation(const SortKey&, Value& array, std::vector<Value>& order) {
        Protocol::commit_permutation(array, order);
    }
};

} // namespace nift::rt::mutation
