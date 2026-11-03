#pragma once
// B2 (b2-refrep.md 14.1, review M1): the sort controller's access to the
// protocol's private sort commits. Included only by collection_sort.cpp (the
// comparator sort) and collection_plain_sort.cpp (the plain sort), which call
// them after the sort instruction's own stage 2.2 (vm_collection_sort.cpp,
// vm_array_mutation.cpp); rt-lint's storage-write rule flags this header and
// the name SortController in any other file.
#include "rt/mutation.h"

#include <vector>

namespace nift::rt::mutation {

struct SortController {
    // The comparator sort's working storage at its start (the receiver's own, in place).
    static std::vector<Value>& storage(Value& array) { return Protocol::sort_storage(array); }
    // The same while the controller runs (no relocation, no allocation: the drain).
    static std::vector<Value>& items(Value& array) noexcept { return Protocol::sort_items(array); }
    // The plain sort's single commit of a permutation built on the side.
    static void commit_permutation(Value& array, std::vector<Value>& order) {
        Protocol::commit_permutation(array, order);
    }
};

} // namespace nift::rt::mutation
