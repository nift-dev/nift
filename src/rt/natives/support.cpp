#include "rt/natives/support.h"

#include "rt/aggregate.h"
#include "rt/compare.h"
#include "rt/decimal.h"
#include "rt/heap_account.h"
#include "rt/number.h"
#include "rt/render.h"
#include "rt/string.h"

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

void append_scalar(const Value& value, std::string& out) { append_rendered(value, out); }

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
    append_rendered(key, rendered);
    return true;
}

} // namespace nift::rt::natives
