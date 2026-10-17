// CP09 randomised property and differential tests for src/rt.
//
//   value_property [seed] [scale]
//
// 1. numeric tower vs the legacy canonical operator (tests/rt/legacy_oracle.h)
// 2. equality / ordering / truthiness vs legacy runtime_equal & co., and
//    hash consistency (equal => same hash)
// 3. legacy <-> rt conversion round trips (value, type and key order), objects
//    up to 64 keys (beyond the 8-key index threshold), duplicate-key rejection
// 4. copy-on-write aliasing against a deep-copy model, for arrays and objects
// 5. object set/remove/re-add sequences (up to 64 keys) against an ordered
//    model, checking order, every lookup, aliases, equality and hash
//
// Numbers include integral doubles above 2^53, Decimal spellings with
// exponents of any size (around the 10^18 small/big split and far beyond),
// and second operands derived from the first as the same value in another
// kind or spelling, so the `equal => same hash` check is exercised on equal
// pairs, not only on rare coincidences.
#include "legacy_oracle.h"
#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/decimal.h"
#include "rt/legacy_bridge.h"
#include "rt/number.h"
#include "rt/string.h"
#include "rt_test.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace nift::rt;
using nift::RuntimeValue;

namespace {

std::mt19937_64 rng;
std::uint64_t below(std::uint64_t n) { return std::uniform_int_distribution<std::uint64_t>(0, n - 1)(rng); }

std::int64_t random_int() {
    static const std::int64_t edges[] = {0, 1, -1, 2, 3, -7, 7, 10, 9007199254740992LL, 9007199254740993LL,
                                         -9007199254740993LL, 4611686018427387904LL, 3037000499LL, 3037000500LL,
                                         std::numeric_limits<std::int64_t>::max(),
                                         std::numeric_limits<std::int64_t>::min(),
                                         std::numeric_limits<std::int64_t>::max() - 1};
    switch (below(4)) {
        case 0: return edges[below(sizeof edges / sizeof edges[0])];
        case 1: return static_cast<std::int64_t>(below(2001)) - 1000;
        case 2: return static_cast<std::int64_t>(rng());
        default: return static_cast<std::int64_t>(rng() >> below(64));
    }
}

double random_double() {
    static const double edges[] = {0.0, -0.0, 0.5, -0.5, 0.1, 0.2, 1.5, 2.0, 3.0, 1e15, 1e16, 9007199254740992.0,
                                   9223372036854775808.0, -9223372036854775808.0, 1e19, 1e21, 1e300, -1e300,
                                   5e-324, 1e-7, 123456.789};
    switch (below(5)) {
        case 0: return edges[below(sizeof edges / sizeof edges[0])];
        case 1: return static_cast<double>(static_cast<std::int64_t>(below(2001)) - 1000) / 4.0;
        case 2: { double d; std::uint64_t bits = rng(); std::memcpy(&d, &bits, 8); return std::isfinite(d) ? d : 1.25; }
        case 3: return std::ldexp(static_cast<double>(rng() >> 11), static_cast<int>(below(140)) - 70);
        default: {  // integral doubles above 2^53, where the shortest spelling differs from the exact value
            const double d = std::ldexp(static_cast<double>((rng() >> 11) | 1), 1 + static_cast<int>(below(below(4) ? 20 : 960)));
            return below(2) ? d : -d;
        }
    }
}

// Exponent text: small, around the 10^18 small/big split of DecimalForm, or
// far beyond int64 (legacy keeps exponents exact at any size).
std::string random_exponent() {
    static const char* edges[] = {"999999999999999999", "1000000000000000000", "1000000000000000001",
                                  "1000000000000000002", "999999999999999998", "9223372036854775807",
                                  "9223372036854775808", "18446744073709551616", "99999999999999999999",
                                  "000000000000000000000000000000000000000007", "230000000000000000"};
    std::string s = below(2) ? "-" : (below(4) == 0 ? "+" : "");
    switch (below(4)) {
        case 0: return s + edges[below(sizeof edges / sizeof edges[0])];
        case 1: {
            std::string digits = std::to_string(1 + below(9));
            for (std::size_t i = 0, n = below(40); i < n; ++i) digits.push_back(static_cast<char>('0' + below(10)));
            return s + digits;
        }
        default: return s + std::to_string(below(30));
    }
}

std::string random_decimal_spelling() {
    std::string s = below(3) == 0 ? "-" : "";
    const std::size_t digits = 1 + below(25);
    const bool zero = below(8) == 0;
    for (std::size_t i = 0; i < digits; ++i) s.push_back(static_cast<char>(zero ? '0' : '0' + below(10)));
    if (below(3) == 0) {
        s.push_back('.');
        for (std::size_t i = 0, n = 1 + below(6); i < n; ++i) s.push_back(zero ? '0' : '0' + below(10));
    }
    if (below(3) == 0) s += (below(2) ? "e" : "E") + random_exponent();
    return s;
}

Value decimal_of(const std::string& spelling) { return make_decimal(spelling, std::strtod(spelling.c_str(), nullptr)); }

// The value of `a` (a Double compares by its shortest spelling) as another
// kind or spelling: scientific Decimal, expanded integer Decimal or Int when
// integral and small enough, or the Double nearest to it. Exercises
// `equal => same hash` across kinds; the legacy oracle still decides equality.
Value equivalent_of(const Value& a) {
    const DecimalForm f = number_form(a);
    if (f.cls != NumericClass::Finite) return a;
    const std::string sign = f.sign < 0 ? "-" : "";
    const std::string exponent = f.big_exponent.empty() ? std::to_string(f.exponent) : f.big_exponent;
    const std::string scientific = f.sign == 0 ? (below(2) ? "0e99999999999999999999" : "-0.000")
                                               : sign + f.coefficient + "e" + exponent;
    std::string digits;
    switch (below(4)) {
        case 0: {  // scientific spelling with a shifted exponent: c0e(x-1)
            if (f.sign == 0 || !f.big_exponent.empty()) return decimal_of(scientific);
            return decimal_of(sign + f.coefficient + "0e" + std::to_string(f.exponent - 1));
        }
        case 1:
            if (decimal_integer_digits(f, 40, digits)) {
                std::int64_t i = 0;
                if (below(2) && number_to_i64(decimal_of(sign + digits), i)) return Value::integer(i);
                return decimal_of(sign + digits);
            }
            return decimal_of(scientific);
        case 2: return Value::number(std::strtod((sign + f.coefficient + "e" + exponent).c_str(), nullptr));
        default: return decimal_of(sign + f.coefficient + "e" + exponent);
    }
}

Value random_number() {
    switch (below(3)) {
        case 0: return Value::integer(random_int());
        case 1: return Value::number(random_double());
        default: return decimal_of(random_decimal_spelling());
    }
}

RuntimeValue legacy_of(const Value& v) {
    RuntimeValue out;
    std::string err;
    if (!to_legacy(v, out, err)) { ++rt_test::failures(); std::fprintf(stderr, "to_legacy failed: %s\n", err.c_str()); }
    return out;
}

bool same_number_result(const Value& rt_out, const RuntimeValue& legacy_out) {
    if (!nift::runtime_equal(legacy_of(rt_out), legacy_out)) return false;
    if (rt_out.is(Kind::Double) && legacy_out.type == nift::RuntimeType::Number) {
        const double a = rt_out.as_double(), b = legacy_out.num;
        return std::memcmp(&a, &b, 8) == 0;  // same bits, including -0.0
    }
    return true;
}

void numeric_differential(long iterations) {
    const ArithOp ops[] = {ArithOp::Add, ArithOp::Sub, ArithOp::Mul, ArithOp::Div, ArithOp::Mod};
    const char names[] = {'+', '-', '*', '/', '%'};
    long mismatches = 0;
    for (long i = 0; i < iterations; ++i) {
        const Value a = random_number();
        const Value b = below(4) == 0 ? equivalent_of(a) : random_number();
        const RuntimeValue la = legacy_of(a), lb = legacy_of(b);
        const std::size_t k = below(5);
        Value out;
        RuntimeValue lout;
        std::string err, lerr;
        const bool ok = arithmetic(ops[k], a, b, out, err);
        const bool lok = legacy_oracle::numeric_binary(la, lb, names[k], lout, lerr);
        bool same = ok == lok && (ok ? same_number_result(out, lout) : err == lerr);
        // Unary minus, equality, ordering, truthiness, hash consistency.
        same = same && nift::runtime_equal(legacy_of(negate(a)), nift::runtime_number_negate(la));
        same = same && equal(a, b) == nift::runtime_equal(la, lb) && truthy(a) == nift::runtime_truthy(la);
        int c = 0, lc = 0;
        const bool cmp = compare_relational(a, b, c), lcmp = nift::runtime_compare_numbers_relational(la, lb, lc);
        same = same && cmp == lcmp && (!cmp || c == lc);
        if (equal(a, b)) same = same && hash(a) == hash(b);
        std::int64_t x = 0, lx = 0;
        same = same && number_to_i64(a, x) == nift::runtime_number_to_i64(la, lx) && x == lx;
        CHECK(same);
        if (!same && ++mismatches <= 5)
            std::fprintf(stderr, "  numeric mismatch: %s %c %s\n", la.dump().c_str(), names[k], lb.dump().c_str());
    }
}

std::string pool_key(std::size_t i) {
    static const char* named[] = {"a", "b", "c", "id", "name", "a long key beyond inline"};
    return i < 6 ? named[i] : "k" + std::to_string(i - 6);
}

// Objects: usually 0-3 keys from a small pool; sometimes (near the root) up to
// 64 keys from an 80-key pool, beyond the 8-entry index threshold. With
// `duplicates`, an object occasionally gets a repeated key appended directly
// (legacy operator[] never does that; only host C++ can).
RuntimeValue random_legacy(int depth, bool duplicates = false) {
    static const char* texts[] = {"", "x", "y", "1", "hello", "a string longer than fourteen bytes"};
    const std::size_t pick = depth >= 3 ? below(5) : below(7);
    switch (pick) {
        case 0: return RuntimeValue(nullptr);
        case 1: return RuntimeValue(below(2) == 0);
        case 2: return legacy_of(random_number());
        case 3: return RuntimeValue(std::string(texts[below(6)]));
        case 4: return RuntimeValue(static_cast<double>(below(3)));
        case 5: {
            RuntimeValue a = RuntimeValue::make_array();
            for (std::size_t i = 0, n = below(4); i < n; ++i) a.push_back(random_legacy(depth + 1, duplicates));
            return a;
        }
        default: {
            RuntimeValue o = RuntimeValue::make_object();
            const bool large = depth <= 1 && below(6) == 0;
            const std::size_t n = large ? 1 + below(64) : below(4), keys = large ? 86 : 6;
            for (std::size_t i = 0; i < n; ++i) o[pool_key(below(keys))] = random_legacy(large ? 3 : depth + 1, duplicates);
            if (duplicates && !o.object.empty() && below(8) == 0)
                o.object.emplace_back(o.object[below(o.object.size())].first, random_legacy(3));
            return o;
        }
    }
}

bool has_duplicate_key(const RuntimeValue& v) {
    for (const auto& item : v.array) if (has_duplicate_key(item)) return true;
    for (std::size_t i = 0; i < v.object.size(); ++i) {
        if (has_duplicate_key(v.object[i].second)) return true;
        for (std::size_t j = 0; j < i; ++j) if (v.object[j].first == v.object[i].first) return true;
    }
    return false;
}

RuntimeValue shuffled(const RuntimeValue& v) {
    RuntimeValue copy = v;
    if (copy.is_object() && copy.object.size() > 1) std::shuffle(copy.object.begin(), copy.object.end(), rng);
    for (auto& item : copy.array) item = shuffled(item);
    for (auto& entry : copy.object) entry.second = shuffled(entry.second);
    return copy;
}

// Same type, value and object key order (runtime_equal ignores key order).
bool identical(const RuntimeValue& a, const RuntimeValue& b) {
    if (a.type != b.type || nift::runtime_fingerprint(a) != nift::runtime_fingerprint(b)) return false;
    if (a.type == nift::RuntimeType::Number) return std::memcmp(&a.num, &b.num, 8) == 0;
    if (a.type == nift::RuntimeType::StrNumber) return a.string == b.string;
    if (a.is_array())
        for (std::size_t i = 0; i < a.array.size(); ++i) if (!identical(a.array[i], b.array[i])) return false;
    if (a.is_object())
        for (std::size_t i = 0; i < a.object.size(); ++i)
            if (a.object[i].first != b.object[i].first || !identical(a.object[i].second, b.object[i].second)) return false;
    return true;
}

void structural_differential(long iterations) {
    for (long i = 0; i < iterations; ++i) {
        const RuntimeValue la = random_legacy(0, true);
        const RuntimeValue lb = below(3) == 0 ? shuffled(la) : random_legacy(0, true);
        Value a, b;
        std::string err;
        const bool aok = from_legacy(la, a, err), bok = from_legacy(lb, b, err);
        // Duplicate keys (anywhere in the tree) are rejected, everything else converts.
        CHECK(aok == !has_duplicate_key(la) && bok == !has_duplicate_key(lb));
        if (!aok || !bok) continue;
        CHECK(identical(legacy_of(a), la));  // round trip keeps value, kind and order
        CHECK(equal(a, b) == nift::runtime_equal(la, lb));
        CHECK(truthy(a) == nift::runtime_truthy(la));
        if (equal(a, b)) CHECK(hash(a) == hash(b));
    }
}

// Copy-on-write: random aliasing and mutation must behave exactly like deep
// copies (the legacy model).
void cow_model(long sequences) {
    for (long s = 0; s < sequences; ++s) {
        std::vector<Value> slots(4);
        std::vector<RuntimeValue> model(4);
        for (std::size_t i = 0; i < 4; ++i) {
            model[i] = RuntimeValue::make_array();
            model[i].push_back(RuntimeValue(static_cast<double>(i)));
            std::string err;
            from_legacy(model[i], slots[i], err);
        }
        for (int step = 0; step < 40; ++step) {
            const std::size_t x = below(4), y = below(4);
            switch (below(5)) {
                case 0: slots[x] = slots[y]; model[x] = model[y]; break;
                case 1: {
                    const double v = static_cast<double>(below(100));
                    array_push(slots[x], Value::number(v));
                    model[x].push_back(RuntimeValue(v));
                    break;
                }
                case 2: if (!model[x].array.empty()) { array_mut(slots[x]).pop_back(); model[x].array.pop_back(); } break;
                case 3: {  // nest a copy of y inside x, then mutate y
                    array_push(slots[x], slots[y]);
                    model[x].push_back(model[y]);
                    array_push(slots[y], Value::boolean(true));
                    model[y].push_back(RuntimeValue(true));
                    break;
                }
                default: {  // mutate a nested element in place
                    auto& items = array_mut(slots[x]);
                    if (!items.empty() && items.back().is(Kind::Array)) {
                        array_push(items.back(), Value());
                        model[x].array.back().push_back(RuntimeValue(nullptr));
                    }
                }
            }
            for (std::size_t i = 0; i < 4; ++i) CHECK(identical(legacy_of(slots[i]), model[i]));
        }
    }
}

void legacy_remove(RuntimeValue& object, const std::string& key) {
    for (auto it = object.object.begin(); it != object.object.end(); ++it)
        if (it->first == key) { object.object.erase(it); return; }
}

// Objects: random set / remove / re-add / alias / nest sequences over 4 slots
// with up to ~70 distinct keys (the index is built from 8 entries and rebuilt
// on growth and removal), against legacy objects with deep-copy semantics.
// Every step checks every slot's entries in order and, for the mutated slot,
// the lookup of every key in the keyspace (present and absent). Every 8
// steps, equality and hash against a reverse-order rebuild.
void object_model(long sequences) {
    for (long s = 0; s < sequences; ++s) {
        std::vector<Value> slots(4);
        std::vector<RuntimeValue> model(4);
        for (std::size_t i = 0; i < 4; ++i) { slots[i] = make_object(); model[i] = RuntimeValue::make_object(); }
        const std::size_t keyspace = 4 + below(67);
        for (int step = 0; step < 120; ++step) {
            const std::size_t x = below(4), y = below(4);
            const std::string key = pool_key(below(keyspace));
            const Atom k = atom(key);
            const std::uint64_t op = below(16);
            if (op < 7) {  // set (insert at end or replace in place)
                const double v = static_cast<double>(below(1000));
                object_set(slots[x], k, Value::number(v));
                model[x][key] = RuntimeValue(v);
            } else if (op < 10) {  // remove (present or absent)
                const bool had = model[x].find_member(key) != nullptr;
                CHECK(object_remove(slots[x], k) == had);
                legacy_remove(model[x], key);
            } else if (op < 12) {  // re-add: remove then set moves the key to the end
                object_remove(slots[x], k);
                legacy_remove(model[x], key);
                object_set(slots[x], k, Value::boolean(true));
                model[x][key] = RuntimeValue(true);
            } else if (op < 13) {  // alias
                slots[x] = slots[y];
                model[x] = model[y];
            } else if (op < 14) {  // nest a copy of y in x, then mutate y
                object_set(slots[x], k, slots[y]);
                const RuntimeValue snapshot = model[y];  // x may be y: copy before inserting
                model[x][key] = snapshot;
                object_set(slots[y], atom("k0"), Value::null());
                model[y]["k0"] = RuntimeValue(nullptr);
            } else {  // mutate a nested object through its parent (copy out, set, store back)
                const Value* child = object_get(slots[x], k);
                if (child && child->is(Kind::Object)) {
                    Value copy = *child;
                    object_set(copy, atom("k1"), Value::number(7.0));
                    object_set(slots[x], k, copy);
                    model[x][key]["k1"] = RuntimeValue(7.0);
                }
            }
            for (std::size_t i = 0; i < 4; ++i) CHECK(identical(legacy_of(slots[i]), model[i]));
            for (std::size_t j = 0; j < keyspace; ++j) {
                const Value* got = object_get(slots[x], atom(pool_key(j)));
                const RuntimeValue* want = model[x].find_member(pool_key(j));
                CHECK((got == nullptr) == (want == nullptr) && (!got || identical(legacy_of(*got), *want)));
            }
            if (step % 8 == 0) {
                Value rebuilt = make_object();
                const auto& entries = object_entries(slots[x]);
                for (std::size_t j = entries.size(); j-- > 0;) object_set(rebuilt, entries[j].first, entries[j].second);
                CHECK(equal(slots[x], rebuilt) && hash(slots[x]) == hash(rebuilt));
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 20261017;
    const long scale = argc > 2 ? std::strtol(argv[2], nullptr, 10) : 1;
    rng.seed(seed);
    std::printf("seed %llu scale %ld\n", static_cast<unsigned long long>(seed), scale);
    numeric_differential(200000 * scale);
    structural_differential(20000 * scale);
    cow_model(500 * scale);
    object_model(300 * scale);
    return rt_test::finish("rt value property");
}
