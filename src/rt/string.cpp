#include "rt/string.h"

#include "rt/heap.h"

namespace nift::rt {

Value make_string(std::string_view text) {
    if (text.size() <= Value::kInlineStringCapacity) return Value::inline_text(text);
    return Value::adopt(new StringObject(std::string(text)));
}

Value make_string(std::string&& text) {
    if (text.size() <= Value::kInlineStringCapacity) return Value::inline_text(text);
    return Value::adopt(new StringObject(std::move(text)));
}

std::string_view string_view_of(const Value& value) {
    if (value.inline_string()) return value.inline_view();
    return value.heap_as<StringObject>()->text;
}

Value make_bytes(std::vector<std::uint8_t> data) { return Value::adopt(new BytesObject(std::move(data))); }

const std::vector<std::uint8_t>& bytes_of(const Value& value) { return value.heap_as<BytesObject>()->data; }

Value make_decimal(std::string spelling, double approx) {
    return Value::adopt(new DecimalObject(std::move(spelling), approx));
}

std::string_view decimal_spelling(const Value& value) { return value.heap_as<DecimalObject>()->spelling; }

double decimal_approx(const Value& value) { return value.heap_as<DecimalObject>()->approx; }

} // namespace nift::rt
