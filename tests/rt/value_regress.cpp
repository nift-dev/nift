// CP09 round-1 review regressions for src/rt (ported from the reviewer's
// repro programs numhash, handles, cow, bridge, strings, inl and objfuzz).
//
//   value_regress [depth]     (depth of the deep-destruction test, default 1000000)
#include "legacy_oracle.h"
#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/decimal.h"
#include "rt/handle.h"
#include "rt/heap.h"
#include "rt/legacy_bridge.h"
#include "rt/number.h"
#include "rt/string.h"
#include "rt_test.h"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

using namespace nift::rt;
using nift::RuntimeValue;

namespace {

Value I(std::int64_t i) { return Value::integer(i); }
Value D(double d) { return Value::number(d); }
Value Dec(const char* spelling) { return make_decimal(spelling, std::strtod(spelling, nullptr)); }

RuntimeValue legacy(const Value& v) {
    RuntimeValue out;
    std::string err;
    CHECK(to_legacy(v, out, err));
    return out;
}

// Same answer as legacy for equality, and equal => same hash.
void same_as_legacy(const Value& a, const Value& b, bool expect_equal) {
    CHECK(equal(a, b) == expect_equal);
    CHECK(nift::runtime_equal(legacy(a), legacy(b)) == expect_equal);
    if (expect_equal) CHECK(hash(a) == hash(b));
}

// Finding 1: integral doubles above 2^53 compare by their shortest spelling.
void hash_beyond_2_53() {
    const Value d62 = D(4611686018427387904.0);  // 2^62, spelled 4.611686018427388e18
    same_as_legacy(d62, I(4611686018427388000LL), true);
    same_as_legacy(d62, Dec("4611686018427388000"), true);
    same_as_legacy(d62, Dec("4.611686018427388e18"), true);
    same_as_legacy(d62, I(4611686018427387904LL), false);  // the exact value is a different number
    same_as_legacy(I(4611686018427388000LL), Dec("4611686018427388000.000"), true);
    same_as_legacy(D(1e300), Dec("1e300"), true);
    same_as_legacy(D(-1e19), Dec("-10000000000000000000"), true);
    // Across the 2^53 boundary every kind agrees.
    for (std::int64_t v : {9007199254740991LL, 9007199254740992LL, -9007199254740992LL, 9007199254740994LL,
                           9007199254740996LL, -9007199254740996LL}) {
        const std::string spelling = std::to_string(v);
        same_as_legacy(I(v), Dec(spelling.c_str()), true);
        same_as_legacy(I(v), D(static_cast<double>(v)), true);
        same_as_legacy(D(static_cast<double>(v)), Dec((spelling + ".0").c_str()), true);
    }
    same_as_legacy(I(9007199254740993LL), Dec("9007199254740993"), true);
    same_as_legacy(I(9007199254740993LL), D(9007199254740992.0), false);
    same_as_legacy(D(-0.0), I(0), true);
    same_as_legacy(D(-0.0), Dec("-0.0"), true);
}

// Finding 2: exponents of any size are exact, as in legacy.
void extreme_exponents() {
    const Value zero_big = make_decimal("0e99999999999999999999", 0.0);
    const Value tiny = make_decimal("1e-99999999999999999999", 0.0);
    const Value huge = make_decimal("1e99999999999999999999", HUGE_VAL);
    same_as_legacy(zero_big, I(0), true);
    same_as_legacy(make_decimal("-0.00e-99999999999999999999", -0.0), D(0.0), true);
    same_as_legacy(tiny, I(0), false);
    same_as_legacy(tiny, make_decimal("10e-100000000000000000000", 0.0), true);
    same_as_legacy(huge, make_decimal("0.1e100000000000000000000", HUGE_VAL), true);
    same_as_legacy(huge, make_decimal("1e99999999999999999998", HUGE_VAL), false);
    // The 10^18 small/big split is canonical: the same value either side of it.
    same_as_legacy(Dec("1e1000000000000000000"), Dec("10e999999999999999999"), true);
    same_as_legacy(Dec("1e1000000000000000001"), Dec("0.1e1000000000000000002"), true);
    same_as_legacy(Dec("1e-1000000000000000001"), Dec("100e-1000000000000000003"), true);
    CHECK(!truthy(zero_big) && !nift::runtime_truthy(legacy(zero_big)));
    CHECK(truthy(tiny) && nift::runtime_truthy(legacy(tiny)));
    CHECK(number_is_zero(zero_big) && number_is_integer(zero_big) && !number_is_integer(tiny));
    CHECK(number_is_integer(huge) && nift::runtime_number_is_integer(legacy(huge)));
    std::int64_t i = -1;
    CHECK(number_to_i64(zero_big, i) && i == 0 && !number_to_i64(huge, i));
    int c = 0, lc = 0;
    CHECK(compare_relational(tiny, I(0), c) && c > 0);
    CHECK(compare_relational(make_decimal("-1e99999999999999999999", -HUGE_VAL), D(-1e308), c) && c < 0);
    CHECK(compare_relational(huge, D(1e308), c) && c > 0);
    CHECK(nift::runtime_compare_numbers_relational(legacy(huge), RuntimeValue(1e308), lc) && lc == c);
    // Arithmetic errors match the legacy operator.
    struct Case { ArithOp op; char name; Value left, right; const char* expected; };
    const Case cases[] = {
        {ArithOp::Div, '/', I(1), zero_big, "division by zero"},
        {ArithOp::Mod, '%', I(5), zero_big, "modulo by zero"},
        {ArithOp::Mod, '%', D(5.5), zero_big, "modulo by zero"},
        {ArithOp::Mod, '%', I(5), tiny, "modulo requires integer-valued operands"},
        {ArithOp::Add, '+', huge, I(1), "arithmetic result is not finite"},
    };
    for (const Case& k : cases) {
        Value out;
        RuntimeValue lout;
        std::string err, lerr;
        CHECK(!arithmetic(k.op, k.left, k.right, out, err) && err == k.expected);
        CHECK(!legacy_oracle::numeric_binary(legacy(k.left), legacy(k.right), k.name, lout, lerr) && lerr == err);
    }
    Value out;
    std::string err;
    CHECK(arithmetic(ArithOp::Add, zero_big, I(41), out, err) && out.is(Kind::Int) && out.as_int() == 41);
    DecimalForm f;
    CHECK(parse_decimal("12e+000000000000000000000000000003", f) && f.exponent == 3 && f.big_exponent.empty());
    CHECK(parse_decimal("1e-99999999999999999999", f) && f.big_exponent == "-99999999999999999999" &&
          f.exponent < 0);
    CHECK(parse_decimal("1.5e1000000000000000001", f) && f.big_exponent.empty() &&
          f.exponent == DecimalForm::kSmallExponentLimit);
    CHECK(decimal_fingerprint(number_form(Dec("2.50e99999999999999999999"))) == "+25e99999999999999999998");
}

// Finding 3: handle registry generations and null resources.
void handles() {
    HandleRegistry reg(HandleKind::File);
    auto res = std::make_shared<int>(5);
    CHECK(reg.add(nullptr).is_null() && reg.live() == 0);
    Value first = reg.add(res);
    CHECK(reg.release(first) && !reg.release(first));  // double release fails
    // A slot about to exhaust its generations: the last generation is issued
    // once, then the slot is retired instead of wrapping to 0.
    CHECK(reg.set_free_slot_generation_for_test(first.handle_id(), HandleRegistry::kMaxGeneration - 1));
    CHECK(!reg.set_free_slot_generation_for_test(99, 0));
    Value a = reg.add(res);
    CHECK(a.handle_id() == first.handle_id() && a.handle_generation() == HandleRegistry::kMaxGeneration - 1);
    CHECK(reg.release(a));
    Value b = reg.add(res);
    CHECK(b.handle_id() == first.handle_id() && b.handle_generation() == HandleRegistry::kMaxGeneration);
    CHECK(reg.resolve(b) && reg.release(b) && reg.retired() == 1 && reg.live() == 0);
    Value c = reg.add(std::make_shared<int>(42));
    CHECK(c.handle_id() != b.handle_id());  // the retired slot is never reused
    CHECK(!reg.resolve(a) && !reg.resolve(b) && !reg.resolve(first) && !reg.release(b));
    const Value forged = Value::handle(HandleKind::File, b.handle_id(), 0);  // wrapped generation
    CHECK(!reg.resolve(forged) && !reg.release(forged));
    CHECK(reg.resolve(c) && *static_cast<int*>(reg.resolve(c).get()) == 42 && reg.live() == 1);
}

// Copy-on-write programs that are within the reference contract (cow.cpp).
void cow_contract_cases() {
    Value a = make_array();
    array_push(a, I(1));
    array_push(a, a);  // a.push(a): the pushed value is the pre-push snapshot
    CHECK(array_size(a) == 2 && array_size(array_items(a)[1]) == 1 && array_items(a)[1].heap() != a.heap());
    Value o = make_object();
    object_set(o, atom("x"), I(1));
    object_set(o, atom("self"), o);
    CHECK(object_size(o) == 2 && object_size(*object_get(o, atom("self"))) == 1);
    a = array_items(a)[1];  // assign a child into its parent
    CHECK(array_size(a) == 1 && array_items(a)[0].as_int() == 1);
    Value m = make_array();
    array_push(m, make_string("a string longer than fourteen"));
    m = std::move(array_mut(m)[0]);  // move a child into its parent
    CHECK(m.is(Kind::String) && string_view_of(m) == "a string longer than fourteen" && m.refcount() == 1);
    Value s = m;
    Value& self = s;
    s = std::move(self);  // self-move keeps the value
    CHECK(s.is(Kind::String) && s.refcount() == 2);
    Value p = make_array();
    array_push(p, I(7));
    array_push(p, array_items(p)[0]);  // the argument is copied before the push reallocates
    CHECK(array_size(p) == 2 && array_items(p)[1].as_int() == 7);
}

// Finding 5: destruction of deep values never recurses.
void deep_destruction(long depth) {
    {
        Value a = make_array();
        for (long i = 0; i < depth; ++i) { Value outer = make_array(); array_push(outer, std::move(a)); a = std::move(outer); }
        Value copy = a;
        CHECK(equal(a, copy));  // same object: no recursion
    }
    {
        Value a = make_array();  // a = [a] with COW copies, O(1) per step
        for (long i = 0; i < depth; ++i) { Value outer = make_array(); array_push(outer, a); a = outer; }
        CHECK(a.refcount() == 1);
    }
    {
        Value o = make_object();
        const Atom k = atom("next");
        for (long i = 0; i < depth; ++i) { Value outer = make_object(); object_set(outer, k, o); o = outer; }
    }
    {
        Value f = Value::adopt(new FunctionObject());
        for (long i = 0; i < depth / 4; ++i) {
            auto* outer = new FunctionObject();
            outer->upvalues.push_back(f);
            auto* st = new StructObject(atom("Node"));
            st->fields.emplace_back(atom("f"), Value::adopt(outer));
            f = Value::adopt(st);
        }
    }
    {
        // A shared subtree survives its parent's destruction.
        Value shared = make_array();
        array_push(shared, make_string("still alive after the parent dies"));
        Value parent = make_array();
        for (int i = 0; i < 3; ++i) array_push(parent, shared);
        parent = Value();
        CHECK(shared.refcount() == 1 && string_view_of(array_items(shared)[0]) == "still alive after the parent dies");
    }
    CHECK(true);  // reaching here means no stack overflow
}

template <class T, class = void> struct has_public_inline_text : std::false_type {};
template <class T>
struct has_public_inline_text<T, std::void_t<decltype(T::inline_text(std::string_view()))>> : std::true_type {};
static_assert(!has_public_inline_text<Value>::value, "inline_text must not be callable outside make_string");

// Finding 6: inline strings (strings.cpp, inl.cpp) and refcount saturation.
void strings_and_refcounts() {
    for (std::size_t n : {0, 1, 13, 14, 15, 16, 40}) {
        std::string text(n, '\0');
        for (std::size_t i = 0; i < n; ++i) text[i] = static_cast<char>(i % 2 ? 0 : 0xFF);
        const Value v = make_string(text);
        const Value heap = Value::adopt(new StringObject(text));
        CHECK(v.is(Kind::String) && v.inline_string() == (n <= Value::kInlineStringCapacity));
        CHECK(string_view_of(v) == text && equal(v, heap) && hash(v) == hash(heap));
    }
    const Value hk = Value::handle(static_cast<HandleKind>(0x8E), 1, 2);
    CHECK(!hk.inline_string() && !hk.is_heap());
    Value v = make_array();
    v.heap()->refcount = Value::kImmortal - 1;
    {
        Value c1 = v;  // reaches kImmortal
        Value c2 = v;  // saturated: stays
        CHECK(v.refcount() == Value::kImmortal);
    }
    CHECK(v.refcount() == Value::kImmortal);  // releases do not count down an immortal object
    Value m = v;
    array_push(m, I(1));  // a saturated object is shared: mutation clones
    CHECK(m.heap() != v.heap() && array_size(v) == 0 && m.refcount() == 1);
    v.heap()->refcount = 1;  // test only: let the object be freed (leak checker)
}

// Finding 7: legacy objects with duplicate keys do not convert (bridge.cpp).
void duplicate_keys() {
    RuntimeValue x = RuntimeValue::make_object(), y = RuntimeValue::make_object(), z = RuntimeValue::make_object();
    x.object.emplace_back("a", RuntimeValue(1.0));
    x.object.emplace_back("a", RuntimeValue(2.0));
    y.object.emplace_back("a", RuntimeValue(2.0));
    y.object.emplace_back("a", RuntimeValue(1.0));
    z.object.emplace_back("a", RuntimeValue(1.0));
    Value rx, rz;
    std::string err;
    CHECK(!from_legacy(x, rx, err) && err == "object has duplicate key 'a'");
    CHECK(!from_legacy(y, rx, err));
    CHECK(from_legacy(z, rz, err) && object_size(rz) == 1 && object_get(rz, atom("a"))->as_double() == 1.0);
    RuntimeValue nested = RuntimeValue::make_array();
    nested.push_back(RuntimeValue(nullptr));
    nested.push_back(x);
    CHECK(!from_legacy(nested, rx, err));
    // Many unique keys (indexed) convert with order kept; one repeat fails.
    RuntimeValue wide = RuntimeValue::make_object();
    for (int i = 0; i < 20; ++i) wide["k" + std::to_string(19 - i)] = RuntimeValue(static_cast<double>(i));
    CHECK(from_legacy(wide, rx, err) && object_size(rx) == 20 && object_entries(rx)[0].first == atom("k19"));
    wide.object.emplace_back("k7", RuntimeValue(true));
    CHECK(!from_legacy(wide, rx, err) && err == "object has duplicate key 'k7'");
}

} // namespace

int main(int argc, char** argv) {
    const long depth = argc > 1 ? std::strtol(argv[1], nullptr, 10) : 1000000;
    hash_beyond_2_53();
    extreme_exponents();
    handles();
    cow_contract_cases();
    deep_destruction(depth);
    strings_and_refcounts();
    duplicate_keys();
    return rt_test::finish("rt value regress");
}
