#include "rt/error.h"

// rt-lint: cold-file error construction and member lookup by name run on
// error paths only (not a property-access hot path).

#include "rt/heap.h"
#include "rt/string.h"

namespace nift::rt {

bool valid_user_error_code(std::string_view code) {
    if (code.size() > 128) return false;
    std::size_t segments = 0;
    for (std::size_t start = 0; start <= code.size();) {
        const std::size_t end = code.find('.', start);
        const std::string_view segment = code.substr(start, end == std::string_view::npos ? code.size() - start
                                                                                           : end - start);
        if (segment.empty() || segment.size() > 32 || segment.front() < 'a' || segment.front() > 'z') return false;
        for (const unsigned char c : segment)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
        ++segments;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return segments >= 2 && segments <= 8 && code.substr(0, code.find('.')) == "user";
}

bool make_error(std::string message, std::string code, const Value& cause, std::string source,
                std::size_t line, std::size_t column, Value& out, std::string& error) {
    if (!valid_user_error_code(code)) { error = "invalid user Error code"; return false; }
    if (!cause.is_null() && !cause.is(Kind::Error)) { error = "Error cause must be Error or null"; return false; }
    std::size_t depth = 1;
    for (Value current = cause; current.is(Kind::Error); current = current.heap_as<ErrorObject>()->cause)
        if (++depth > 16) { error = "Error cause chain exceeds 16 values or contains a cycle"; return false; }
    if (source.empty() || line == 0 || column == 0) { source.clear(); line = 0; column = 0; }
    auto* object = new ErrorObject();
    object->category = code.substr(0, code.find('.'));
    object->message = std::move(message);
    object->code = std::move(code);
    object->source = std::move(source);
    object->line = line;
    object->column = column;
    object->cause = cause;
    out = Value::adopt(object);
    return true;
}

Value make_user_error(std::string message) {
    Value out;
    std::string ignored;
    make_error(std::move(message), "user.raised", Value(), {}, 0, 0, out, ignored);
    return out;
}

bool error_member(const Value& error, std::string_view member, Value& out) {
    if (!error.is(Kind::Error)) return false;
    const auto* e = error.heap_as<ErrorObject>();
    if (member == "message") out = make_string(e->message);
    else if (member == "code") out = make_string(e->code);
    else if (member == "category") out = make_string(e->category);
    else if (member == "source") out = make_string(e->source);
    else if (member == "line") out = Value::integer(static_cast<std::int64_t>(e->line));
    else if (member == "column") out = Value::integer(static_cast<std::int64_t>(e->column));
    else if (member == "cause") out = e->cause;
    else return false;
    return true;
}

} // namespace nift::rt
