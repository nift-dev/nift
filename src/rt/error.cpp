#include "rt/error.h"

// rt-lint: cold-file error construction and member lookup by name run on
// error paths only (not a property-access hot path).

#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/host/host_error.h"
#include "rt/natives/builtin.h"
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
    heap_retally(object);
    out = Value::adopt(object);
    return true;
}

Value make_user_error(std::string message) {
    Value out;
    std::string ignored;
    make_error(std::move(message), "user.raised", Value(), {}, 0, 0, out, ignored);
    return out;
}

bool make_system_error(std::string code, std::string message, const SourceOrigin& origin, Value& out,
                       std::string& error) {
    if (!host::catchable(code) && !natives::is_recoverable_code(code)) {
        error = "code is not catchable";
        return false;
    }
    std::unique_ptr<ErrorObject> object(new ErrorObject());
    object->category = code.substr(0, code.find('.'));
    object->message = std::move(message);
    object->code = std::move(code);
    object->source = origin.path;
    object->line = origin.line;
    object->column = origin.column;
    if (object->source.empty() || object->line == 0 || object->column == 0) {
        object->source.clear();
        object->line = 0;
        object->column = 0;
    }
    heap_retally(object.get());
    out = Value::adopt(object.release());
    return true;
}

Value clone_error_with_origin(const Value& error, const SourceOrigin& origin) {
    if (!error.is(Kind::Error)) return error;
    const auto* source = error.heap_as<ErrorObject>();
    std::unique_ptr<ErrorObject> copy(new ErrorObject());
    copy->message = source->message;
    copy->code = source->code;
    copy->category = source->category;
    copy->source = origin.path;
    copy->line = origin.line;
    copy->column = origin.column;
    copy->cause = source->cause;
    heap_retally(copy.get());
    return Value::adopt(copy.release());
}

namespace {

void append_json_string(std::string_view text, std::string& out) {
    static constexpr char hex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0f];
                    out += hex[c & 0x0f];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void append_number(std::string& out, std::size_t value) { out += std::to_string(value); }

void append_error_json(const ErrorObject& error, bool pretty, int depth, std::string& out) {
    const auto indent = [&](int level) { if (pretty) out.append(static_cast<std::size_t>(level * 2), ' '); };
    out += '{';
    if (pretty) out += '\n';
    const auto field = [&](const char* name, int level) {
        indent(level);
        out += '"';
        out += name;
        out += pretty ? "\": " : "\":";
    };
    field("message", depth + 1);
    append_json_string(error.message, out);
    out += pretty ? ",\n" : ",";
    field("code", depth + 1);
    append_json_string(error.code, out);
    out += pretty ? ",\n" : ",";
    field("category", depth + 1);
    append_json_string(error.category, out);
    out += pretty ? ",\n" : ",";
    field("source", depth + 1);
    append_json_string(error.source, out);
    out += pretty ? ",\n" : ",";
    field("line", depth + 1);
    append_number(out, error.line);
    out += pretty ? ",\n" : ",";
    field("column", depth + 1);
    append_number(out, error.column);
    out += pretty ? ",\n" : ",";
    field("cause", depth + 1);
    if (error.cause.is(Kind::Error)) {
        append_error_json(*error.cause.heap_as<ErrorObject>(), pretty, depth + 1, out);
    } else {
        out += "null";
    }
    if (pretty) out += '\n';
    indent(depth);
    out += '}';
}

} // namespace

std::string error_to_json(const Value& error, bool pretty) {
    std::string out;
    if (error.is(Kind::Error)) append_error_json(*error.heap_as<ErrorObject>(), pretty, 0, out);
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
