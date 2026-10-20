#include "rt/natives/array_methods.h"

#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/compare.h"
#include "rt/natives/support.h"
#include "rt/number.h"
#include "rt/string.h"

#include <limits>
#include <vector>

namespace nift::rt::natives {

namespace {

const std::vector<Value>* receiver(const Value* args, std::uint32_t argc) {
    if (argc == 0 || !args[0].is(Kind::Array)) return nullptr;
    return &array_items(args[0]);
}

Status internal(std::string& error, const char* what) {
    error = what;
    return Status::Internal;
}

std::string named(std::string_view name, const char* message) { return std::string(name) + message; }

// A copy of items[begin, end) as a new array. Elements are shared (O(1)
// copies); the receiver is never mutated, so `items` stays valid.
Value copy_range(const std::vector<Value>& items, std::size_t begin, std::size_t end) {
    ArrayBuilder result(end - begin);
    for (std::size_t i = begin; i < end; ++i) result.push(items[i]);
    return result.finish();
}

// Legacy min/max selection over `values` (non-empty): numbers by exact value,
// strings bytewise; the first value is taken without a check, every later one
// must be comparable with the current pick.
Status select(const Value* values, std::size_t count, bool want_min, std::string_view name, Value& out,
              std::string& error) {
    const Value* best = &values[0];
    for (std::size_t i = 1; i < count; ++i) {
        const Value& v = values[i];
        bool take = false;
        if (best->is_number() && v.is_number()) {
            const int c = compare_numbers(v, *best);
            take = want_min ? c < 0 : c > 0;
        } else if (best->is(Kind::String) && v.is(Kind::String)) {
            const int c = string_view_of(v).compare(string_view_of(*best));
            take = want_min ? c < 0 : c > 0;
        } else {
            return fail(error, named(name, ": values must be comparable and homogeneous"));
        }
        if (take) best = &v;
    }
    out = *best;  // copied last: `out` may alias an argument
    return Status::Ok;
}

Status aggregate_select(const Value* args, std::uint32_t argc, Value& out, std::string& error, bool want_min) {
    const std::string_view name = want_min ? "min" : "max";
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "min/max receiver");
    if (!no_arguments(argc, name, error)) return Status::Error;
    if (items->empty()) return fail(error, named(name, ": cannot aggregate an empty array"));
    return select(items->data(), items->size(), want_min, name, out, error);
}

Status free_select(const Value* args, std::uint32_t argc, Value& out, std::string& error, bool want_min) {
    const std::string_view name = want_min ? "min" : "max";
    if (argc == 1 && args[0].is(Kind::Array)) {
        const auto& items = array_items(args[0]);
        if (items.empty()) return fail(error, named(name, ": expected at least one value"));
        return select(items.data(), items.size(), want_min, name, out, error);
    }
    if (argc == 0) return fail(error, named(name, ": expected at least one value"));
    return select(args, argc, want_min, name, out, error);
}

// take / drop / chunk count: legacy "<m>: count must be a non-negative
// integer" (chunk: "a positive integer", and 0 is rejected).
Status count_argument(const Value* args, std::uint32_t argc, std::string_view name, bool positive,
                      std::size_t& count, std::string& error) {
    if (argc != 2) return fail(error, named(name, ": expected one count"));
    if (!number_to_size(args[1], count) || (positive && count == 0))
        return fail(error, named(name, positive ? ": count must be a positive integer"
                                                : ": count must be a non-negative integer"));
    return Status::Ok;
}

} // namespace

Status arr_size(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "size receiver");
    if (!no_arguments(argc, "size", error)) return Status::Error;
    out = count_value(items->size());
    return Status::Ok;
}

Status arr_length(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "length receiver");
    if (!no_arguments(argc, "length", error)) return Status::Error;
    out = count_value(items->size());
    return Status::Ok;
}

Status arr_empty(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "empty receiver");
    if (!no_arguments(argc, "empty", error)) return Status::Error;
    out = Value::boolean(items->empty());
    return Status::Ok;
}

Status arr_first(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "first receiver");
    if (!no_arguments(argc, "first", error)) return Status::Error;
    if (items->empty()) return fail(error, "first: array is empty");
    out = items->front();
    return Status::Ok;
}

Status arr_last(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "last receiver");
    if (!no_arguments(argc, "last", error)) return Status::Error;
    if (items->empty()) return fail(error, "last: array is empty");
    out = items->back();
    return Status::Ok;
}

Status arr_index_of(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "indexOf receiver");
    if (argc != 2) return fail(error, "indexOf: expected one value");
    for (std::size_t i = 0; i < items->size(); ++i)
        if (equal((*items)[i], args[1])) { out = count_value(i); return Status::Ok; }
    out = Value::integer(-1);
    return Status::Ok;
}

Status arr_contains(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "contains receiver");
    if (argc != 2) return fail(error, "contains: expected one value");
    bool found = false;
    for (const Value& item : *items)
        if (equal(item, args[1])) { found = true; break; }
    out = Value::boolean(found);
    return Status::Ok;
}

Status arr_join(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "join receiver");
    if (argc != 2) return fail(error, "join: expected separator");
    if (!args[1].is(Kind::String)) return fail(error, "join: separator must be a string");
    const std::string_view separator = string_view_of(args[1]);
    // C3: the result buffer grows only after a budget check (BudgetedText).
    BudgetedText result(context);
    char buffer[64];
    for (std::size_t i = 0; i < items->size(); ++i) {
        const Value& item = (*items)[i];
        if (!renderable_scalar(item)) return fail(error, "join: elements must be renderable scalar values");
        const std::string_view text = item.is(Kind::String) ? string_view_of(item) : render_scalar(item, buffer);
        if (!result.append(i ? separator : std::string_view(), text)) return Status::Memory;
    }
    out = make_string(result.take());
    return Status::Ok;
}

Status arr_slice(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "slice receiver");
    if (argc < 2 || argc > 3) return fail(error, "slice: expected start and optional end");
    std::size_t begin = 0, end = items->size();
    if (!number_to_size(args[1], begin)) return fail(error, "slice: invalid start");
    if (argc == 3 && !number_to_size(args[2], end)) return fail(error, "slice: invalid end");
    begin = std::min(begin, items->size());
    end = std::min(end, items->size());
    if (end < begin) end = begin;
    out = copy_range(*items, begin, end);
    return Status::Ok;
}

Status arr_unique(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "unique receiver");
    if (!no_arguments(argc, "unique", error)) return Status::Error;
    // Legacy keeps the first of each structurally equal group (O(n^2) scan).
    // Here: one open-addressing table of kept positions keyed by the CP09 hash
    // (equal values hash alike), so O(n) with two allocations.
    const std::size_t n = items->size();
    std::size_t capacity = 16;
    while (capacity < n * 2) capacity <<= 1;
    std::vector<std::uint64_t> slots(capacity, 0);      // the hash of the kept value
    std::vector<std::uint32_t> positions(capacity, 0);  // position + 1; 0 = empty
    ArrayBuilder result(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint64_t h = hash((*items)[i]);
        std::size_t slot = static_cast<std::size_t>(h) & (capacity - 1);
        bool seen = false;
        for (; positions[slot] != 0; slot = (slot + 1) & (capacity - 1))
            if (slots[slot] == h && equal((*items)[positions[slot] - 1], (*items)[i])) { seen = true; break; }
        if (seen) continue;
        slots[slot] = h;
        positions[slot] = static_cast<std::uint32_t>(i + 1);
        result.push((*items)[i]);
    }
    out = result.finish();
    return Status::Ok;
}

Status arr_flatten(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "flatten receiver");
    if (!no_arguments(argc, "flatten", error)) return Status::Error;
    ArrayBuilder result(items->size());
    for (const Value& item : *items) {
        if (!item.is(Kind::Array)) { result.push(item); continue; }
        for (const Value& inner : array_items(item)) result.push(inner);
    }
    out = result.finish();
    return Status::Ok;
}

Status arr_sum(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "sum receiver");
    if (!no_arguments(argc, "sum", error)) return Status::Error;
    // Legacy adds the nearest doubles (design record Q6: inexact beyond 2^53,
    // no overflow error), unlike `+`.
    double total = 0;
    for (const Value& item : *items) {
        if (!item.is_number()) return fail(error, "sum: values must be numeric");
        total += number_approx(item);
    }
    out = Value::number(total);
    return Status::Ok;
}

Status arr_min(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return aggregate_select(args, argc, out, error, true);
}

Status arr_max(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return aggregate_select(args, argc, out, error, false);
}

Status arr_take(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "take receiver");
    std::size_t count = 0;
    if (count_argument(args, argc, "take", false, count, error) != Status::Ok) return Status::Error;
    out = copy_range(*items, 0, std::min(count, items->size()));
    return Status::Ok;
}

Status arr_drop(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "drop receiver");
    std::size_t count = 0;
    if (count_argument(args, argc, "drop", false, count, error) != Status::Ok) return Status::Error;
    out = copy_range(*items, std::min(count, items->size()), items->size());
    return Status::Ok;
}

Status arr_chunk(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "chunk receiver");
    std::size_t width = 0;
    if (count_argument(args, argc, "chunk", true, width, error) != Status::Ok) return Status::Error;
    ArrayBuilder result(items->size() / width + 1);
    for (std::size_t b = 0; b < items->size(); b += width) {
        const std::size_t e = items->size() - b > width ? b + width : items->size();
        result.push(copy_range(*items, b, e));
    }
    out = result.finish();
    return Status::Ok;
}

Status arr_from_entries(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    const auto* items = receiver(args, argc);
    if (!items) return internal(error, "from_entries receiver");
    if (!no_arguments(argc, "from_entries", error)) return Status::Error;
    const Atom key_atom = atom("key"), value_atom = atom("value");
    Value result = make_object();
    std::string rendered;
    for (const Value& entry : *items) {
        const Value* key = entry.is(Kind::Object) && object_size(entry) == 2 ? object_get(entry, key_atom) : nullptr;
        const Value* value = key ? object_get(entry, value_atom) : nullptr;
        if (!key || !value)
            return fail(error, "from_entries: each entry must be an object containing exactly key and value");
        if (!generated_key(*key, "from_entries", rendered, error)) return Status::Error;
        const Atom name = atom(rendered);
        if (object_get(result, name)) return fail(error, "from_entries: duplicate generated key '" + rendered + "'");
        object_set(result, name, *value);  // result is fresh: `entry`, `key`, `value` stay valid
    }
    out = static_cast<Value&&>(result);
    return Status::Ok;
}

Status fn_min(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return free_select(args, argc, out, error, true);
}

Status fn_max(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return free_select(args, argc, out, error, false);
}

Status fn_range(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    if (argc == 0 || argc > 3) return fail(error, "range: expected stop, start/stop, or start/stop/step");
    std::int64_t n[3] = {0, 0, 1};
    for (std::uint32_t i = 0; i < argc; ++i)
        if (!args[i].is_number() || !number_to_i64(args[i], n[i]))
            return fail(error, "range: arguments must be signed 64-bit integers");
    const std::int64_t start = argc == 1 ? 0 : n[0], stop = argc == 1 ? n[0] : n[1], step = argc == 3 ? n[2] : 1;
    if (step == 0) return fail(error, "range: step must not be zero");
    constexpr std::size_t kLimit = 10000000;
    constexpr std::int64_t kExact = std::int64_t(1) << 53;
    ArrayBuilder result;
    std::size_t produced = 0;
    for (std::int64_t x = start; step > 0 ? x < stop : x > stop;) {
        if (produced++ >= kLimit) return fail(error, "range: result exceeds 10000000 items");
        // Legacy stores (double)x: exact below 2^53, rounded beyond (design
        // record Q7).
        Value item = x > -kExact && x < kExact ? Value::integer(x) : Value::number(static_cast<double>(x));
        if (!result.push(context, static_cast<Value&&>(item))) return Status::Memory;
        if ((step > 0 && x > std::numeric_limits<std::int64_t>::max() - step) ||
            (step < 0 && x < std::numeric_limits<std::int64_t>::min() - step))
            return fail(error, "range: integer overflow");
        x += step;
    }
    out = result.finish();
    return Status::Ok;
}

} // namespace nift::rt::natives
