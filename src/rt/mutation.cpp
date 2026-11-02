#include "rt/mutation.h"

#include "rt/aggregate.h"
#include "rt/aggregate_store.h"
#include "rt/heap_account.h"
#include "rt/natives/builtin.h"
#include "rt/natives/collection_methods.h"
#include "rt/natives/support.h"
#include "rt/value_cycle.h"

#include <algorithm>
#include <utility>

// The mutation protocol (rt/mutation.h): stage functions, the array edit and
// sort commits, and the validated entry points. Test-only mutants
// (NIFT_MUTATION_MUTANT, tests/rt/mutation_protocol.cpp): 1 validates push
// after its commit, 2 skips object_put's immutable check, 3 skips the heap
// tally of an array edit's bookkeeping, 4 drops the inline stage 2.2 test
// (admits), 5 narrows the hoisted cycle pre-filter (1, 3 and 4 are in
// mutation.h, always inlined).

namespace nift::rt::mutation {

namespace {

const CycleGraph& default_graph() {
    static const CycleGraph graph;
    return graph;
}

const CycleGraph& graph_of(const Env& env) { return env.graph != nullptr ? *env.graph : default_graph(); }

const char* receiver_kind_name(const Value& receiver) {
    switch (receiver.kind()) {
        case Kind::Array: return "array";
        case Kind::Object: return "object";
        case Kind::Struct: return "struct";
        case Kind::Collection: return collection_kind_name(collection_kind(receiver));
        default: return "value";
    }
}

// The refusals are built out of line (B1-perf): a validated entry point's
// success path then needs no frame for their text.
NIFT_MUTATION_COLD Outcome failed(Failure failure, std::string text) {
    Outcome out;
    out.failure = failure;
    out.text = std::move(text);
    return out;
}

NIFT_MUTATION_COLD Outcome failed_text(Failure failure, const char* text) { return failed(failure, text); }

NIFT_MUTATION_COLD Outcome cycle_failure(std::size_t operand) {
    Outcome out;
    out.failure = Failure::Cycle;
    out.operand = operand;
    return out;
}

NIFT_MUTATION_COLD Outcome budget_failure() { return failed_text(Failure::Budget, "memory budget exceeded"); }

NIFT_MUTATION_COLD Outcome collection_admit_failure(const Value& receiver, CollectionKind sub, const char* method,
                                                    std::string_view receiver_text) {
    if (!receiver.is(Kind::Collection) || collection_kind(receiver) != sub)
        return failed(Failure::Kind, std::string(method) + ": receiver is not a " + collection_kind_name(sub));
    return check_mutable(receiver, receiver_text);
}

NIFT_MUTATION_COLD Outcome no_field_failure(std::string_view receiver_text) {
    return failed(Failure::Bounds, "struct has no field: " + std::string(receiver_text));
}

// 2.1 then 2.2: the receiver kind (and collection sub-kind), then immutability.
NIFT_MUTATION_INLINE bool admits_collection(const Value& receiver, CollectionKind sub) noexcept {
    return receiver.is(Kind::Collection) && collection_kind(receiver) == sub && !refuses_mutation(receiver);
}

// 2.4 for one stored operand. For a struct or collection receiver
// closes_cycle is would_close_cycle, whose first test is the O(1)
// may_contain_identity summary; it is hoisted here, so a scalar or plain
// value makes no call (the same answer).
NIFT_MUTATION_INLINE bool closes(const Value& receiver, const Value& stored, const Env& env) {
#if defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 5
    const bool summary = stored.is(Kind::Array) || stored.is(Kind::Object);  // M-B1e: a narrower pre-filter
#else
    const bool summary = may_contain_identity(stored);
#endif
    return cycle_applies(receiver) && summary && closes_cycle(receiver.heap(), stored, graph_of(env));
}

} // namespace

void make_immutable(Value& value) noexcept {
    if (value.is_heap()) mark_immutable(value.heap());
}

namespace detail {

Outcome immutable_failure(const Value& receiver, std::string_view receiver_text) {
    return failed(Failure::Immutable, std::string("cannot mutate const ") + receiver_kind_name(receiver) + ": " +
                                          std::string(receiver_text));
}

Outcome admit_failure(const Value& receiver, Kind kind, const char* method, const char* want,
                      std::string_view receiver_text) {
    if (!receiver.is(kind)) return failed(Failure::Kind, std::string(method) + ": receiver is not " + want);
    return check_mutable(receiver, receiver_text);
}

Outcome bounds_failure(const char* text) { return failed(Failure::Bounds, text); }

bool reserve_slow(std::size_t heap_limit, std::size_t bytes) noexcept {
    if (bytes == static_cast<std::size_t>(-1)) return false;
    return natives::reserve_within(heap_limit, bytes);  // reserve_result's check, without building a Context
}

} // namespace detail

bool cycle_applies(const Value& receiver) noexcept {
    return receiver.is(Kind::Struct) || receiver.is(Kind::Collection);
}

bool closes_cycle(const HeapObject* receiver, const Value& stored, const CycleGraph& graph) {
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 5
    (void)receiver; (void)stored; (void)graph;
    return false;  // R5: no cycle check (the stored value may close a cycle and leak)
#else
    const CycleTarget target = CycleTarget::aggregate(receiver);
    if (receiver->kind() == Kind::Array || receiver->kind() == Kind::Object)  // B2's targets: no summary gate
        return (stored.is(Kind::Array) || stored.is(Kind::Object) || may_contain_identity(stored)) &&
               would_close_cycle_walk(target, stored, graph);
    return would_close_cycle(target, stored, graph);
#endif
}

std::size_t collection_reserve_bytes(const Value& collection, std::size_t step) noexcept {
    const std::size_t n = collection_item_count(collection);
    const std::size_t capacity = collection.heap_as<CollectionObject>()->items.capacity();
    return n + step > capacity ? 2 * (n + step) * sizeof(Value) + 4 * (n + step) * sizeof(std::uint32_t) : 0;
}

void commit_permutation(Value& array, std::vector<Value>& order) {
    auto& items = array_mut(array);
    const std::size_t n = items.size();
    for (std::size_t i = 0; i < n; ++i) {  // follow each cycle of the permutation once
        if (static_cast<std::size_t>(order[i].as_int()) == i) continue;
        Value value = std::move(items[i]);
        std::size_t at = i;
        while (static_cast<std::size_t>(order[at].as_int()) != i) {
            const auto next = static_cast<std::size_t>(order[at].as_int());
            items[at] = std::move(items[next]);
            order[at] = Value::integer(static_cast<std::int64_t>(at));
            at = next;
        }
        items[at] = std::move(value);
        order[at] = Value::integer(static_cast<std::int64_t>(at));
    }
    heap_retally(array.heap());
}

std::vector<Value>& sort_storage(Value& array) { return array_mut(array); }

Outcome insert(Value& array, std::size_t index, const Value& item, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "insert", "an array", env.receiver_text);
    if (index > array_size(array)) return failed_text(Failure::Bounds, "insert: index out of range");
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 1))) return budget_failure();
    commit_array(array, ArrayEdit::Insert, index, 0, item);
    return {};
}

Outcome remove(Value& array, std::size_t index, Value* removed, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "remove", "an array", env.receiver_text);
    if (index >= array_size(array)) return failed_text(Failure::Bounds, "remove: index out of range");
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 0))) return budget_failure();
    if (removed) *removed = array_view(array)[index];
    commit_array(array, ArrayEdit::Remove, index, 0, Value());
    return {};
}

Outcome pop(Value& array, Value* removed, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "pop", "an array", env.receiver_text);
    if (array_size(array) == 0) return failed_text(Failure::Bounds, "pop: array is empty");
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 0))) return budget_failure();
    if (removed) *removed = array_view(array).back();
    commit_array(array, ArrayEdit::Pop, 0, 0, Value());
    return {};
}

Outcome reverse(Value& array, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "reverse", "an array", env.receiver_text);
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 0))) return budget_failure();
    commit_array(array, ArrayEdit::Reverse, 0, 0, Value());
    return {};
}

// One commit: the result is built on the side and swapped in.
Outcome splice(Value& array, std::size_t index, std::size_t count, const Value& replacement, Value* removed,
               const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "splice", "an array", env.receiver_text);
    if (!replacement.is(Kind::Array)) return failed_text(Failure::Argument, "splice: replacements must be an array");
    const auto& items = array_view(array);
    const std::size_t n = items.size();
    index = std::min(index, n);
    count = std::min(count, n - index);
    const std::size_t added = array_size(replacement);
    const std::size_t growth = array_reserve_bytes(array, added);
    if (growth == static_cast<std::size_t>(-1) ||
        !reserve(env.heap_limit, growth + (n + added) * sizeof(Value) + 128))
        return budget_failure();
    std::vector<Value> side;
    side.reserve(n - count + added);
    Value cut = make_array(count);
    for (std::size_t k = 0; k < index; ++k) side.push_back(items[k]);
    for (const Value& v : array_view(replacement)) side.push_back(v);
    for (std::size_t k = index; k < n; ++k) {
        if (k < index + count) array_push(cut, items[k]);
        else side.push_back(items[k]);
    }
    array_mut(array).swap(side);  // the swap: the receiver's own storage, identity kept
    heap_retally(array.heap());
    if (removed) *removed = std::move(cut);
    return {};
}

Outcome object_put(Value& object, Atom key, const Value& value, const Env& env) {
#if defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 2
    if (!object.is(Kind::Object)) return failed_text(Failure::Kind, "set: receiver is not an object");  // M-B1b
#else
    if (!admits(object, Kind::Object))
        return detail::admit_failure(object, Kind::Object, "set", "an object", env.receiver_text);
#endif
    if (closes(object, value, env)) return cycle_failure(0);
    object_set(object, key, value);
    return {};
}

Outcome object_erase(Value& object, Atom key, const Env& env) {
    if (!admits(object, Kind::Object))
        return detail::admit_failure(object, Kind::Object, "remove", "an object", env.receiver_text);
    if (object_find(object, key) == nullptr) return failed_text(Failure::Bounds, "remove: key not found");
    object_remove(object, key);
    return {};
}

Outcome field_set(Value& instance, Atom field, const Value& value, const Env& env) {
    if (!admits(instance, Kind::Struct))
        return detail::admit_failure(instance, Kind::Struct, "field", "a struct", env.receiver_text);
    if (struct_get(instance, field) == nullptr)
        return no_field_failure(env.receiver_text);
    if (closes(instance, value, env)) return cycle_failure(0);
    struct_set(instance, field, value);
    return {};
}

Outcome map_put(Value& map, const Value& key, const Value& value, const Env& env) {
    if (!admits_collection(map, CollectionKind::Map))
        return collection_admit_failure(map, CollectionKind::Map, "set", env.receiver_text);
    if (!natives::collection_key_ok(key))
        return failed_text(Failure::Argument, "map: key must be bool, number, or string");
    if (closes(map, key, env)) return cycle_failure(0);
    if (closes(map, value, env)) return cycle_failure(1);
    if (!reserve(env.heap_limit, collection_reserve_bytes(map, 2))) return budget_failure();
    map_set(map, key, value);
    return {};
}

Outcome set_insert(Value& set, const Value& member, const Env& env) {
    if (!admits_collection(set, CollectionKind::Set))
        return collection_admit_failure(set, CollectionKind::Set, "add", env.receiver_text);
    if (!natives::collection_key_ok(member))
        return failed_text(Failure::Argument, "add: set values must be bool, number, or string");
    if (closes(set, member, env)) return cycle_failure(0);
    if (!reserve(env.heap_limit, collection_reserve_bytes(set, 1))) return budget_failure();
    set_add(set, member);
    return {};
}

Outcome collection_remove(Value& collection, const Value& key, Value* removed, const Env& env) {
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
    collection_erase(collection, static_cast<std::size_t>(at));
    return {};
}

Outcome stack_push(Value& stack, const Value& item, const Env& env) {
    if (!admits_collection(stack, CollectionKind::Stack))
        return collection_admit_failure(stack, CollectionKind::Stack, "push", env.receiver_text);
    if (closes(stack, item, env)) return cycle_failure(0);
    if (!reserve(env.heap_limit, collection_reserve_bytes(stack, 1))) return budget_failure();
    rt::stack_push(stack, item);
    return {};
}

Outcome stack_pop(Value& stack, Value* removed, const Env& env) {
    if (!admits_collection(stack, CollectionKind::Stack))
        return collection_admit_failure(stack, CollectionKind::Stack, "pop", env.receiver_text);
    if (collection_size(stack) == 0) return failed_text(Failure::Bounds, "pop: collection is empty");
    if (removed) *removed = collection_view(stack).back();
    rt::stack_pop(stack);
    return {};
}

Outcome clear(Value& receiver, const Env& env) {
    if (receiver.is(Kind::Collection)) {
        Outcome out = check_mutable(receiver, env.receiver_text);
        if (out.ok()) collection_clear(receiver);
        return out;
    }
    if (!admits(receiver, Kind::Array))
        return detail::admit_failure(receiver, Kind::Array, "clear", "an array", env.receiver_text);
    if (!reserve(env.heap_limit, array_reserve_bytes(receiver, 0))) return budget_failure();
    commit_array(receiver, ArrayEdit::Clear, 0, 0, Value());
    return {};
}

} // namespace nift::rt::mutation
