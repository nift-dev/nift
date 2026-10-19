// CP09 follow-ups from the CP11 review, round 2:
//   R6  equality and hashing are iterative (no C++ recursion at any depth),
//       equal_within stops at a depth bound in legacy's visiting order, and
//       the legacy bridge fails cleanly beyond kMaxBridgeDepth;
//   R5  exact live-heap accounting (rt/heap_account.h);
//   reference safety: in sanitizer builds every mutation entry point
//       relocates the aggregate's storage, so a reference held across a
//       mutation is a deterministic use-after-free.
//
//   value_round2            run the checks
//   value_round2 stale-array | stale-object | stale-string
//                           deliberately use a stale reference; under
//                           AddressSanitizer this must abort (the sanitizer
//                           target expects a failure exit)
#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/legacy_bridge.h"
#include "rt/string.h"
#include "rt_test.h"

#include <cstdio>
#include <string>

using namespace nift::rt;

namespace {

Value I(std::int64_t i) { return Value::integer(i); }

Value nest(std::size_t depth, Value leaf) {
    Value v = static_cast<Value&&>(leaf);
    for (std::size_t k = 0; k < depth; ++k) {
        Value outer = make_array(1);
        array_push(outer, v);
        v = outer;
    }
    return v;
}

// The former recursive definition, as the reference for shallow values.
std::uint64_t mix(std::uint64_t h, std::uint64_t v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }

std::uint64_t fnv(const std::string& text) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char ch : text) { h ^= ch; h *= 1099511628211ull; }
    return h;
}

void deep_equality_and_hash() {
    const std::size_t depth = 300000;  // far beyond any C++ stack for a recursive walk
    Value a = nest(depth, I(1)), b = nest(depth, I(1)), c = nest(depth, I(2));
    CHECK(equal(a, b));
    CHECK(!equal(a, c));
    CHECK(hash(a) == hash(b));
    CHECK(equal_within(a, b, 1000) == Equality::TooDeep);
    CHECK(equal_within(nest(1000, I(1)), nest(1000, I(1)), 1000) == Equality::Equal);
    CHECK(equal_within(nest(1001, I(1)), nest(1001, I(1)), 1000) == Equality::TooDeep);
    CHECK(equal_within(nest(10, I(1)), nest(10, I(2)), 1000) == Equality::Unequal);
    // A bounded walk descends into a shared aggregate (legacy has no identity shortcut).
    CHECK(equal_within(a, a, 1000) == Equality::TooDeep && equal(a, a));
    // Legacy order: the first array element is decided before the second.
    Value first_deep = make_array(2), first_deep2 = make_array(2);
    array_push(first_deep, nest(50, I(1)));
    array_push(first_deep, I(1));
    array_push(first_deep2, nest(50, I(1)));
    array_push(first_deep2, I(2));
    CHECK(equal_within(first_deep, first_deep2, 10) == Equality::TooDeep);
    CHECK(equal_within(first_deep, first_deep2, 100) == Equality::Unequal);
    // Objects: a missing key decides only where legacy reaches it.
    Value o1 = make_object(), o2 = make_object();
    object_set(o1, atom("a"), nest(50, I(1)));
    object_set(o1, atom("b"), I(1));
    object_set(o2, atom("a"), nest(50, I(1)));
    object_set(o2, atom("c"), I(1));
    CHECK(equal_within(o1, o2, 10) == Equality::TooDeep);
    CHECK(equal_within(o1, o2, 100) == Equality::Unequal);
    // Hash values are those of the recursive definition.
    Value arr = make_array();
    array_push(arr, I(3));
    array_push(arr, make_string("long enough to be a heap string"));
    std::uint64_t expect = 8;
    expect = mix(expect, hash(I(3)));
    expect = mix(expect, hash(make_string("long enough to be a heap string")));
    CHECK(hash(arr) == expect);
    Value outer = make_array();
    array_push(outer, arr);
    array_push(outer, I(1));
    CHECK(hash(outer) == mix(mix(8, expect), hash(I(1))));
    Value obj = make_object();
    object_set(obj, atom("k"), arr);
    CHECK(hash(obj) == 9 + mix(fnv("k"), expect));
}

void bridge_depth() {
    std::string error;
    nift::RuntimeValue out;
    CHECK(to_legacy(nest(kMaxBridgeDepth - 1, I(1)), out, error));
    CHECK(!to_legacy(nest(kMaxBridgeDepth + 5, I(1)), out, error) && error.find("nesting exceeds") != std::string::npos);
    nift::RuntimeValue deep(1.0);
    for (std::size_t k = 0; k < kMaxBridgeDepth + 5; ++k) {
        nift::RuntimeValue wrap = nift::RuntimeValue::make_array();
        wrap.array.push_back(std::move(deep));
        deep = std::move(wrap);
    }
    Value back;
    error.clear();
    CHECK(!from_legacy(deep, back, error) && error.find("nesting exceeds") != std::string::npos);
    // Legacy's recursive destructor: unwind the test value iteratively.
    while (deep.is_array() && !deep.array.empty()) {
        nift::RuntimeValue inner = std::move(deep.array[0]);
        deep = std::move(inner);
    }
}

void heap_accounting() {
    std::size_t base = live_heap_bytes();
    const std::size_t charged0 = heap_charged_bytes();
    {
        Value s = make_string(std::string(100000, 'x'));
        CHECK(live_heap_bytes() >= base + 100000 && live_heap_bytes() < base + 100000 + 1024);
        Value copy = s;  // a reference, not a new object
        CHECK(live_heap_bytes() < base + 100000 + 1024);
        Value t = make_string(std::string(20, 'y'));
        const std::size_t before = live_heap_bytes();
        for (int k = 0; k < 12; ++k) append_in_place(t, t);  // 20 * 4096 bytes
        CHECK(live_heap_bytes() >= before + 20 * 4096 - 20);
        Value nested = make_array();
        for (int k = 0; k < 1000; ++k) array_push(nested, make_string(std::string(100, 'z')));
        CHECK(live_heap_bytes() >= before + 20 * 4096 + 1000 * (100 + 16));
        Value shared = nested;
        array_push(shared, I(1));  // copy-on-write clone is counted
        CHECK(live_heap_bytes() >= before + 20 * 4096 + 1000 * (100 + 16) + 1000 * sizeof(Value));
        Value o = make_object();
        for (int k = 0; k < 200; ++k) object_set(o, atom("key" + std::to_string(k)), I(k));
        CHECK(object_remove(o, atom("key7")));
        const Value deep = nest(100000, make_string(std::string(64, 'd')));
        CHECK(live_heap_bytes() >= before + 100000 * sizeof(ArrayObject));
    }
    // The 200 new atoms stay charged (C2: the atom table lives as long as the
    // thread; reclaiming it is CP23's); every value is gone.
    CHECK(heap_charged_bytes() - charged0 >= 200 * AtomTable::kAtomEntryOverhead);
    base += heap_charged_bytes() - charged0;
    CHECK(live_heap_bytes() == base);  // everything freed: the count returns exactly
    // An object never retallied (direct construction in host code) counts 0.
    { Value f = Value::adopt(new FunctionObject()); CHECK(live_heap_bytes() == base); }
    CHECK(live_heap_bytes() == base);
}

int stale(const std::string& what) {
    if (what == "stale-array") {
        Value a = make_array();
        array_push(a, make_string("an element long enough for the heap"));
        const Value& held = array_items(a)[0];
        array_push(a, I(2));  // relocates in checked builds
        std::printf("%zu\n", string_view_of(held).size());
    } else if (what == "stale-object") {
        Value o = make_object();
        object_set(o, atom("k"), I(1));
        const Value* held = object_get(o, atom("k"));
        object_set(o, atom("k"), I(2));
        std::printf("%lld\n", static_cast<long long>(held->as_int()));
    } else if (what == "stale-string") {
        Value s = make_string(std::string(40, 's'));
        const std::string_view held = string_view_of(s);
        append_in_place(s, make_string("t"));
        std::printf("%c\n", held[0]);
    } else {
        return 2;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) return stale(argv[1]);
    deep_equality_and_hash();
    bridge_depth();
    heap_accounting();
    return rt_test::finish("rt value round 2");
}
