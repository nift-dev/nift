#include "rt/mutation.h"

#include <cctype>
#include "rt/mutation_collection.h"

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
// sort commits, and the validated entry points for arrays, objects and
// structs (collections: mutation_collection.cpp). Test-only mutants
// (NIFT_MUTATION_MUTANT, tests/rt/mutation_protocol.cpp): 1 validates push
// after its commit, 2 skips object_put's immutable check, 3 skips the heap
// tally of an array edit's bookkeeping, 4 drops the inline stage 2.2 test
// (admits), 5 narrows the hoisted cycle pre-filter (1, 3 and 4 are in
// mutation.h, always inlined).

namespace nift::rt::mutation {

namespace internal {

// The refusals are built out of line (B1-perf): a validated entry point's
// success path then needs no frame for their text.
Outcome failed(Failure failure, std::string text) {
    Outcome out;
    out.failure = failure;
    out.text = std::move(text);
    return out;
}

Outcome failed_text(Failure failure, const char* text) { return failed(failure, text); }

Outcome budget_failure() { return failed_text(Failure::Budget, "memory budget exceeded"); }

Outcome collection_admit_failure(const Value& receiver, CollectionKind sub, const char* method,
                                 std::string_view receiver_text) {
    if (!receiver.is(Kind::Collection) || collection_kind(receiver) != sub)
        return failed(Failure::Kind, std::string(method) + ": receiver is not a " + collection_kind_name(sub));
    return check_mutable(receiver, receiver_text);
}

} // namespace internal

using namespace internal;

namespace {

const CycleGraph& default_graph() {
    static const CycleGraph graph;
    return graph;
}


const char* receiver_kind_name(const Value& receiver) {
    switch (receiver.kind()) {
        case Kind::Array: return "array";
        case Kind::Object: return "object";
        case Kind::Struct: return "struct";
        case Kind::Collection: return collection_kind_name(collection_kind(receiver));
        default: return "value";
    }
}

NIFT_MUTATION_COLD Outcome no_field_failure(std::string_view receiver_text) {
    return failed(Failure::Bounds, "struct has no field: " + std::string(receiver_text));
}

using detail::cycle_failure;

// 2.4 for the elements an array operand contributes (splice's replacement):
// the replacement array is not stored, only its elements, so the receiver
// itself as the replacement (a.splice(0, 0, a)) closes no cycle.
bool elements_close(const Value& receiver, const Value& replacement, const Env& env) {
    if (!replacement.is(Kind::Array) || replacement.heap() == receiver.heap()) return false;
    for (const Value& item : array_view(replacement))
        if (closes(receiver, item, env)) return true;
    return false;
}

} // namespace

void make_immutable(Value& value) noexcept {
    if (value.is_heap()) mark_immutable(value.heap());
}

namespace detail {

namespace {
// B5 (owner Q1): the error names the receiver expression (`b` in `b.push(1)`), legacy's
// "cannot mutate const array: args". A mutator call's note is its call text (the malformed-
// expression diagnostic's); its receiver is the text before the method's `.`.
std::string_view receiver_of_call(std::string_view text) {
    if (text.empty() || text.back() != ')') return text;
    int depth = 0;
    std::size_t k = text.size();
    while (k-- > 0) {
        const char c = text[k];
        if (c == ')') ++depth;
        else if (c == '(' && --depth == 0) break;
        else if (c == '"') {  // a string argument: skip to its opening quote
            while (k-- > 0 && !(text[k] == '"' && (k == 0 || text[k - 1] != '\\'))) {}
        }
    }
    if (k == static_cast<std::size_t>(-1) || k == 0) return text;
    std::size_t name = k;
    while (name > 0 && (std::isalnum(static_cast<unsigned char>(text[name - 1])) || text[name - 1] == '_')) --name;
    if (name == k || name == 0 || text[name - 1] != '.') return text;
    return text.substr(0, name - 1);
}
} // namespace

Outcome immutable_failure(const Value& receiver, std::string_view receiver_text_in) {
    const std::string_view receiver_text = receiver_of_call(receiver_text_in);
    if (!is_immutable(receiver) && receiver.is_heap() && is_sort_protected(receiver.heap()))
#if defined(NIFT_SORT_WORDING_TEST)  // review M2 control: other words, same kind (classification never reads text)
        return failed(Failure::SortProtected, "array busy: a comparator sort owns it");
#else
        return failed(Failure::SortProtected, kSortReceiverMutated);
#endif
    return failed(Failure::Immutable, std::string("cannot mutate const ") + receiver_kind_name(receiver) + ": " +
                                          std::string(receiver_text));
}

Outcome admit_failure(const Value& receiver, Kind kind, const char* method, const char* want,
                      std::string_view receiver_text) {
    if (!receiver.is(kind)) return failed(Failure::Kind, std::string(method) + ": receiver is not " + want);
    return check_mutable(receiver, receiver_text);
}

Outcome bounds_failure(const char* text) { return failed(Failure::Bounds, text); }

Outcome cycle_failure(std::size_t operand) {
    Outcome out;
    out.failure = Failure::Cycle;
    out.operand = operand;
    return out;
}

bool reserve_slow(std::size_t heap_limit, std::size_t bytes) noexcept {
    if (bytes == static_cast<std::size_t>(-1)) return false;
    return natives::reserve_within(heap_limit, bytes);  // reserve_result's check, without building a Context
}

} // namespace detail

// B2: every aggregate receiver (R21 restores the B1 transition rule: array
// and object receivers skip the check, as while they were copied).
bool cycle_applies(const Value& receiver) noexcept {
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 21
    return receiver.is(Kind::Struct) || receiver.is(Kind::Collection);
#else
    (void)receiver;
    return true;
#endif
}

bool closes_cycle(const HeapObject* receiver, const Value& stored, const CycleGraph* graph) {
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 5
    (void)receiver; (void)stored; (void)graph;
    return false;  // R5: no cycle check (the stored value may close a cycle and leak)
#else
    if (stored.is_heap() && stored.heap() == receiver) return true;  // a.push(a)
    return would_close_cycle_walk(CycleTarget::aggregate(receiver), stored, graph ? *graph : default_graph());
#endif
}


void Protocol::commit_permutation(const SortKey&, Value& array, std::vector<Value>& order) {
    auto& items = Storage::array_mut(array);
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

std::vector<Value>& Protocol::sort_storage(const SortKey&, Value& array) { return Storage::array_mut(array); }

void Protocol::commit_sorted(const SortKey&, Value& array, std::vector<Value>& sorted) {
    auto& items = Storage::array_mut(array);
    if (items.size() != sorted.size()) return;  // never: the receiver is protected while the sort runs
    for (std::size_t i = 0; i < items.size(); ++i) std::swap(items[i], sorted[i]);
    heap_retally(array.heap());
}

// Stage 3 + 4 for an array edit: one structural change, then the heap tally.
void Protocol::commit_array(Value& array, ArrayEdit edit, std::size_t index, std::size_t count,
                            const Value& operand) {
    Value keep = operand;  // the operand may share the receiver's object (a.splice(0, 0, a))
    auto& items = Storage::array_mut(array);  // in place, after the checked-build D9 relocation
    switch (edit) {
        case ArrayEdit::Pop: items.pop_back(); break;
        case ArrayEdit::Remove: items.erase(items.begin() + static_cast<std::ptrdiff_t>(index)); break;
        case ArrayEdit::Insert:  // moved in: keep is already the copy insert(const&) would make
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), static_cast<Value&&>(keep));
            break;
        case ArrayEdit::Clear: items.clear(); break;
        case ArrayEdit::Reverse: std::reverse(items.begin(), items.end()); break;
        case ArrayEdit::SpliceRemove: {
            const auto from = items.begin() + static_cast<std::ptrdiff_t>(index);
            items.erase(from, from + static_cast<std::ptrdiff_t>(count));
            break;
        }
        case ArrayEdit::SpliceInsert: {
            if (keep.heap() == array.heap()) {  // a.splice(i, 0, a): never insert a vector's range into itself
                const std::vector<Value> own = items;
                items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), own.begin(), own.end());
                break;
            }
            const auto& replacement = array_view(keep);
            items.insert(items.begin() + static_cast<std::ptrdiff_t>(index), replacement.begin(), replacement.end());
            break;
        }
    }
#if !(defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 3)
    heap_retally(array.heap());  // M-B1c drops this: the edit's growth is never counted
#endif
}

Outcome Protocol::insert(Value& array, std::size_t index, const Value& item, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "insert", "an array", env.receiver_text);
    if (index > array_size(array)) return failed_text(Failure::Bounds, "insert: index out of range");
    if (closes(array, item, env)) return cycle_failure(0);
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 1))) return budget_failure();
    commit_array(array, ArrayEdit::Insert, index, 0, item);
    return {};
}

Outcome Protocol::remove(Value& array, std::size_t index, Value* removed, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "remove", "an array", env.receiver_text);
    if (index >= array_size(array)) return failed_text(Failure::Bounds, "remove: index out of range");
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 0))) return budget_failure();
    if (removed) *removed = array_view(array)[index];
    commit_array(array, ArrayEdit::Remove, index, 0, Value());
    return {};
}

Outcome Protocol::pop(Value& array, Value* removed, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "pop", "an array", env.receiver_text);
    if (array_size(array) == 0) return failed_text(Failure::Bounds, "pop: array is empty");
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 0))) return budget_failure();
    if (removed) *removed = array_view(array).back();
    commit_array(array, ArrayEdit::Pop, 0, 0, Value());
    return {};
}

Outcome Protocol::reverse(Value& array, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "reverse", "an array", env.receiver_text);
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 0))) return budget_failure();
    commit_array(array, ArrayEdit::Reverse, 0, 0, Value());
    return {};
}

// The removal half: stages 2.1-2.3 (the replacement's kind and, 2.4, its
// elements, when given: the VM's c = 2 and the one-call splice validate the
// replacement before anything changes), then the removal.
Outcome Protocol::splice_remove(Value& array, std::size_t index, std::size_t count, const Value* replacement,
                                Value* removed, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "splice", "an array", env.receiver_text);
    if (replacement && !replacement->is(Kind::Array))
        return failed_text(Failure::Argument, "splice: replacements must be an array");
    if (replacement && elements_close(array, *replacement, env)) return cycle_failure(0);
    const std::size_t n = array_size(array);
    index = std::min(index, n);
    count = std::min(count, n - index);
    if (removed) {
        build::Array cut(count);
        for (std::size_t k = 0; k < count; ++k) cut.push(array_view(array)[index + k]);
        *removed = std::move(cut).publish();
    }
    if (!reserve(env.heap_limit, array_reserve_bytes(array, 0))) return budget_failure();
    commit_array(array, ArrayEdit::SpliceRemove, index, count, Value());
    return {};
}

// The insertion half: the replacement's elements at `index` (<= size).
Outcome Protocol::splice_insert(Value& array, std::size_t index, const Value& replacement, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "splice", "an array", env.receiver_text);
    if (!replacement.is(Kind::Array)) return failed_text(Failure::Argument, "splice: replacements must be an array");
    if (index > array_size(array)) return failed_text(Failure::Bounds, "splice: index out of range");
    if (elements_close(array, replacement, env)) return cycle_failure(0);
    const std::size_t growth = array_reserve_bytes(array, array_size(replacement));
    if (!reserve(env.heap_limit, growth)) return budget_failure();
    commit_array(array, ArrayEdit::SpliceInsert, index, 0, replacement);
    return {};
}

// Every stage for the whole splice first (the insertion's budget included),
// then the two commits: a refused splice changes nothing.
Outcome Protocol::splice(Value& array, std::size_t index, std::size_t count, const Value& replacement,
                         Value* removed, const Env& env) {
    if (!admits(array, Kind::Array))
        return detail::admit_failure(array, Kind::Array, "splice", "an array", env.receiver_text);
    if (!replacement.is(Kind::Array)) return failed_text(Failure::Argument, "splice: replacements must be an array");
    if (elements_close(array, replacement, env)) return cycle_failure(0);
    const auto& items = array_view(array);
    const std::size_t n = items.size();
    index = std::min(index, n);
    count = std::min(count, n - index);
    const std::size_t added = array_size(replacement);
    const std::size_t growth = array_reserve_bytes(array, added);
    if (growth == static_cast<std::size_t>(-1) ||
        !reserve(env.heap_limit, growth + (n + added) * sizeof(Value) + 128))
        return budget_failure();
    // The result is built on the side (every allocation before any change:
    // owner D's strong guarantee) and swapped in. The receiver itself may be
    // the replacement (a.splice(i, k, a)): only its elements before the
    // removal are read, never written while read.
    std::vector<Value> side;
    side.reserve(n - count + added);
    build::Array cut(count);
    for (std::size_t k = 0; k < index; ++k) side.push_back(items[k]);
    for (const Value& v : array_view(replacement)) side.push_back(v);
    for (std::size_t k = index; k < n; ++k) {
        if (k < index + count) cut.push(items[k]);
        else side.push_back(items[k]);
    }
    Storage::array_mut(array).swap(side);  // the receiver's own storage, identity kept
    heap_retally(array.heap());
    if (removed) *removed = std::move(cut).publish();
    return {};
}

Outcome Protocol::object_put(Value& object, Atom key, const Value& value, const Env& env) {
#if defined(NIFT_MUTATION_MUTANT) && NIFT_MUTATION_MUTANT == 2
    if (!object.is(Kind::Object)) return failed_text(Failure::Kind, "set: receiver is not an object");  // M-B1b
#else
    if (!admits(object, Kind::Object))
        return detail::admit_failure(object, Kind::Object, "set", "an object", env.receiver_text);
#endif
    if (closes(object, value, env)) return cycle_failure(0);
    Storage::object_set(object, key, value);
    return {};
}

Outcome Protocol::object_erase(Value& object, Atom key, const Env& env) {
    if (!admits(object, Kind::Object))
        return detail::admit_failure(object, Kind::Object, "remove", "an object", env.receiver_text);
    if (object_find(object, key) == nullptr) return failed_text(Failure::Bounds, "remove: key not found");
    Storage::object_remove(object, key);
    return {};
}

Outcome Protocol::field_set(Value& instance, Atom field, const Value& value, const Env& env) {
    if (!admits(instance, Kind::Struct))
        return detail::admit_failure(instance, Kind::Struct, "field", "a struct", env.receiver_text);
    if (struct_get(instance, field) == nullptr)
        return no_field_failure(env.receiver_text);
    if (closes(instance, value, env)) return cycle_failure(0);
    Storage::struct_set(instance, field, value);
    return {};
}

Outcome Protocol::clear(Value& receiver, const Env& env) {
    if (receiver.is(Kind::Collection)) {
        Outcome out = check_mutable(receiver, env.receiver_text);
        if (out.ok()) Storage::collection_clear(receiver);
        return out;
    }
    if (!admits(receiver, Kind::Array))
        return detail::admit_failure(receiver, Kind::Array, "clear", "an array", env.receiver_text);
    if (!reserve(env.heap_limit, array_reserve_bytes(receiver, 0))) return budget_failure();
    commit_array(receiver, ArrayEdit::Clear, 0, 0, Value());
    return {};
}

} // namespace nift::rt::mutation
