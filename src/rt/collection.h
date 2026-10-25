#pragma once
// CP14B private collection bytecode ABI. State is ordinary frame-owned Values.
#include "rt/natives/builtin.h"
#include "rt/vm_ops.h"
#include <string_view>
namespace nift::rt {
enum class CollectionMethod : std::uint16_t {
    Push, Pop, Insert, Remove, Clear, Splice, Reverse, Sort, ComparatorSort,
    Map, Filter, Reduce, Any, All, Find, FindIndex, Count, SortBy, GroupBy,
    IndexBy, Partition, UniqueBy, MinBy, MaxBy, CountBy, GroupByEach, CountMethods
};
inline constexpr std::string_view collection_names[] = {
    "push","pop","insert","remove","clear","splice","reverse","sort","sort",
    "map","filter","reduce","any","all","find","find_index","count","sort_by","group_by",
    "index_by","partition","unique_by","min_by","max_by","count_by","group_by_each"
};
inline int collection_method(std::string_view name) {
    for(std::size_t i=0;i<std::size(collection_names);++i)if(collection_names[i]==name)return static_cast<int>(i);
    return -1;
}
namespace collection_slot {
enum : std::uint16_t { Result, Source, Length, Index, Item, Callback, Returned,
                      Keys, HashTable, More, Selector, Directions, Count };
}
bool collection_step(CollectionMethod method, std::uint16_t phase, Value* state,
                     const natives::Context& context, Fault& fault);
} // namespace nift::rt
