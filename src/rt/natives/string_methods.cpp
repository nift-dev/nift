#include "rt/natives/string_methods.h"

#include "rt/aggregate.h"
#include "rt/natives/support.h"
#include "rt/string.h"

#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace nift::rt::natives {

namespace {

// The receiver of every method here is a String (the compiler binds these
// natives only to receivers whose kind it proved); anything else is a
// compiler defect.
bool receiver(const Value* args, std::uint32_t argc, std::string_view& text) {
    if (argc == 0 || !args[0].is(Kind::String)) return false;
    text = string_view_of(args[0]);
    return true;
}

Status internal(std::string& error, const char* what) {
    error = what;
    return Status::Internal;
}

// One string argument (index_of, contains, starts_with, ...): legacy's
// "<m>: expected one string" / "<m>: argument must be a string".
Status one_string(const Value* args, std::uint32_t argc, std::string_view name, std::string_view& needle,
                  std::string& error) {
    if (argc != 2) return fail(error, std::string(name) + ": expected one string");
    if (!args[1].is(Kind::String)) return fail(error, std::string(name) + ": argument must be a string");
    needle = string_view_of(args[1]);
    return Status::Ok;
}

// Legacy trim (shared read-only layer): " \t\r\n" only.
bool trim_byte(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
// Legacy trim_start / trim_end: std::isspace in the C locale (adds \v, \f).
bool space_byte(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

Status trimmed(const Value* args, std::uint32_t argc, Value& out, std::string& error, std::string_view name,
               bool (*strip)(char), bool front, bool back) {
    std::string_view text;
    if (!receiver(args, argc, text)) return internal(error, "trim receiver");
    if (!no_arguments(argc, name, error)) return Status::Error;
    std::size_t a = 0, b = text.size();
    if (front) while (a < b && strip(text[a])) ++a;
    if (back) while (b > a && strip(text[b - 1])) --b;
    if (a == 0 && b == text.size()) { out = args[0]; return Status::Ok; }  // shares the receiver's storage
    out = make_string(text.substr(a, b - a));
    return Status::Ok;
}

Status cased(const Value* args, std::uint32_t argc, Value& out, std::string& error, std::string_view name,
             bool upper) {
    std::string_view text;
    if (!receiver(args, argc, text)) return internal(error, "case receiver");
    if (!no_arguments(argc, name, error)) return Status::Error;
    std::string result(text);
    for (char& c : result)
        c = static_cast<char>(upper ? std::toupper(static_cast<unsigned char>(c))
                                    : std::tolower(static_cast<unsigned char>(c)));
    out = make_string(static_cast<std::string&&>(result));
    return Status::Ok;
}

} // namespace

Status str_length(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text;
    if (!receiver(args, argc, text)) return internal(error, "length receiver");
    if (!no_arguments(argc, "length", error)) return Status::Error;
    std::size_t count = 0;
    if (!utf8_count(text, count)) return fail(error, "length: invalid UTF-8");
    out = count_value(count);
    return Status::Ok;
}

Status str_empty(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text;
    if (!receiver(args, argc, text)) return internal(error, "empty receiver");
    if (!no_arguments(argc, "empty", error)) return Status::Error;
    out = Value::boolean(text.empty());
    return Status::Ok;
}

Status str_split(const Context& context, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text;
    if (!receiver(args, argc, text)) return internal(error, "split receiver");
    if (argc != 2) return fail(error, "split: expected one delimiter");
    if (!args[1].is(Kind::String)) return fail(error, "split: delimiter must be a string");
    const std::string_view delimiter = string_view_of(args[1]);
    ArrayBuilder result;
    auto push = [&](std::string_view part) { return result.push(context, make_string(part)); };
    if (delimiter.empty()) {
        // One element per code point; the whole text is validated first, as
        // legacy collects the parts before building the array.
        std::size_t count = 0;
        if (!utf8_count(text, count)) return fail(error, "split: invalid UTF-8");
        for (std::size_t i = 0; i < text.size();) {
            const std::size_t n = utf8_sequence(text, i);
            if (!push(text.substr(i, n))) return Status::Memory;
            i += n;
        }
    } else {
        std::size_t pos = 0;
        while (true) {
            const std::size_t at = text.find(delimiter, pos);
            if (at == std::string_view::npos) { if (!push(text.substr(pos))) return Status::Memory; break; }
            if (!push(text.substr(pos, at - pos))) return Status::Memory;
            pos = at + delimiter.size();
        }
    }
    out = result.finish();
    return Status::Ok;
}

Status str_trim(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return trimmed(args, argc, out, error, "trim", trim_byte, true, true);
}

Status str_trim_start(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return trimmed(args, argc, out, error, "trim_start", space_byte, true, false);
}

Status str_trim_end(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return trimmed(args, argc, out, error, "trim_end", space_byte, false, true);
}

Status str_to_upper(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return cased(args, argc, out, error, "to_upper", true);
}

Status str_to_lower(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    return cased(args, argc, out, error, "to_lower", false);
}

Status str_index_of(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text, needle;
    if (!receiver(args, argc, text)) return internal(error, "index_of receiver");
    if (one_string(args, argc, "index_of", needle, error) != Status::Ok) return Status::Error;
    const std::size_t at = text.find(needle);  // a byte offset, as legacy
    out = at == std::string_view::npos ? Value::integer(-1) : count_value(at);
    return Status::Ok;
}

Status str_last_index_of(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text, needle;
    if (!receiver(args, argc, text)) return internal(error, "last_index_of receiver");
    if (one_string(args, argc, "last_index_of", needle, error) != Status::Ok) return Status::Error;
    const std::size_t at = text.rfind(needle);
    out = at == std::string_view::npos ? Value::integer(-1) : count_value(at);
    return Status::Ok;
}

Status str_contains(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text, needle;
    if (!receiver(args, argc, text)) return internal(error, "contains receiver");
    if (one_string(args, argc, "contains", needle, error) != Status::Ok) return Status::Error;
    out = Value::boolean(text.find(needle) != std::string_view::npos);
    return Status::Ok;
}

Status str_starts_with(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text, needle;
    if (!receiver(args, argc, text)) return internal(error, "starts_with receiver");
    if (one_string(args, argc, "starts_with", needle, error) != Status::Ok) return Status::Error;
    out = Value::boolean(text.substr(0, needle.size()) == needle);
    return Status::Ok;
}

Status str_ends_with(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text, needle;
    if (!receiver(args, argc, text)) return internal(error, "ends_with receiver");
    if (one_string(args, argc, "ends_with", needle, error) != Status::Ok) return Status::Error;
    out = Value::boolean(needle.size() <= text.size() && text.substr(text.size() - needle.size()) == needle);
    return Status::Ok;
}

Status str_replace(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text;
    if (!receiver(args, argc, text)) return internal(error, "replace receiver");
    if (argc != 3) return fail(error, "replace: expected old and replacement strings");
    if (!args[1].is(Kind::String) || !args[2].is(Kind::String))
        return fail(error, "replace: arguments must be strings");
    const std::string_view pattern = string_view_of(args[1]), replacement = string_view_of(args[2]);
    if (pattern.empty()) return fail(error, "replace: old string must not be empty");
    std::size_t pos = text.find(pattern);
    if (pos == std::string_view::npos) { out = args[0]; return Status::Ok; }
    // Legacy replace_string_linear: left to right, non-overlapping, one pass.
    std::string result;
    result.reserve(text.size());
    std::size_t begin = 0;
    while (pos != std::string_view::npos) {
        result.append(text.data() + begin, pos - begin);
        result.append(replacement.data(), replacement.size());
        begin = pos + pattern.size();
        pos = text.find(pattern, begin);
    }
    result.append(text.data() + begin, text.size() - begin);
    out = make_string(static_cast<std::string&&>(result));
    return Status::Ok;
}

Status str_substr(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text;
    if (!receiver(args, argc, text)) return internal(error, "substr receiver");
    if (argc < 2 || argc > 3) return fail(error, "substr: expected pos and optional length");
    std::size_t at = 0, length = std::string_view::npos;
    if (!number_to_size(args[1], at)) return fail(error, "substr: invalid pos");
    if (at > text.size()) at = text.size();
    if (argc == 3 && !number_to_size(args[2], length)) return fail(error, "substr: invalid length");
    // Byte offsets, as legacy (may split a UTF-8 sequence; design record Q2).
    const std::string_view part = text.substr(at, length);
    if (part.size() == text.size()) { out = args[0]; return Status::Ok; }
    out = make_string(part);
    return Status::Ok;
}

Status str_to_string(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text;
    if (!receiver(args, argc, text)) return internal(error, "to_string receiver");
    if (!no_arguments(argc, "to_string", error)) return Status::Error;
    out = args[0];
    return Status::Ok;
}

Status str_to_int(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text;
    if (!receiver(args, argc, text)) return internal(error, "to_int receiver");
    if (!no_arguments(argc, "to_int", error)) return Status::Error;
    if (text.empty()) return fail(error, "to_int: invalid integer");
    if (text.front() == '+') text.remove_prefix(1);  // then from_chars, which still accepts '-' ("+-5" is -5)
    if (text.empty()) return fail(error, "to_int: invalid integer");
    std::int64_t v = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), v);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size())
        return fail(error, "to_int: invalid or out-of-range integer");
    out = Value::integer(v);
    return Status::Ok;
}

Status str_to_double(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error) {
    std::string_view text;
    if (!receiver(args, argc, text)) return internal(error, "to_double receiver");
    if (!no_arguments(argc, "to_double", error)) return Status::Error;
    if (text.empty() || space_byte(text.front()) || space_byte(text.back()))
        return fail(error, "to_double: invalid number");
    if (text.find("0x") != std::string_view::npos || text.find("0X") != std::string_view::npos)
        return fail(error, "to_double: invalid number");
    // strtod needs a terminator; legacy stops at an embedded NUL byte, which
    // then fails the full-consumption check, as here.
    const std::string copy(text);
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(copy.c_str(), &end);
    if (errno == ERANGE || end != copy.c_str() + copy.size() || !std::isfinite(v))
        return fail(error, "to_double: invalid or out-of-range number");
    out = Value::number(v);
    return Status::Ok;
}

} // namespace nift::rt::natives
