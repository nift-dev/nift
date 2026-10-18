#pragma once
// Shared argument conversions and checks of the CP14 built-in families, each
// a port of the legacy helper it names (RuntimeValue.cpp, ParserExpression.cpp).
#include "rt/natives/builtin.h"
#include "rt/value.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nift::rt::natives {

// Sets `error` and returns Status::Error.
Status fail(std::string& error, std::string message);
// "<name>: expected no arguments" unless argc == 1 (the receiver only).
bool no_arguments(std::uint32_t argc, std::string_view name, std::string& error);

// Legacy runtime_number_to_size: a non-negative integral number that fits
// size_t (a double must also be below 2^64 as a double, a Decimal at most
// SIZE_MAX exactly). -0.0 is 0.
bool number_to_size(const Value& value, std::size_t& out);

// Legacy runtime_compare_numbers: exact order across Int, Double and Decimal;
// a NaN double sorts after every number and equals another NaN.
int compare_numbers(const Value& left, const Value& right);

// The UTF-8 sequence length legacy accepts at `text[at]` (1-4), or 0 when the
// bytes there are not a sequence legacy accepts (lead bytes C2-F4, every
// continuation byte 10xxxxxx; overlong E0/F0 forms and surrogates pass, as in
// legacy).
std::size_t utf8_sequence(std::string_view text, std::size_t at);
// Number of code points, or false when utf8_sequence rejects a position.
bool utf8_count(std::string_view text, std::size_t& count);

// Legacy render_expression_value for the kinds a join or key may render:
// strings as-is, numbers as legacy dump, true/false/null. False for every
// other kind.
bool renderable_scalar(const Value& value);
void append_scalar(const Value& value, std::string& out);

// Legacy generated_object_key (CP171): strings as-is, numbers and booleans
// rendered; any other kind is "<name>: generated key must be string, number,
// or bool".
bool generated_key(const Value& key, std::string_view name, std::string& rendered, std::string& error);

// Builds a fresh array: one array_mut for the whole construction (no
// per-element unshare or sanitizer relocation), the live-heap count retallied
// every Context::kBudgetStride elements (so the budget check sees the growing
// result) and at finish(). The array is private to the builder until finish(),
// so holding its storage across the pushes is within the CP09 contract.
class ArrayBuilder {
public:
    explicit ArrayBuilder(std::size_t reserve = 0);
    ArrayBuilder(const ArrayBuilder&) = delete;
    ArrayBuilder& operator=(const ArrayBuilder&) = delete;
    // Returns false when the context's heap budget is exceeded.
    bool push(const Context& context, Value item);
    void push(Value item);
    Value finish();

private:
    Value array_;
    std::vector<Value>* items_;
};

// Value of a count or index (legacy numbers; Int here, rendered identically).
inline Value count_value(std::size_t n) { return Value::integer(static_cast<std::int64_t>(n)); }

} // namespace nift::rt::natives
