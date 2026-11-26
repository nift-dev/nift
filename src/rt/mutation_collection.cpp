#include "rt/mutation_collection.h"

#include "rt/aggregate.h"
#include "rt/natives/collection_methods.h"

// The collection entry points of the mutation protocol (rt/mutation.h),
// split from mutation.cpp by concern (B2): the same stages in the same order.

namespace nift::rt::mutation {

using namespace internal;
using detail::cycle_failure;

namespace {
bool comparable_sorted_key(const Value& collection, const Value& key) {
    if (!collection_sorted(collection) || collection_size(collection) == 0 ||
        collection_find(collection, key) >= 0) return true;
    const Value& first = collection_view(collection).front();
    return (key.is(Kind::Bool) && first.is(Kind::Bool)) ||
           (key.is(Kind::String) && first.is(Kind::String)) ||
           (key.is_number() && first.is_number());
}
} // namespace

std::size_t collection_reserve_bytes(const Value& collection, std::size_t step) noexcept {
    const std::size_t n = collection_item_count(collection);
    const std::size_t capacity = collection.heap_as<CollectionObject>()->items.capacity();
    return n + step > capacity ? 2 * (n + step) * sizeof(Value) + 4 * (n + step) * sizeof(std::uint32_t) : 0;
}

Outcome Protocol::map_put(Value& map, const Value& key, const Value& value, const Env& env) {
    if (!admits_collection(map, CollectionKind::Map))
        return collection_admit_failure(map, CollectionKind::Map, "set", env.receiver_text);
    if (!natives::collection_key_ok(key))
        return failed_text(Failure::Argument, "map: key must be bool, number, or string");
    if (!comparable_sorted_key(map, key))
        return failed_text(Failure::Argument, "sorted_map: incomparable keys");
    if (closes(map, key, env)) return cycle_failure(0);
    if (closes(map, value, env)) return cycle_failure(1);
    if (!reserve(env.heap_limit, collection_reserve_bytes(map, 2))) return budget_failure();
    Storage::map_set(map, key, value);
    return {};
}

Outcome Protocol::set_insert(Value& set, const Value& member, const Env& env) {
    if (!admits_collection(set, CollectionKind::Set))
        return collection_admit_failure(set, CollectionKind::Set, "add", env.receiver_text);
    if (!natives::collection_key_ok(member))
        return failed_text(Failure::Argument, "add: set values must be bool, number, or string");
    if (!comparable_sorted_key(set, member))
        return failed_text(Failure::Argument, "sorted_set: incomparable values");
    if (closes(set, member, env)) return cycle_failure(0);
    if (!reserve(env.heap_limit, collection_reserve_bytes(set, 1))) return budget_failure();
    Storage::set_add(set, member);
    return {};
}

Outcome Protocol::collection_remove(Value& collection, const Value& key, Value* removed, const Env& env) {
    const bool map = collection.is(Kind::Collection) && collection_kind(collection) == CollectionKind::Map;
    const CollectionKind sub = map ? CollectionKind::Map : CollectionKind::Set;
    if (!admits_collection(collection, sub))
        return collection_admit_failure(collection, sub, "remove", env.receiver_text);
    if (!map && !natives::collection_key_ok(key))
        return failed_text(Failure::Argument, "remove: set values must be bool, number, or string");
    const std::ptrdiff_t at = collection_find(collection, key);
    if (at < 0)  // a missing member: no-op
        return map ? failed_text(Failure::Bounds, "remove: key not found") : Outcome{};
    if (removed) *removed = map ? collection_view(collection)[static_cast<std::size_t>(at) * 2 + 1] : key;
    Storage::collection_erase(collection, static_cast<std::size_t>(at));
    return {};
}

Outcome Protocol::stack_push(Value& stack, const Value& item, const Env& env) {
    if (!admits_collection(stack, CollectionKind::Stack))
        return collection_admit_failure(stack, CollectionKind::Stack, "push", env.receiver_text);
    if (closes(stack, item, env)) return cycle_failure(0);
    if (!reserve(env.heap_limit, collection_reserve_bytes(stack, 1))) return budget_failure();
    Storage::stack_push(stack, item);
    return {};
}

Outcome Protocol::stack_pop(Value& stack, Value* removed, const Env& env) {
    if (!admits_collection(stack, CollectionKind::Stack))
        return collection_admit_failure(stack, CollectionKind::Stack, "pop", env.receiver_text);
    if (collection_size(stack) == 0) return failed_text(Failure::Bounds, "pop: collection is empty");
    if (removed) *removed = collection_view(stack).back();
    Storage::stack_pop(stack);
    return {};
}

} // namespace nift::rt::mutation
