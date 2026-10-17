#include "rt/compare.h"

#include "rt/aggregate.h"
#include "rt/decimal.h"
#include "rt/heap.h"
#include "rt/number.h"
#include "rt/string.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nift::rt {

namespace {

bool numbers_equal(const Value& l, const Value& r) {
    if (l.is(Kind::Int) && r.is(Kind::Int)) return l.as_int() == r.as_int();
    if (l.is(Kind::Double) && r.is(Kind::Double)) return l.as_double() == r.as_double();
    const DecimalForm a = number_form(l), b = number_form(r);
    if (a.cls == NumericClass::NaN || b.cls == NumericClass::NaN) return false;
    return compare_decimal_forms(a, b) == 0;
}

bool errors_equal(const Value& l, const Value& r) {
    const auto* a = l.heap_as<ErrorObject>();
    const auto* b = r.heap_as<ErrorObject>();
    for (int depth = 0; a && b; ++depth) {
        if (depth >= 16) return false;
        if (a->message != b->message || a->code != b->code || a->category != b->category || a->source != b->source ||
            a->line != b->line || a->column != b->column)
            return false;
        a = a->cause.is(Kind::Error) ? a->cause.heap_as<ErrorObject>() : nullptr;
        b = b->cause.is(Kind::Error) ? b->cause.heap_as<ErrorObject>() : nullptr;
    }
    return !a && !b;
}

std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t hash_text(std::string_view text) {
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : text) { h ^= c; h *= 1099511628211ull; }
    return h;
}

// Equality compares exact decimal forms (a Double by its shortest round-trip
// spelling), so the hash must be a function of that form. Integers with
// |v| <= 2^53 take a cheap shortcut that every kind reaches for the same
// value: a Double whose shortest spelling is such an integer *is* that integer
// (all integers up to 2^53 are representable), and Int and Decimal are exact.
// Everything else, including integral doubles above 2^53 (2^62 spells
// 4.611686018427388e18, not ...904), hashes the canonical fingerprint.
std::uint64_t hash_number(const Value& number) {
    constexpr std::int64_t kExact = std::int64_t{1} << 53;
    std::int64_t i = 0;
    switch (number.kind()) {
        case Kind::Int:
            i = number.as_int();
            if (i >= -kExact && i <= kExact) return mix(2, static_cast<std::uint64_t>(i));
            break;
        case Kind::Double: {
            const double d = number.as_double();
            if (std::isfinite(d) && std::trunc(d) == d && std::fabs(d) <= static_cast<double>(kExact))
                return mix(2, static_cast<std::uint64_t>(static_cast<std::int64_t>(d)));
            break;
        }
        default:
            if (number_to_i64(number, i) && i >= -kExact && i <= kExact) return mix(2, static_cast<std::uint64_t>(i));
            break;
    }
    return mix(3, hash_text(decimal_fingerprint(number_form(number))));
}

} // namespace

namespace {

struct EqualPair {
    const Value* left;
    const Value* right;
    std::size_t depth;
};

enum class Shallow : std::uint8_t { Unequal, Equal, Descend };

// One level of legacy runtime_equal: scalars and identities are decided here;
// two distinct arrays/objects of equal size need their children compared.
Shallow shallow_equal(const Value& left, const Value& right, bool identity) {
    if (left.is_number() && right.is_number()) return numbers_equal(left, right) ? Shallow::Equal : Shallow::Unequal;
    if (left.kind() != right.kind()) return Shallow::Unequal;
    bool same = false;
    switch (left.kind()) {
        case Kind::Null: same = true; break;
        case Kind::Bool: same = left.as_bool() == right.as_bool(); break;
        case Kind::String: same = string_view_of(left) == string_view_of(right); break;
        case Kind::Bytes: same = bytes_of(left) == bytes_of(right); break;
        case Kind::Array:
            if (identity && left.heap() == right.heap()) return Shallow::Equal;
            return array_size(left) == array_size(right) ? Shallow::Descend : Shallow::Unequal;
        case Kind::Object:
            if (identity && left.heap() == right.heap()) return Shallow::Equal;
            return object_size(left) == object_size(right) ? Shallow::Descend : Shallow::Unequal;
        case Kind::Error: same = left.heap() == right.heap() || errors_equal(left, right); break;
        case Kind::Function: case Kind::Struct: same = left.heap() == right.heap(); break;
        case Kind::Handle:
            same = left.handle_kind() == right.handle_kind() && left.handle_id() == right.handle_id() &&
                   left.handle_generation() == right.handle_generation();
            break;
        default: break;
    }
    return same ? Shallow::Equal : Shallow::Unequal;
}

// Pushes the children of two equal-sized aggregates so they pop in legacy's
// order (array elements first to last; object entries in the left object's
// order, matched by key). A missing key is a pair with no right side, which
// decides the result when legacy would reach it.
void push_children(const EqualPair& pair, std::vector<EqualPair>& work) {
    const Value& l = *pair.left;
    const Value& r = *pair.right;
    if (l.is(Kind::Array)) {
        const auto& a = array_items(l);
        const auto& b = array_items(r);
        for (std::size_t i = a.size(); i-- > 0;) work.push_back({&a[i], &b[i], pair.depth + 1});
        return;
    }
    const auto& entries = object_entries(l);
    const std::size_t mark = work.size();
    for (const auto& entry : entries) {
        work.push_back({&entry.second, object_get(r, entry.first), pair.depth + 1});
    }
    std::reverse(work.begin() + static_cast<std::ptrdiff_t>(mark), work.end());
}

} // namespace

// Iterative (heap worklist, CP11 review round 2, R6): no nesting depth can
// exhaust the C++ stack. Pairs are visited depth-first in legacy's order, so
// when `max_depth` stops the walk, legacy (whose recursion goes deeper before
// it crashes) has not yet decided either. A bounded walk also descends into
// two references to the same aggregate, as legacy does.
Equality equal_within(const Value& left, const Value& right, std::size_t max_depth) {
    const bool identity = max_depth == static_cast<std::size_t>(-1);
    std::vector<EqualPair> work;
    EqualPair pair{&left, &right, 0};
    for (;;) {
        const Shallow s = pair.right ? shallow_equal(*pair.left, *pair.right, identity) : Shallow::Unequal;
        if (s == Shallow::Unequal) return Equality::Unequal;
        if (s == Shallow::Descend) {
            if (pair.depth >= max_depth) return Equality::TooDeep;
            push_children(pair, work);
        }
        if (work.empty()) return Equality::Equal;
        pair = work.back();
        work.pop_back();
    }
}

bool equal(const Value& left, const Value& right) {
    return equal_within(left, right, static_cast<std::size_t>(-1)) == Equality::Equal;
}

bool compare_relational(const Value& left, const Value& right, int& comparison) {
    if (left.is_number() && right.is_number()) {
        if (left.is(Kind::Int) && right.is(Kind::Int)) {
            comparison = left.as_int() < right.as_int() ? -1 : (left.as_int() > right.as_int() ? 1 : 0);
            return true;
        }
        const DecimalForm a = number_form(left), b = number_form(right);
        if (a.cls == NumericClass::NaN || b.cls == NumericClass::NaN) return false;
        comparison = compare_decimal_forms(a, b);
        return true;
    }
    if (left.is(Kind::String) && right.is(Kind::String)) {
        const int c = string_view_of(left).compare(string_view_of(right));
        comparison = c < 0 ? -1 : (c > 0 ? 1 : 0);
        return true;
    }
    return false;
}

bool truthy(const Value& value) {
    switch (value.kind()) {
        case Kind::Null: return false;
        case Kind::Bool: return value.as_bool();
        case Kind::Int: case Kind::Double: case Kind::Decimal: return !number_is_zero(value);
        case Kind::String: return !string_view_of(value).empty();
        case Kind::Bytes: return !bytes_of(value).empty();
        case Kind::Array: return array_size(value) != 0;
        case Kind::Object: return object_size(value) != 0;
        default: return true;  // functions, structs, handles, errors
    }
}

namespace {

std::uint64_t leaf_hash(const Value& value) {
    switch (value.kind()) {
        case Kind::Null: return 0x6e756c6cull;
        case Kind::Bool: return value.as_bool() ? 0x7472ull : 0x6661ull;
        case Kind::Int: case Kind::Double: case Kind::Decimal: return hash_number(value);
        case Kind::String: return mix(5, hash_text(string_view_of(value)));
        case Kind::Bytes: {
            const auto& b = bytes_of(value);
            return mix(7, hash_text(std::string_view(reinterpret_cast<const char*>(b.data()), b.size())));
        }
        case Kind::Error: return mix(13, hash_text(value.heap_as<ErrorObject>()->message));
        case Kind::Handle: return mix(mix(12, value.handle_id()), value.handle_generation());
        default: return mix(static_cast<std::uint64_t>(value.kind()), reinterpret_cast<std::uintptr_t>(value.heap()));
    }
}

// A partially hashed aggregate: arrays fold children in order
// (h = mix(h, child)); objects add mix(key, child) per entry.
struct HashFrame {
    const Value* value;
    std::size_t next;
    std::uint64_t h;
};

std::size_t child_count(const Value& v) { return v.is(Kind::Array) ? array_size(v) : object_size(v); }

const Value& child_at(const Value& v, std::size_t i) {
    return v.is(Kind::Array) ? array_items(v)[i] : object_entries(v)[i].second;
}

std::uint64_t combine(const Value& parent, std::size_t i, std::uint64_t h, std::uint64_t child) {
    if (parent.is(Kind::Array)) return mix(h, child);
    return h + mix(hash_text(atom_text(object_entries(parent)[i].first)), child);
}

bool is_aggregate(const Value& v) { return v.is(Kind::Array) || v.is(Kind::Object); }

} // namespace

// Iterative post-order (CP11 review round 2, R6); same values as the former
// recursive definition.
std::uint64_t hash(const Value& value) {
    if (!is_aggregate(value)) return leaf_hash(value);
    std::vector<HashFrame> stack{{&value, 0, value.is(Kind::Array) ? 8u : 9u}};
    for (;;) {
        HashFrame& top = stack.back();
        if (top.next < child_count(*top.value)) {
            const Value& child = child_at(*top.value, top.next);
            if (is_aggregate(child)) {
                stack.push_back({&child, 0, child.is(Kind::Array) ? 8u : 9u});
                continue;
            }
            top.h = combine(*top.value, top.next, top.h, leaf_hash(child));
            ++top.next;
            continue;
        }
        const std::uint64_t done = top.h;
        stack.pop_back();
        if (stack.empty()) return done;
        HashFrame& parent = stack.back();
        parent.h = combine(*parent.value, parent.next, parent.h, done);
        ++parent.next;
    }
}

} // namespace nift::rt
