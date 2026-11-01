#pragma once
// Weekend collections (D-063): the private bytecode ABI of the map/set/stack
// mutators. The compiler lowers `name.method(args)` on a bare binding to a
// take / KeyedMutate / put sequence over a collection_slot state window
// (rt/collection.h): Source = the collection (moved out of the binding, so an
// unshared collection is mutated in place), Item and Returned = the
// arguments, Result = the method's value.
#include <cstdint>
#include <string_view>

namespace nift::rt {
// Mutators first (take / mutate / put), then the fallible reads (the receiver
// is a copy; nothing is put back). Reads run here rather than as natives so
// their errors get the collection error context (a `return: ` prefix and the
// return statement's position inside a return expression, as legacy).
enum class KeyedMethod : std::uint16_t {
    MapSet, MapRemove, SetAdd, SetRemove, StackPush, StackPop, Clear,
    MapGet, SetContains, StackTop, StackFront, Count
};
inline constexpr KeyedMethod kFirstKeyedRead = KeyedMethod::MapGet;
inline constexpr std::string_view keyed_names[] = {"set", "remove", "add", "remove", "push", "pop", "clear",
                                                   "get", "contains", "top", "front"};
// Arguments each method takes (legacy's arity).
inline constexpr std::uint8_t keyed_arity[] = {2, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0};
} // namespace nift::rt
