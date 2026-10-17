// CP09 randomised property and differential tests for src/rt.
//
//   value_property [seed] [scale]
//
// 1. numeric tower vs the legacy canonical operator (tests/rt/legacy_oracle.h)
// 2. equality / ordering / truthiness vs legacy runtime_equal & co., and
//    hash consistency (equal => same hash)
// 3. legacy <-> rt conversion round trips (value, type and key order)
// 4. copy-on-write aliasing against a deep-copy model
#include "legacy_oracle.h"
#include "rt/aggregate.h"
#include "rt/compare.h"
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
    switch (below(4)) {
        case 0: return edges[below(sizeof edges / sizeof edges[0])];
        case 1: return static_cast<double>(static_cast<std::int64_t>(below(2001)) - 1000) / 4.0;
        case 2: { double d; std::uint64_t bits = rng(); std::memcpy(&d, &bits, 8); return std::isfinite(d) ? d : 1.25; }
        default: return std::ldexp(static_cast<double>(rng() >> 11), static_cast<int>(below(140)) - 70);
    }
}

std::string random_decimal_spelling() {
    std::string s = below(3) == 0 ? "-" : "";
    const std::size_t digits = 1 + below(25);
    for (std::size_t i = 0; i < digits; ++i) s.push_back(static_cast<char>('0' + below(10)));
    if (below(3) == 0) { s.push_back('.'); for (std::size_t i = 0, n = 1 + below(6); i < n; ++i) s.push_back('0' + below(10)); }
    if (below(4) == 0) { s.push_back('e'); if (below(2)) s.push_back('-'); s += std::to_string(below(30)); }
    return s;
}

Value random_number() {
    switch (below(3)) {
        case 0: return Value::integer(random_int());
        case 1: return Value::number(random_double());
        default: { std::string s = random_decimal_spelling(); return make_decimal(s, std::strtod(s.c_str(), nullptr)); }
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
        const Value a = random_number(), b = random_number();
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

RuntimeValue random_legacy(int depth) {
    static const char* keys[] = {"a", "b", "c", "id", "name", "a long key beyond inline"};
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
            for (std::size_t i = 0, n = below(4); i < n; ++i) a.push_back(random_legacy(depth + 1));
            return a;
        }
        default: {
            RuntimeValue o = RuntimeValue::make_object();
            for (std::size_t i = 0, n = below(4); i < n; ++i) o[keys[below(6)]] = random_legacy(depth + 1);
            return o;
        }
    }
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
        const RuntimeValue la = random_legacy(0);
        const RuntimeValue lb = below(3) == 0 ? shuffled(la) : random_legacy(0);
        Value a, b;
        std::string err;
        CHECK(from_legacy(la, a, err) && from_legacy(lb, b, err));
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

} // namespace

int main(int argc, char** argv) {
    const std::uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 20261017;
    const long scale = argc > 2 ? std::strtol(argv[2], nullptr, 10) : 1;
    rng.seed(seed);
    std::printf("seed %llu scale %ld\n", static_cast<unsigned long long>(seed), scale);
    numeric_differential(200000 * scale);
    structural_differential(20000 * scale);
    cow_model(500 * scale);
    return rt_test::finish("rt value property");
}
