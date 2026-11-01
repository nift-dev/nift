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
// tally of an array edit's bookkeeping.

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

Outcome failed(Failure failure, std::string text) {
    Outcome out;
    out.failure = failure;
    out.text = std::move(text);
    return out;
}

Outcome cycle_failure(std::size_t operand) {
    Outcome out;
    out.failure = Failure::Cycle;
    out.operand = operand;
    return out;
}

Outcome budget_failure() { return failed(Failure::Budget, "memory budget exceeded"); }

// 2.1 then 2.2: the receiver kind (and collection sub-kind), then immutability.
Outcome admit(const Value& receiver, Kind kind, const char* method, const char* want, const Env& env) {
    if (!receiver.is(kind)) return failed(Failure::Kind, std::string(method) + ": receiver is not " + want);
    return check_mutable(receiver, env.receiver_text);
}

Outcome admit_collection(const Value& receiver, CollectionKind sub, const char* method, const Env& env) {
    if (!receiver.is(Kind::Collection) || collection_kind(receiver) != sub)
        return failed(Failure::Kind, std::string(method) + ": receiver is not a " + collection_kind_name(sub));
    return check_mutable(receiver, env.receiver_text);
}

// 2.4 for one stored operand.
bool closes(const Value& receiver, const Value& stored, const Env& env) {
    return cycle_applies(receiver) && closes_cycle(receiver.heap(), stored, graph_of(env));
}

} // namespace

bool is_immutable(const Value& value) noexcept { return value.is_heap() && rt::is_immutable(value.heap()); }

void make_immutable(Value& value) noexcept {
    if (value.is_heap()) mark_immutable(value.heap());
}

bool writes_in_place(const Value& receiver) noexcept {
    if (receiver.is(Kind::Array) || receiver.is(Kind::Object)) return receiver.refcount() <= 1;
    return true;
}

Outcome check_mutable(const Value& receiver, std::string_view receiver_text) {
    if (!is_immutable(receiver) || !writes_in_place(receiver)) return {};
    return failed(Failure::Immutable, std::string("cannot mutate const ") + receiver_kind_name(receiver) + ": " +
                                          std::string(receiver_text));
}

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

std::size_t array_reserve_bytes(const Value& array, std::size_t growth) noexcept {
    const std::size_t n = array_size(array);
    if (growth > static_cast<std::size_t>(-1) / (4 * sizeof(Value)) - n) return static_cast<std::size_t>(-1);
    const std::size_t capacity = array.heap_as<ArrayObject>()->items.capacity();
    const std::size_t clone = array.refcount() > 1 ? n : 0;
    const std::size_t reserve = clone + (growth && n + growth > capacity ? 2 * (n + growth) : 0);
    return reserve ? reserve * sizeof(Value) + 128 : 0;
}

std::size_t collection_reserve_bytes(const Value& collection, std::size_t step) noexcept {
    const std::size_t n = collection_item_count(collection);
    const std::size_t capacity = collection.heap_as<CollectionObject>()->items.capacity();
    return n + step > capacity ? 2 * (n + step) * sizeof(Value) + 4 * (n + step) * sizeof(std::uint32_t) : 0;
}

bool reserve(std::size_t heap_limit, std::size_t bytes) noexcept {
    if (bytes == 0) return true;
    if (bytes == static_cast<std::size_t>(-1)) return false;
    natives::Context context;
    context.heap_limit = heap_limit;
    return natives::reserve_result(context, bytes);
}

void commit_array(Value& array, ArrayEdit edit, std::size_t index, std::size_t count, const Value& operand) {
    const Value keep = operand;  // the operand may share the receiver's object (a.splice(0, 0, a))
    auto& items = array_mut(array);  // the copy-on-write clone, or the D9 relocation
    switch (edit) {
        case ArrayEdit::Pop: items.pop_back(); break;
        case ArrayEdit::Remove: items.erase(items.begin() + static_cast<std::ptrdiff_t>(index)); break;
        case ArrayEdit::Insert: items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), keep); break;
        case ArrayEdit::Clear: items.clear(); break;
        case ArrayEdit::Reverse: std::reverse(items.begin(), items.end()); break;
        case ArrayEdit::SpliceRemove: {
            const auto from = items.begin() + static_cast<std::ptrdiff_t>(index);
            items.erase(from, from + static_cast<std::ptrdiff_t>(count));
            break;
        }
        case ArrayEdit::SpliceInsert: {
            const auto& replacement = array_view(keep);
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), replacement.begin(), replacement.end());
            break;
        }
    }
#if !(defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 3)
    heap_retally(array.heap());  // M-B1c drops this: the edit's growth is never counted
#endif
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

Outcome push(Value& array, const Value& item, const Env& env) {
#if defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 1
    if (array.is(Kind::Array)) array_push(array, item);  // M-B1a: the commit before the validation
#endif
    Outcome out = admit(array, Kind::Array, "push", "an array", env);
    if (!out.ok()) return out;
#if !(defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 1)
    array_push(array, item);
#endif
    return out;
}

Outcome set(Value& array, std::size_t index, const Value& item, const Env& env) {
    Outcome out = admit(array, Kind::Array, "set", "an array", env);
    if (!out.ok()) return out;
    if (index >= array_size(array)) return failed(Failure::Bounds, "set: index out of range");
    array_set(array, index, item);
    return out;
}

Outcome insert(Value& array, std::size_t index, const Value& item, const Env& env) {
    Outcome out = admit(array, Kind::Array, "insert", "an array", env);
    if (!out.ok()) return out;
    if (index > array_size(array)) return failed(Failure::Bounds, "insert: index out of range");
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 1))) return budget_failure();
    commit_array(array, ArrayEdit::Insert, index, 0, item);
    return out;
}

Outcome remove(Value& array, std::size_t index, Value* removed, const Env& env) {
    Outcome out = admit(array, Kind::Array, "remove", "an array", env);
    if (!out.ok()) return out;
    if (index >= array_size(array)) return failed(Failure::Bounds, "remove: index out of range");
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 0))) return budget_failure();
    if (removed) *removed = array_view(array)[index];
    commit_array(array, ArrayEdit::Remove, index, 0, Value());
    return out;
}

Outcome pop(Value& array, Value* removed, const Env& env) {
    Outcome out = admit(array, Kind::Array, "pop", "an array", env);
    if (!out.ok()) return out;
    if (array_size(array) == 0) return failed(Failure::Bounds, "pop: array is empty");
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 0))) return budget_failure();
    if (removed) *removed = array_view(array).back();
    commit_array(array, ArrayEdit::Pop, 0, 0, Value());
    return out;
}

Outcome reverse(Value& array, const Env& env) {
    Outcome out = admit(array, Kind::Array, "reverse", "an array", env);
    if (!out.ok()) return out;
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 0))) return budget_failure();
    commit_array(array, ArrayEdit::Reverse, 0, 0, Value());
    return out;
}

// One commit: the result is built on the side and swapped in.
Outcome splice(Value& array, std::size_t index, std::size_t count, const Value& replacement, Value* removed,
               const Env& env) {
    Outcome out = admit(array, Kind::Array, "splice", "an array", env);
    if (!out.ok()) return out;
    if (!replacement.is(Kind::Array)) return failed(Failure::Argument, "splice: replacements must be an array");
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
    return out;
}

Outcome object_put(Value& object, Atom key, const Value& value, const Env& env) {
#if defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 2
    if (!object.is(Kind::Object)) return failed(Failure::Kind, "set: receiver is not an object");  // M-B1b
    Outcome out;
#else
    Outcome out = admit(object, Kind::Object, "set", "an object", env);
    if (!out.ok()) return out;
#endif
    if (closes(object, value, env)) return cycle_failure(0);
    object_set(object, key, value);
    return out;
}

Outcome object_erase(Value& object, Atom key, const Env& env) {
    Outcome out = admit(object, Kind::Object, "remove", "an object", env);
    if (!out.ok()) return out;
    if (object_find(object, key) == nullptr) return failed(Failure::Bounds, "remove: key not found");
    object_remove(object, key);
    return out;
}

Outcome field_set(Value& instance, Atom field, const Value& value, const Env& env) {
    Outcome out = admit(instance, Kind::Struct, "field", "a struct", env);
    if (!out.ok()) return out;
    if (struct_get(instance, field) == nullptr)
        return failed(Failure::Bounds, "struct has no field: " + std::string(env.receiver_text));
    if (closes(instance, value, env)) return cycle_failure(0);
    struct_set(instance, field, value);
    return out;
}

Outcome map_put(Value& map, const Value& key, const Value& value, const Env& env) {
    Outcome out = admit_collection(map, CollectionKind::Map, "set", env);
    if (!out.ok()) return out;
    if (!natives::collection_key_ok(key)) return failed(Failure::Argument, "map: key must be bool, number, or string");
    if (closes(map, key, env)) return cycle_failure(0);
    if (closes(map, value, env)) return cycle_failure(1);
    if (!reserve(env.heap_limit, collection_reserve_bytes(map, 2))) return budget_failure();
    map_set(map, key, value);
    return out;
}

Outcome set_insert(Value& set, const Value& member, const Env& env) {
    Outcome out = admit_collection(set, CollectionKind::Set, "add", env);
    if (!out.ok()) return out;
    if (!natives::collection_key_ok(member))
        return failed(Failure::Argument, "add: set values must be bool, number, or string");
    if (closes(set, member, env)) return cycle_failure(0);
    if (!reserve(env.heap_limit, collection_reserve_bytes(set, 1))) return budget_failure();
    set_add(set, member);
    return out;
}

Outcome collection_remove(Value& collection, const Value& key, Value* removed, const Env& env) {
    const bool map = collection.is(Kind::Collection) && collection_kind(collection) == CollectionKind::Map;
    Outcome out = admit_collection(collection, map ? CollectionKind::Map : CollectionKind::Set, "remove", env);
    if (!out.ok()) return out;
    if (!map && !natives::collection_key_ok(key))
        return failed(Failure::Argument, "remove: set values must be bool, number, or string");
    const std::ptrdiff_t at = collection_find(collection, key);
    if (at < 0) return map ? failed(Failure::Bounds, "remove: key not found") : out;  // a missing member: no-op
    if (removed) *removed = map ? collection_view(collection)[static_cast<std::size_t>(at) * 2 + 1] : key;
    collection_erase(collection, static_cast<std::size_t>(at));
    return out;
}

Outcome stack_push(Value& stack, const Value& item, const Env& env) {
    Outcome out = admit_collection(stack, CollectionKind::Stack, "push", env);
    if (!out.ok()) return out;
    if (closes(stack, item, env)) return cycle_failure(0);
    if (!reserve(env.heap_limit, collection_reserve_bytes(stack, 1))) return budget_failure();
    rt::stack_push(stack, item);
    return out;
}

Outcome stack_pop(Value& stack, Value* removed, const Env& env) {
    Outcome out = admit_collection(stack, CollectionKind::Stack, "pop", env);
    if (!out.ok()) return out;
    if (collection_size(stack) == 0) return failed(Failure::Bounds, "pop: collection is empty");
    if (removed) *removed = collection_view(stack).back();
    rt::stack_pop(stack);
    return out;
}

Outcome clear(Value& receiver, const Env& env) {
    if (receiver.is(Kind::Collection)) {
        Outcome out = check_mutable(receiver, env.receiver_text);
        if (out.ok()) collection_clear(receiver);
        return out;
    }
    Outcome out = admit(receiver, Kind::Array, "clear", "an array", env);
    if (!out.ok()) return out;
    if (!reserve(env.heap_limit, array_reserve_bytes(receiver, 0))) return budget_failure();
    commit_array(receiver, ArrayEdit::Clear, 0, 0, Value());
    return out;
}

} // namespace nift::rt::mutation
