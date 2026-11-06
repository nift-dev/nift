#pragma once
// B2 (b2-refrep.md 14.1, 17.2, 18.1, 19.1): the sort controller's named
// access to the protocol's sort commits. Each requires a mutation::SortKey
// (rt/mutation.h: only the three sort functions can make one), so including
// this header grants nothing (tests/rt/compile_fail: bypass_sort_*.cpp).
#include "rt/collection_plain_sort.h"
#include "rt/collection_sort.h"
#include "rt/mutation.h"

#include <vector>

namespace nift::rt::mutation {

struct SortController {
    // The comparator sort's working storage at its start (the receiver's own, in place).
    static std::vector<Value>& storage(const SortKey& key, Value& array) { return Protocol::sort_storage(key, array); }
    // The same while the controller runs (no relocation, no allocation: the drain).
    static std::vector<Value>& items(const SortKey& key, Value& array) noexcept {
        return Protocol::sort_items(key, array);
    }
    // B5 (Q7): the comparator sort's single commit of its sorted working copy.
    static void commit_sorted(const SortKey& key, Value& array, std::vector<Value>& sorted) {
        Protocol::commit_sorted(key, array, sorted);
    }
    // The plain sort's single commit of a permutation built on the side.
    static void commit_permutation(const SortKey& key, Value& array, std::vector<Value>& order) {
        Protocol::commit_permutation(key, array, order);
    }
};

} // namespace nift::rt::mutation
