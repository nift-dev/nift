#include "rt/compare.h"

#include "rt/aggregate.h"
#include "rt/decimal.h"
#include "rt/heap.h"
#include "rt/number.h"
#include "rt/string.h"

#include <cmath>
#include <vector>

namespace nift::rt {

namespace {

DecimalForm form_of(const Value& number) {
    switch (number.kind()) {
        case Kind::Int: return decimal_form_of_int(number.as_int());
        case Kind::Double: return decimal_form_of_double(number.as_double());
        default: {
            DecimalForm form;
            if (!parse_decimal(decimal_spelling(number), form)) return decimal_form_of_double(decimal_approx(number));
            return form;
        }
    }
}

bool numbers_equal(const Value& l, const Value& r) {
    if (l.is(Kind::Int) && r.is(Kind::Int)) return l.as_int() == r.as_int();
    if (l.is(Kind::Double) && r.is(Kind::Double)) return l.as_double() == r.as_double();
    const DecimalForm a = form_of(l), b = form_of(r);
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

// Legacy object equality is an unordered match of (key, value) pairs.
bool objects_equal(const Value& l, const Value& r) {
    const auto& a = object_entries(l);
    const auto& b = object_entries(r);
    if (a.size() != b.size()) return false;
    for (const auto& entry : a) {
        const Value* other = object_get(r, entry.first);
        if (!other || !equal(entry.second, *other)) return false;
    }
    return true;
}

bool arrays_equal(const Value& l, const Value& r) {
    const auto& a = array_items(l);
    const auto& b = array_items(r);
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (!equal(a[i], b[i])) return false;
    return true;
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

std::uint64_t hash_number(const Value& number) {
    // Integers (whatever their kind) hash by value so 1, 1.0 and "1.00" agree;
    // other numbers hash their canonical decimal form.
    std::int64_t i = 0;
    if (number_to_i64(number, i)) return mix(2, static_cast<std::uint64_t>(i));
    return mix(3, hash_text(decimal_fingerprint(form_of(number))));
}

} // namespace

bool equal(const Value& left, const Value& right) {
    if (left.is_number() && right.is_number()) return numbers_equal(left, right);
    if (left.kind() != right.kind()) return false;
    switch (left.kind()) {
        case Kind::Null: return true;
        case Kind::Bool: return left.as_bool() == right.as_bool();
        case Kind::String: return string_view_of(left) == string_view_of(right);
        case Kind::Bytes: return bytes_of(left) == bytes_of(right);
        case Kind::Array: return left.heap() == right.heap() || arrays_equal(left, right);
        case Kind::Object: return left.heap() == right.heap() || objects_equal(left, right);
        case Kind::Error: return left.heap() == right.heap() || errors_equal(left, right);
        case Kind::Function: case Kind::Struct: return left.heap() == right.heap();
        case Kind::Handle:
            return left.handle_kind() == right.handle_kind() && left.handle_id() == right.handle_id() &&
                   left.handle_generation() == right.handle_generation();
        default: return false;
    }
}

bool compare_relational(const Value& left, const Value& right, int& comparison) {
    if (left.is_number() && right.is_number()) {
        if (left.is(Kind::Int) && right.is(Kind::Int)) {
            comparison = left.as_int() < right.as_int() ? -1 : (left.as_int() > right.as_int() ? 1 : 0);
            return true;
        }
        const DecimalForm a = form_of(left), b = form_of(right);
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

std::uint64_t hash(const Value& value) {
    switch (value.kind()) {
        case Kind::Null: return 0x6e756c6cull;
        case Kind::Bool: return value.as_bool() ? 0x7472ull : 0x6661ull;
        case Kind::Int: case Kind::Double: case Kind::Decimal: return hash_number(value);
        case Kind::String: return mix(5, hash_text(string_view_of(value)));
        case Kind::Bytes: {
            const auto& b = bytes_of(value);
            return mix(7, hash_text(std::string_view(reinterpret_cast<const char*>(b.data()), b.size())));
        }
        case Kind::Array: {
            std::uint64_t h = 8;
            for (const auto& item : array_items(value)) h = mix(h, hash(item));
            return h;
        }
        case Kind::Object: {
            std::uint64_t h = 9;  // order-insensitive: sum of entry hashes
            for (const auto& entry : object_entries(value))
                h += mix(hash_text(atom_text(entry.first)), hash(entry.second));
            return h;
        }
        case Kind::Error: return mix(13, hash_text(value.heap_as<ErrorObject>()->message));
        case Kind::Handle: return mix(mix(12, value.handle_id()), value.handle_generation());
        default: return mix(static_cast<std::uint64_t>(value.kind()), reinterpret_cast<std::uintptr_t>(value.heap()));
    }
}

} // namespace nift::rt
