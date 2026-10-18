// CP14 built-in natives: instruction-count guards (callgrind) and allocation
// counts per native operation at a given input size.
//
// Linked against the natives and the CP09 value model only, WITHOUT
// RuntimeValue.cpp, legacy_bridge.cpp and render.cpp: the link itself proves
// that no native converts to or through a legacy RuntimeValue.
//
// Usage: builtins-bench <n> [workload-substring]
//   For each workload: builds its input of size n (not measured), zeroes the
//   callgrind counters, runs the operation (`reps` calls), dumps the counters
//   under the workload's name (CALLGRIND_DUMP_STATS_AT), and prints
//   "<name> <n> <reps> <allocations> <allocated bytes> <live-heap delta>".
//   Outside valgrind the client requests are no-ops (allocation counts only).
//   tests/runtime2/builtins/builtins_guards.py drives it.
#include "rt/aggregate.h"
#include "rt/atom.h"
#include "rt/heap_account.h"
#include "rt/natives/builtin.h"
#include "rt/string.h"

#include <valgrind/callgrind.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <new>
#include <string>
#include <vector>

namespace {

std::size_t g_allocations = 0, g_bytes = 0;
bool g_counting = false;

} // namespace

void* operator new(std::size_t size) {
    if (g_counting) { ++g_allocations; g_bytes += size; }
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace nift::rt;
using namespace nift::rt::natives;

namespace {

struct Workload {
    const char* name;
    BuiltinId id;
    // Builds the arguments for size n; `reps` calls are measured (conversions
    // run n calls on n different inputs).
    std::function<std::vector<std::vector<Value>>(std::size_t n)> inputs;
};

std::vector<std::vector<Value>> one(std::vector<Value> args) { return {static_cast<std::vector<Value>&&>(args)}; }

Value text_of(std::size_t n, char fill = 'a') { return make_string(std::string(n, fill)); }

Value int_array(std::size_t n) {
    Value a = make_array(n);
    for (std::size_t i = 0; i < n; ++i) array_push(a, Value::integer(static_cast<std::int64_t>(i)));
    return a;
}

Value string_array(std::size_t n) {
    Value a = make_array(n);
    for (std::size_t i = 0; i < n; ++i) array_push(a, make_string("ab"));
    return a;
}

Value object_of(std::size_t n) {
    Value o = make_object();
    for (std::size_t i = 0; i < n; ++i) object_set(o, atom("k" + std::to_string(i)), Value::integer(static_cast<std::int64_t>(i)));
    return o;
}

Value records(std::size_t n) {
    Value a = make_array(n);
    for (std::size_t i = 0; i < n; ++i) {
        Value o = make_object();
        object_set(o, atom("id"), Value::integer(static_cast<std::int64_t>(i)));
        object_set(o, atom("name"), make_string("name \"" + std::to_string(i) + "\""));
        array_push(a, static_cast<Value&&>(o));
    }
    return a;
}

std::vector<Workload> workloads() {
    auto s = [](const char* t) { return make_string(t); };
    auto i = [](std::int64_t v) { return Value::integer(v); };
    return {
        {"str.length", BuiltinId::StrLength, [](std::size_t n) { return one({text_of(n)}); }},
        {"str.split", BuiltinId::StrSplit, [s](std::size_t n) {
             std::string t; for (std::size_t k = 0; k < n; ++k) t += "ab,"; return one({make_string(t), s(",")}); }},
        {"str.replace", BuiltinId::StrReplace, [s](std::size_t n) {
             std::string t(n, 'a'); for (std::size_t k = 0; k < n; k += 10) t[k] = 'x';
             return one({make_string(t), s("x"), s("yy")}); }},
        {"str.substr", BuiltinId::StrSubstr, [i](std::size_t n) {
             return one({text_of(n), i(static_cast<std::int64_t>(n / 4)), i(static_cast<std::int64_t>(n / 2))}); }},
        {"str.trim", BuiltinId::StrTrim, [](std::size_t n) {
             return one({make_string(" " + std::string(n, 'a') + " ")}); }},
        {"str.to_upper", BuiltinId::StrToUpper, [](std::size_t n) { return one({text_of(n)}); }},
        {"str.index_of", BuiltinId::StrIndexOf, [s](std::size_t n) {
             return one({make_string(std::string(n, 'a') + "b"), s("b")}); }},
        {"str.to_int", BuiltinId::StrToInt, [](std::size_t n) {
             std::vector<std::vector<Value>> calls;
             for (std::size_t k = 0; k < n; ++k) calls.push_back({make_string(std::to_string(k * 7919))});
             return calls; }},
        {"str.to_double", BuiltinId::StrToDouble, [](std::size_t n) {
             std::vector<std::vector<Value>> calls;
             for (std::size_t k = 0; k < n; ++k) calls.push_back({make_string(std::to_string(k) + ".25")});
             return calls; }},
        {"num.to_string", BuiltinId::NumToString, [](std::size_t n) {
             std::vector<std::vector<Value>> calls;
             for (std::size_t k = 0; k < n; ++k) calls.push_back({Value::number(static_cast<double>(k) + 0.1)});
             return calls; }},
        {"arr.first", BuiltinId::ArrFirst, [](std::size_t n) { return one({int_array(n)}); }},
        {"arr.join", BuiltinId::ArrJoin, [s](std::size_t n) { return one({string_array(n), s("")}); }},
        {"arr.join.numbers", BuiltinId::ArrJoin, [s](std::size_t n) { return one({int_array(n), s(",")}); }},
        {"arr.slice", BuiltinId::ArrSlice, [i](std::size_t n) {
             return one({int_array(n), i(static_cast<std::int64_t>(n / 4)), i(static_cast<std::int64_t>(3 * n / 4))}); }},
        {"arr.take", BuiltinId::ArrTake, [i](std::size_t n) {
             return one({string_array(n), i(static_cast<std::int64_t>(n))}); }},
        {"arr.sum", BuiltinId::ArrSum, [](std::size_t n) { return one({int_array(n)}); }},
        {"arr.contains", BuiltinId::ArrContains, [i](std::size_t n) { return one({int_array(n), i(-1)}); }},
        {"arr.unique", BuiltinId::ArrUnique, [](std::size_t n) { return one({int_array(n)}); }},
        {"arr.max", BuiltinId::ArrMax, [](std::size_t n) { return one({int_array(n)}); }},
        {"fn.range", BuiltinId::FnRange, [i](std::size_t n) { return one({i(static_cast<std::int64_t>(n))}); }},
        {"obj.get", BuiltinId::ObjGet, [s](std::size_t n) { return one({object_of(n), make_string("k" + std::to_string(n / 2))}); }},
        {"obj.keys", BuiltinId::ObjKeys, [](std::size_t n) { return one({object_of(n)}); }},
        {"obj.merge", BuiltinId::ObjMerge, [](std::size_t n) { return one({object_of(n), object_of(4)}); }},
        {"any.stringify", BuiltinId::Stringify, [](std::size_t n) { return one({records(n)}); }},
        {"any.prettify", BuiltinId::Prettify, [](std::size_t n) { return one({records(n)}); }},
    };
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: builtins-bench <n> [workload]\n"); return 2; }
    const std::size_t n = std::strtoull(argv[1], nullptr, 10);
    const char* filter = argc > 2 ? argv[2] : nullptr;
    const Context context;
    for (const Workload& w : workloads()) {
        if (filter && !std::strstr(w.name, filter)) continue;
        auto calls = w.inputs(n);
        std::vector<Value> results(calls.size());
        std::string error;
        const BuiltinSpec& spec = builtin_spec(w.id);
        const std::size_t heap_before = live_heap_bytes();
        g_allocations = g_bytes = 0;
        g_counting = true;
        CALLGRIND_ZERO_STATS;
        for (std::size_t k = 0; k < calls.size(); ++k)
            if (call_builtin(spec, context, calls[k].data(), static_cast<std::uint32_t>(calls[k].size()), results[k],
                             error) != Status::Ok) {
                std::fprintf(stderr, "%s failed: %s\n", w.name, error.c_str());
                return 1;
            }
        CALLGRIND_DUMP_STATS_AT(w.name);
        g_counting = false;
        const long long delta = static_cast<long long>(live_heap_bytes()) - static_cast<long long>(heap_before);
        std::printf("%s %zu %zu %zu %zu %lld\n", w.name, n, calls.size(), g_allocations, g_bytes, delta);
    }
    return 0;
}
