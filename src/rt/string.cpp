#include "rt/string.h"

#include "rt/heap.h"
#include "rt/heap_account.h"

namespace nift::rt {

namespace {

Value counted(HeapObject* object) {
    heap_retally(object);
    return Value::adopt(object);
}

} // namespace

Value make_string(std::string_view text) {
    if (text.size() <= Value::kInlineStringCapacity) return Value::inline_text(text);
    return counted(new StringObject(std::string(text)));
}

Value make_string(std::string&& text) {
    if (text.size() <= Value::kInlineStringCapacity) return make_string(std::string_view(text));
    return counted(new StringObject(std::move(text)));
}

std::string_view string_view_of(const Value& value) {
    if (value.inline_string()) return value.inline_view();
    return value.heap_as<StringObject>()->text;
}

void append_in_place(Value& text, const Value& tail) {
    std::string& storage = text.heap_as<StringObject>()->text;
    if (tail.is_heap() && tail.heap() == text.heap()) {
        storage += std::string(storage);
    } else {
#if defined(__SANITIZE_ADDRESS__) || defined(NIFT_RT_CHECKED_REFS)
        std::string fresh;
        fresh.reserve(storage.capacity());
        fresh = storage;
        storage.swap(fresh);
#endif
        storage.append(string_view_of(tail));
    }
    heap_retally(text.heap());
}

Value make_bytes(std::vector<std::uint8_t> data) { return counted(new BytesObject(std::move(data))); }

const std::vector<std::uint8_t>& bytes_of(const Value& value) { return value.heap_as<BytesObject>()->data; }

Value make_decimal(std::string spelling, double approx) {
    return counted(new DecimalObject(std::move(spelling), approx));
}

std::string_view decimal_spelling(const Value& value) { return value.heap_as<DecimalObject>()->spelling; }

double decimal_approx(const Value& value) { return value.heap_as<DecimalObject>()->approx; }

} // namespace nift::rt
