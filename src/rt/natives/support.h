#pragma once
// Shared argument conversions and checks of the CP14 built-in families, each
// a port of the legacy helper it names (RuntimeValue.cpp, ParserExpression.cpp).
#include "rt/natives/builtin.h"
#include "rt/string.h"
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
// Appends a renderable scalar (callers check renderable_scalar first).
void append_scalar(const Value& value, std::string& out);
// Legacy number rendering (RuntimeValue::dump of Number / StrNumber), ported:
// natives never construct legacy values.
void append_number(const Value& value, std::string& out);

// ---- Pre-allocation budget check (memory investigation C3) ----
//
// Natives whose result can outgrow their inputs (join, replace; next:
// stringify, flatten) check the heap budget BEFORE every substantial
// allocation of the result, never after building it: through reserve_result,
// usually via BudgetedText, which checks before each growth of its buffer.
// When a check fails the native returns Status::Memory ("memory budget
// exceeded") and the buffer it held is freed.
//
// Allowed temporary overhead, against the configured heap budget (not RSS):
// the result buffer while it is built, plus, during one reallocation, its
// previous storage; both are included in the bytes checked, so the live
// total plus everything the native holds never exceeds the budget. The
// arguments are already counted (they are live values). A buffer below
// kUncheckedBytes / 2 (32 KiB) grows as std::string would, unchecked, so the
// slack per native call is bounded by 96 KiB (a new buffer of at most 64 KiB
// plus the old one), matching stringify's 64 KiB check stride; every larger
// growth is checked first.
inline constexpr std::size_t kUncheckedBytes = std::size_t(64) << 10;
inline constexpr std::size_t kSizeOverflow = static_cast<std::size_t>(-1);
// Sums and products that saturate at kSizeOverflow instead of wrapping.
std::size_t size_add(std::size_t a, std::size_t b) noexcept;
std::size_t size_mul(std::size_t a, std::size_t b) noexcept;
// Results at or beyond this many bytes are refused whatever the budget
// (covers kSizeOverflow and std::string's max_size()).
inline constexpr std::size_t kMaxResultBytes = static_cast<std::size_t>(1) << 62;
// True when `bytes` more (a buffer about to be allocated, plus whatever
// uncounted storage the caller still holds) fit the context's heap budget on
// top of the live total. With the budget disabled (kNoHeapLimit) only the
// kMaxResultBytes bound applies.
bool reserve_result(const Context& context, std::size_t bytes) noexcept;

// The rendering of a non-string renderable scalar (number, bool, null), as
// append_scalar would append it; numbers are rendered into `buffer`.
std::string_view render_scalar(const Value& value, char (&buffer)[64]);

// A string result built under the budget: every growth of the buffer is
// checked first (new capacity plus the storage being replaced). append*
// return false when the budget refuses; the caller returns Status::Memory.
class BudgetedText {
public:
    explicit BudgetedText(const Context& context) : context_(context) {}
    BudgetedText(const BudgetedText&) = delete;
    BudgetedText& operator=(const BudgetedText&) = delete;
    // Capacity for at least `bytes` (checked); for callers that know the size.
    bool reserve(std::size_t bytes);
    bool append(std::string_view text) {
        if (text.size() > text_.capacity() - text_.size() && !grow(text.size())) return false;
        text_.append(text.data(), text.size());
        return true;
    }
    // Two pieces under one check (a separator and an element).
    bool append(std::string_view first, std::string_view second) {
        const std::size_t n = first.size() + second.size();  // both are live strings: no overflow
        if (n > text_.capacity() - text_.size() && !grow(n)) return false;
        if (!first.empty()) text_.append(first.data(), first.size());
        text_.append(second.data(), second.size());
        return true;
    }
    std::string take() { return static_cast<std::string&&>(text_); }

private:
    bool grow(std::size_t extra);
    const Context& context_;
    std::string text_;
};

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
