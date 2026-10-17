// CP09 unit tests for src/rt value model.
#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/decimal.h"
#include "rt/error.h"
#include "rt/handle.h"
#include "rt/heap.h"
#include "rt/number.h"
#include "rt/string.h"
#include "rt_test.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

using namespace nift::rt;

namespace {

Value I(std::int64_t i) { return Value::integer(i); }
Value D(double d) { return Value::number(d); }
Value S(const char* s) { return make_string(s); }
Value Dec(const char* spelling) { return make_decimal(spelling, std::strtod(spelling, nullptr)); }

bool arith(ArithOp op, const Value& a, const Value& b, Value& out, std::string& err) {
    err.clear();
    return arithmetic(op, a, b, out, err);
}

void layout_and_scalars() {
    CHECK(sizeof(Value) == 16);
    Value n;
    CHECK(n.is_null() && !truthy(n));
    CHECK(Value::boolean(true).as_bool() && !Value::boolean(false).as_bool());
    CHECK(I(-5).as_int() == -5 && I(std::numeric_limits<std::int64_t>::min()).as_int() < 0);
    CHECK(std::signbit(D(-0.0).as_double()));
    Value h = Value::handle(HandleKind::File, 77, 3);
    CHECK(h.handle_kind() == HandleKind::File && h.handle_id() == 77 && h.handle_generation() == 3);
    CHECK(!h.is_heap() && h.refcount() == 0);
}

void strings() {
    Value small = S("abcdefghijklmn");  // 14 bytes: inline
    CHECK(small.inline_string() && string_view_of(small) == "abcdefghijklmn");
    Value large = S("abcdefghijklmno");  // 15 bytes: heap
    CHECK(!large.inline_string() && large.is_heap() && string_view_of(large) == "abcdefghijklmno");
    Value empty = S("");
    CHECK(empty.inline_string() && string_view_of(empty).empty() && !truthy(empty));
    std::string with_nul("a\0b", 3);
    CHECK(string_view_of(make_string(with_nul)) == with_nul);
    Value copy = large;
    CHECK(large.refcount() == 2 && copy.heap() == large.heap());
    { Value moved = static_cast<Value&&>(copy); CHECK(copy.is_null() && large.refcount() == 2); }
    CHECK(large.refcount() == 1);
    CHECK(equal(S("x"), S("x")) && !equal(S("x"), S("y")));
    int c = 0;
    CHECK(compare_relational(S("a"), S("b"), c) && c < 0);
    CHECK(hash(S("long string beyond inline")) == hash(make_string(std::string("long string beyond inline"))));
}

void arrays_cow() {
    Value a = make_array();
    array_push(a, I(1));
    array_push(a, I(2));
    Value b = a;  // O(1) copy
    CHECK(a.heap() == b.heap() && a.refcount() == 2);
    array_push(b, I(3));  // b unshares, a unchanged
    CHECK(a.heap() != b.heap() && array_size(a) == 2 && array_size(b) == 3);
    CHECK(a.refcount() == 1 && b.refcount() == 1);
    CHECK(!unshare(b));  // already unique: no clone
    array_mut(b)[0] = S("z");
    CHECK(equal(array_items(b)[0], S("z")) && equal(array_items(a)[0], I(1)));
    // Nested sharing: cloning the outer array shares inner values.
    Value outer = make_array();
    array_push(outer, a);
    Value alias = outer;
    array_mut(alias);  // shallow clone
    CHECK(array_items(alias)[0].heap() == a.heap() && a.refcount() == 3);
    CHECK(truthy(a) && !truthy(make_array()));
}

void objects() {
    Value o = make_object();
    const Atom kb = atom("b"), ka = atom("a"), kc = atom("c");
    object_set(o, kb, I(1));
    object_set(o, ka, I(2));
    object_set(o, kc, I(3));
    object_set(o, ka, I(20));  // replace keeps position
    const auto& e = object_entries(o);
    CHECK(e.size() == 3 && e[0].first == kb && e[1].first == ka && e[2].first == kc);
    CHECK(object_get(o, ka) && object_get(o, ka)->as_int() == 20 && !object_get(o, atom("zz")));
    Value p = make_object();  // same entries, different order: equal
    object_set(p, kc, I(3));
    object_set(p, ka, D(20.0));
    object_set(p, kb, I(1));
    CHECK(equal(o, p) && hash(o) == hash(p));
    Value q = o;
    object_set(q, kb, I(9));
    CHECK(object_get(o, kb)->as_int() == 1 && object_get(q, kb)->as_int() == 9);
    // Indexed lookup beyond the linear threshold, including removal.
    Value big = make_object();
    for (int i = 0; i < 300; ++i) object_set(big, atom("k" + std::to_string(i)), I(i));
    bool all = true;
    for (int i = 0; i < 300; ++i) {
        const Value* v = object_get(big, atom("k" + std::to_string(i)));
        all = all && v && v->as_int() == i;
    }
    CHECK(all && object_size(big) == 300);
    CHECK(object_remove(big, atom("k5")) && !object_get(big, atom("k5")) && object_size(big) == 299);
    CHECK(object_get(big, atom("k299"))->as_int() == 299 && !object_remove(big, atom("k5")));
    CHECK(object_entries(big)[5].first == atom("k6"));
}

void numbers() {
    Value out;
    std::string err;
    CHECK(arith(ArithOp::Add, I(std::numeric_limits<std::int64_t>::max()), I(1), out, err) == false &&
          err == "signed 64-bit integer overflow");
    CHECK(!arith(ArithOp::Mul, I(4611686018427387904LL), I(2), out, err) && err == "signed 64-bit integer overflow");
    CHECK(!arith(ArithOp::Mul, D(1e18), I(10), out, err) && err == "signed 64-bit integer overflow");  // legacy
    CHECK(arith(ArithOp::Div, I(7), I(2), out, err) && out.is(Kind::Double) && out.as_double() == 3.5);
    CHECK(arith(ArithOp::Div, I(-7), I(2), out, err) && out.as_double() == -3.5);
    CHECK(arith(ArithOp::Div, I(6), I(2), out, err) && out.is(Kind::Double) && out.as_double() == 3.0);
    CHECK(arith(ArithOp::Mod, I(-7), I(3), out, err) && out.as_int() == -1);
    CHECK(arith(ArithOp::Mod, I(std::numeric_limits<std::int64_t>::min()), I(-1), out, err) && out.as_int() == 0);
    CHECK(!arith(ArithOp::Div, I(1), I(0), out, err) && err == "division by zero");
    CHECK(!arith(ArithOp::Mod, I(1), I(0), out, err) && err == "modulo by zero");
    CHECK(!arith(ArithOp::Mod, D(1.5), I(1), out, err) && err == "modulo requires integer-valued operands");
    CHECK(!arith(ArithOp::Mul, D(1e300), D(1e300), out, err) && err == "arithmetic result is not finite");
    CHECK(arith(ArithOp::Add, D(0.1), D(0.2), out, err) && out.as_double() == 0.1 + 0.2);
    CHECK(arith(ArithOp::Add, D(3.0), I(1), out, err) && out.is(Kind::Int) && out.as_int() == 4);
    // Exact numbers beyond 2^53.
    CHECK(arith(ArithOp::Add, Dec("9007199254740993"), I(1), out, err) && out.as_int() == 9007199254740994LL);
    CHECK(equal(Dec("9007199254740993"), I(9007199254740993LL)) && !equal(Dec("9007199254740993"), D(9007199254740992.0)));
    CHECK(equal(I(1), D(1.0)) && equal(I(1), Dec("1.00")) && hash(I(1)) == hash(D(1.0)) && hash(I(1)) == hash(Dec("1.00")));
    CHECK(equal(D(0.1), Dec("0.1")) && hash(D(0.1)) == hash(Dec("0.1")));
    CHECK(!equal(S("1"), I(1)) && !equal(D(NAN), D(NAN)));
    int c = 0;
    CHECK(compare_relational(Dec("9007199254740993"), I(9007199254740992LL), c) && c > 0);
    CHECK(!compare_relational(D(NAN), I(1), c) && !compare_relational(S("1"), I(1), c));
    const Value min = negate(I(std::numeric_limits<std::int64_t>::min()));
    CHECK(min.is(Kind::Decimal) && decimal_spelling(min) == "9223372036854775808");
    CHECK(decimal_spelling(negate(Dec("-5"))) == "5" && negate(I(5)).as_int() == -5);
    CHECK(std::signbit(negate(D(0.0)).as_double()));
    CHECK(!truthy(I(0)) && !truthy(D(-0.0)) && !truthy(Dec("0.000")) && truthy(Dec("1e-400")));
    std::int64_t i = 0;
    CHECK(number_to_i64(Dec("-9223372036854775808"), i) && i == std::numeric_limits<std::int64_t>::min());
    CHECK(!number_to_i64(Dec("9223372036854775808"), i) && !number_to_i64(D(9223372036854775808.0), i));
    CHECK(number_is_integer(Dec("1.50e1")) && !number_is_integer(Dec("1.5")));
}

void decimals() {
    DecimalForm f;
    CHECK(parse_decimal("-001.2300e2", f) && f.sign < 0 && f.coefficient == "123" && f.exponent == 0);
    CHECK(parse_decimal("0.000", f) && f.sign == 0);
    CHECK(!parse_decimal("1.", f) && !parse_decimal(".5", f) && !parse_decimal("1e", f) && !parse_decimal("x", f));
    CHECK(decimal_fingerprint(decimal_form_of_double(2.0)) == "+2e0");
    CHECK(compare_decimal_forms(decimal_form_of_int(10), decimal_form_of_double(9.99)) > 0);
}

void errors_and_handles() {
    Value e = make_user_error("boom");
    Value m;
    CHECK(error_member(e, "message", m) && string_view_of(m) == "boom");
    CHECK(error_member(e, "code", m) && string_view_of(m) == "user.raised");
    CHECK(error_member(e, "category", m) && string_view_of(m) == "user");
    CHECK(error_member(e, "cause", m) && m.is_null() && !error_member(e, "secret", m));
    Value chained, bad;
    std::string err;
    CHECK(make_error("outer", "user.io.failed", e, "a.f", 3, 4, chained, err));
    CHECK(error_member(chained, "line", m) && m.as_int() == 3);
    CHECK(!make_error("x", "io.open_failed", Value(), {}, 0, 0, bad, err) && err == "invalid user Error code");
    CHECK(!make_error("x", "user.raised", S("no"), {}, 0, 0, bad, err));
    CHECK(equal(make_user_error("a"), make_user_error("a")) && !equal(make_user_error("a"), make_user_error("b")));
    CHECK(truthy(e));
    HandleRegistry files(HandleKind::File);
    Value h = files.add(std::make_shared<int>(5));
    CHECK(files.resolve(h) && *static_cast<int*>(files.resolve(h).get()) == 5);
    CHECK(files.release(h) && !files.resolve(h) && files.live() == 0);
    Value h2 = files.add(std::make_shared<int>(6));
    CHECK(h2.handle_id() == h.handle_id() && !files.resolve(h) && files.resolve(h2));  // stale handle fails
    HandleRegistry timers(HandleKind::Timer);
    CHECK(!timers.resolve(h2));  // foreign kind
    CHECK(!equal(make_string("\x1fnift:file:0"), h2));  // strings never act as handles
}

void functions_and_structs() {
    auto* f = new FunctionObject();
    f->name = "f";
    Value fn = Value::adopt(f);
    Value alias = fn;
    Value other = Value::adopt(new FunctionObject());
    CHECK(equal(fn, alias) && !equal(fn, other) && truthy(fn));
    auto* s = new StructObject(atom("Point"));
    s->fields.emplace_back(atom("x"), I(1));
    Value p = Value::adopt(s);
    Value q = p;  // reference semantics: shared, no COW
    q.heap_as<StructObject>()->fields[0].second = I(9);
    CHECK(p.heap_as<StructObject>()->fields[0].second.as_int() == 9 && equal(p, q));
}

} // namespace

int main() {
    layout_and_scalars();
    strings();
    arrays_cow();
    objects();
    numbers();
    decimals();
    errors_and_handles();
    functions_and_structs();
    return rt_test::finish("rt value unit");
}
