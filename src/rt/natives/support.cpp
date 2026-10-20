#include "rt/natives/support.h"

#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/decimal.h"
#include "rt/heap_account.h"
#include "rt/number.h"
#include "rt/string.h"

#include <charconv>
#include <cmath>
#include <limits>

namespace nift::rt::natives {

bool Context::over_budget() const noexcept { return heap_limit != 0 && live_heap_bytes() > heap_limit; }

Status fail(std::string& error, std::string message) {
    error = static_cast<std::string&&>(message);
    return Status::Error;
}

bool no_arguments(std::uint32_t argc, std::string_view name, std::string& error) {
    if (argc == 1) return true;
    error.assign(name.data(), name.size());
    error += ": expected no arguments";
    return false;
}

bool number_to_size(const Value& value, std::size_t& out) {
    switch (value.kind()) {
        case Kind::Int:
            if (value.as_int() < 0) return false;
            out = static_cast<std::size_t>(value.as_int());
            return true;
        case Kind::Double: {
            const double d = value.as_double();
            if (!std::isfinite(d) || d < 0.0 || std::trunc(d) != d ||
                d >= static_cast<double>(std::numeric_limits<std::size_t>::max()))
                return false;
            out = static_cast<std::size_t>(d);
            return true;
        }
        case Kind::Decimal: {
            const DecimalForm form = number_form(value);
            if (form.sign < 0) return false;
            static const std::string maximum = std::to_string(std::numeric_limits<std::size_t>::max());
            std::string digits;
            if (!decimal_integer_digits(form, maximum.size(), digits)) return false;
            if (digits.size() > maximum.size() || (digits.size() == maximum.size() && digits > maximum)) return false;
            std::size_t result = 0;
            for (char c : digits) result = result * 10 + static_cast<std::size_t>(c - '0');
            out = result;
            return true;
        }
        default: return false;
    }
}

int compare_numbers(const Value& left, const Value& right) {
    const bool left_nan = left.is(Kind::Double) && std::isnan(left.as_double());
    const bool right_nan = right.is(Kind::Double) && std::isnan(right.as_double());
    if (left_nan || right_nan) return left_nan == right_nan ? 0 : (left_nan ? 1 : -1);
    int comparison = 0;
    compare_relational(left, right, comparison);
    return comparison;
}

std::size_t utf8_sequence(std::string_view text, std::size_t at) {
    const unsigned char c = static_cast<unsigned char>(text[at]);
    const std::size_t n = c < 0x80                ? 1
                          : c >= 0xC2 && c <= 0xDF ? 2
                          : c >= 0xE0 && c <= 0xEF ? 3
                          : c >= 0xF0 && c <= 0xF4 ? 4
                                                   : 0;
    if (n == 0 || at + n > text.size()) return 0;
    for (std::size_t j = 1; j < n; ++j)
        if ((static_cast<unsigned char>(text[at + j]) & 0xC0) != 0x80) return 0;
    return n;
}

bool utf8_count(std::string_view text, std::size_t& count) {
    count = 0;
    for (std::size_t i = 0; i < text.size();) {
        // ASCII runs are the common case.
        if (static_cast<unsigned char>(text[i]) < 0x80) { ++i; ++count; continue; }
        const std::size_t n = utf8_sequence(text, i);
        if (n == 0) return false;
        i += n;
        ++count;
    }
    return true;
}

bool renderable_scalar(const Value& value) {
    switch (value.kind()) {
        case Kind::Null: case Kind::Bool: case Kind::Int: case Kind::Double: case Kind::Decimal: case Kind::String:
            return true;
        default: return false;
    }
}

namespace {

// The rendering of a number: in `buffer` for Int and Double, the stored
// spelling for a Decimal.
std::string_view render_number(const Value& value, char (&buffer)[64]) {
    std::to_chars_result converted{buffer, std::errc()};
    if (value.is(Kind::Int)) {
        converted = std::to_chars(buffer, buffer + sizeof buffer, value.as_int());
    } else if (value.is(Kind::Double)) {
        // Jsonic++ format_number (legacy RuntimeValue::dump): integral values in
        // int64 range as integers ("-0.0" is "0"), others shortest round trip in
        // general format ("1e+21", "1e-07").
        const double d = value.as_double();
        if (std::floor(d) == d && d >= -9223372036854775808.0 && d < 9223372036854775808.0)
            converted = std::to_chars(buffer, buffer + sizeof buffer, static_cast<long long>(d));
        else
            converted = std::to_chars(buffer, buffer + sizeof buffer, d, std::chars_format::general);
        if (converted.ec != std::errc()) { buffer[0] = '0'; return std::string_view(buffer, 1); }
    } else {
        return decimal_spelling(value);  // a StrNumber dumps its spelling
    }
    return std::string_view(buffer, static_cast<std::size_t>(converted.ptr - buffer));
}

} // namespace

void append_number(const Value& value, std::string& out) {
    char buffer[64];
    const std::string_view text = render_number(value, buffer);
    out.append(text.data(), text.size());
}

std::size_t size_add(std::size_t a, std::size_t b) noexcept {
    return a > kSizeOverflow - b ? kSizeOverflow : a + b;
}

std::size_t size_mul(std::size_t a, std::size_t b) noexcept {
    return b != 0 && a > kSizeOverflow / b ? kSizeOverflow : a * b;
}

bool reserve_result(const Context& context, std::size_t bytes) noexcept {
    if (bytes >= kMaxResultBytes) return false;  // overflowed, or beyond any std::string
    if (context.heap_limit == 0) return true;    // budget disabled (unit tests)
    // Rounded as the heap string will be counted (terminator, header, granule).
    const std::size_t counted = size_add(bytes, 1 + kHeapGranule + 64);
    const std::size_t live = live_heap_bytes();
    return live <= context.heap_limit && counted <= context.heap_limit - live;
}

bool BudgetedText::reserve(std::size_t bytes) {
    if (bytes <= text_.capacity()) return true;
    // The old storage (if any) is freed by the reallocation but held during it.
    const std::size_t held = size_add(bytes, text_.capacity());
    if (held > kUncheckedBytes && !reserve_result(context_, held)) return false;
    text_.reserve(bytes);
    return true;
}

bool BudgetedText::grow(std::size_t extra) {
    const std::size_t needed = size_add(text_.size(), extra);
    if (needed == kSizeOverflow) return false;
    // Small buffers grow as std::string would (unchecked slack, see header).
    if (size_add(needed, text_.capacity()) <= kUncheckedBytes / 2) return true;
    // Amortised doubling, as std::string would, but checked first.
    const std::size_t doubled = size_mul(text_.capacity(), 2);
    return reserve(needed > doubled ? needed : (doubled < kMaxResultBytes ? doubled : needed));
}

std::string_view render_scalar(const Value& value, char (&buffer)[64]) {
    switch (value.kind()) {
        case Kind::Bool: return value.as_bool() ? std::string_view("true") : std::string_view("false");
        case Kind::Null: return "null";
        default: return render_number(value, buffer);
    }
}

void append_scalar(const Value& value, std::string& out) {
    switch (value.kind()) {
        case Kind::String: { const std::string_view s = string_view_of(value); out.append(s.data(), s.size()); return; }
        case Kind::Bool: out += value.as_bool() ? "true" : "false"; return;
        case Kind::Null: out += "null"; return;
        default: append_number(value, out); return;
    }
}

ArrayBuilder::ArrayBuilder(std::size_t reserve) : array_(make_array(reserve)), items_(&array_mut(array_)) {}

void ArrayBuilder::push(Value item) { items_->push_back(static_cast<Value&&>(item)); }

bool ArrayBuilder::push(const Context& context, Value item) {
    items_->push_back(static_cast<Value&&>(item));
    if (items_->size() % Context::kBudgetStride != 0) return true;
    heap_retally(array_.heap());
    return !context.over_budget();
}

Value ArrayBuilder::finish() {
    heap_retally(array_.heap());
    items_ = nullptr;
    return static_cast<Value&&>(array_);
}

bool generated_key(const Value& key, std::string_view name, std::string& rendered, std::string& error) {
    if (!(key.is(Kind::String) || key.is_number() || key.is(Kind::Bool))) {
        error.assign(name.data(), name.size());
        error += ": generated key must be string, number, or bool";
        return false;
    }
    rendered.clear();
    append_scalar(key, rendered);
    return true;
}

} // namespace nift::rt::natives
