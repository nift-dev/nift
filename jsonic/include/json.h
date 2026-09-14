#pragma once

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_set>
#include <utility>
#include <type_traits>

// A small, self-contained JSON implementation for Nift.
// This header can be copied into another C++17 project and used independently.
namespace json {

enum class Type { Null, Boolean, Number, StrNumber, String, Array, Object };

enum class DuplicateKeyPolicy { Preserve, Reject };

struct ParseOptions {
    bool allow_comments = false;
    bool allow_trailing_commas = false;
    DuplicateKeyPolicy duplicate_keys = DuplicateKeyPolicy::Preserve;
    std::size_t max_depth = 512;
};

struct ParseDiagnostic {
    std::string message;
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
};

class Document {
public:
    Type type = Type::Null;
    double num = 0.0;
    bool boolean = false;
    std::string string;
    std::vector<Document> array;
    std::vector<std::pair<std::string, Document>> object;

    Document() = default;
    Document(std::nullptr_t) : type(Type::Null) {}
    Document(bool value) : type(Type::Boolean), boolean(value) {}
    Document(int value) : type(Type::Number), num(value) {}
    Document(double value) : type(Type::Number), num(value) {}
    Document(const char* value) : type(Type::String), string(value ? value : "") {}
    Document(const std::string& value) : type(Type::String), string(value) {}

    static Document make_array() { Document d; d.type = Type::Array; return d; }
    static Document make_object() { Document d; d.type = Type::Object; return d; }

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Boolean; }
    bool is_number() const { return type == Type::Number || type == Type::StrNumber; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    bool has(const std::string& key) const {
        if (!is_object()) return false;
        return std::find_if(object.begin(), object.end(), [&](const auto& entry) { return entry.first == key; }) != object.end();
    }

    Document& operator[](const std::string& key) {
        if (is_null()) type = Type::Object;
        if (!is_object()) throw std::runtime_error("JSON value is not an object");
        for (auto& entry : object) if (entry.first == key) return entry.second;
        object.emplace_back(key, Document{});
        return object.back().second;
    }

    const Document& operator[](const std::string& key) const {
        if (!is_object()) throw std::runtime_error("JSON value is not an object");
        for (const auto& entry : object) if (entry.first == key) return entry.second;
        throw std::out_of_range("JSON object has no key '" + key + "'");
    }

    Document& operator[](std::size_t index) {
        if (!is_array()) throw std::runtime_error("JSON value is not an array");
        return array.at(index);
    }

    const Document& operator[](std::size_t index) const {
        if (!is_array()) throw std::runtime_error("JSON value is not an array");
        return array.at(index);
    }

    std::string as_string(const std::string& fallback = "") const {
        return is_string() ? string : fallback;
    }

    int as_int(int fallback = 0) const {
        return is_number() ? static_cast<int>(num) : fallback;
    }

    void push_back(const Document& value) {
        if (is_null()) type = Type::Array;
        if (!is_array()) throw std::runtime_error("JSON value is not an array");
        array.push_back(value);
    }

    static bool parse(const std::string& text, Document& result, std::string& error) {
        return parse(std::string_view(text), result, error);
    }

    static bool parse(const char* text, Document& result, std::string& error) {
        return parse(std::string_view(text), result, error);
    }

    static bool parse(std::string_view text, Document& result, std::string& error) {
        return parse(text, result, error, ParseOptions{});
    }

    static bool parse(const std::string& text, Document& result, std::string& error,
                      const ParseOptions& options) {
        return parse(std::string_view(text), result, error, options);
    }

    static bool parse(const char* text, Document& result, std::string& error,
                      const ParseOptions& options) {
        return parse(std::string_view(text), result, error, options);
    }

    static bool parse(std::string_view text, Document& result, std::string& error,
                      const ParseOptions& options) {
        const auto run = [&](auto comments) {
            Parser<decltype(comments)::value> parser(text, options);
            try {
                result = parser.parse_value();
                parser.skip_whitespace();
                if (!parser.finished()) parser.fail("unexpected characters after JSON value");
                return true;
            } catch (const ParseError& e) {
                const auto [line, column] = line_column(text, e.position);
                error = std::string(e.what()) + " at line " + std::to_string(line) +
                        ", column " + std::to_string(column);
                return false;
            }
        };
        return options.allow_comments ? run(std::true_type{}) : run(std::false_type{});
    }

    static bool parse(const std::string& text, Document& result, ParseDiagnostic& diagnostic,
                      const ParseOptions& options = {}) {
        return parse(std::string_view(text), result, diagnostic, options);
    }

    static bool parse(const char* text, Document& result, ParseDiagnostic& diagnostic,
                      const ParseOptions& options = {}) {
        return parse(std::string_view(text), result, diagnostic, options);
    }

    static bool parse(std::string_view text, Document& result, ParseDiagnostic& diagnostic,
                      const ParseOptions& options = {}) {
        const auto run = [&](auto comments) {
            Parser<decltype(comments)::value> parser(text, options);
            try {
                result = parser.parse_value();
                parser.skip_whitespace();
                if (!parser.finished()) parser.fail("unexpected characters after JSON value");
                diagnostic = {};
                return true;
            } catch (const ParseError& e) {
                const auto [line, column] = line_column(text, e.position);
                diagnostic.message = e.what();
                diagnostic.offset = e.position;
                diagnostic.line = line;
                diagnostic.column = column;
                return false;
            }
        };
        return options.allow_comments ? run(std::true_type{}) : run(std::false_type{});
    }

    // Parses a named array directly from a root JSON object and invokes the
    // callback for each item without constructing a DOM for the whole array.
    // This is useful for very large metadata arrays while preserving normal
    // Document parsing everywhere else. The callback receives an rvalue item
    // and should return true to continue or false to stop with an error.
    template <typename Callback>
    static bool for_each_array_item(const std::string& text, const std::string& member,
                                    Callback&& callback, std::string& error) {
        return for_each_array_item(text, member, std::forward<Callback>(callback), error,
                                   ParseOptions{});
    }

    template <typename Callback>
    static bool for_each_array_item(const std::string& text, const std::string& member,
                                    Callback&& callback, std::string& error,
                                    const ParseOptions& options) {
        Parser<true> parser(text, options);
        try {
            parser.skip_whitespace();
            parser.expect('{', "expected root JSON object");
            parser.skip_whitespace();
            bool found = false;
            std::unordered_set<std::string> seen_keys;
            if (!parser.consume('}')) {
                while (true) {
                    parser.skip_whitespace();
                    if (parser.finished() || parser.peek() != '"') parser.fail("expected quoted object key");
                    std::string key = parser.parse_string();
                    if (!seen_keys.insert(key).second) parser.fail("duplicate object key '" + key + "'");
                    parser.skip_whitespace();
                    parser.expect(':', "expected ':' after object key");
                    parser.skip_whitespace();

                    if (key == member) {
                        if (found) parser.fail("duplicate array member '" + member + "'");
                        found = true;
                        parser.expect('[', "expected array for member '" + member + "'");
                        parser.skip_whitespace();
                        if (!parser.consume(']')) {
                            while (true) {
                                Document item = parser.parse_value();
                                if (!callback(std::move(item))) {
                                    error = "array item rejected";
                                    return false;
                                }
                                parser.skip_whitespace();
                                if (parser.consume(']')) break;
                                parser.expect(',', "expected ',' between array members");
                                parser.skip_whitespace();
                                if (parser.consume(']')) {
                                    if (!options.allow_trailing_commas)
                                        parser.fail("trailing comma in array");
                                    break;
                                }
                            }
                        }
                    } else {
                        (void)parser.parse_value();
                    }

                    parser.skip_whitespace();
                    if (parser.consume('}')) break;
                    parser.expect(',', "expected ',' between object members");
                    parser.skip_whitespace();
                    if (parser.consume('}')) {
                        if (!options.allow_trailing_commas)
                            parser.fail("trailing comma in object");
                        break;
                    }
                }
            }
            parser.skip_whitespace();
            if (!parser.finished()) parser.fail("unexpected characters after JSON value");
            if (!found) {
                error = "root object has no array member '" + member + "'";
                return false;
            }
            return true;
        } catch (const ParseError& e) {
            const auto [line, column] = line_column(text, e.position);
            error = std::string(e.what()) + " at line " + std::to_string(line) +
                    ", column " + std::to_string(column);
            return false;
        }
    }

    std::string dump(int indent = 2) const {
        std::string out;
        out.reserve(256);
        write(out, *this, indent, 0);
        return out;
    }

    // Appends a JSON string payload without surrounding quotes. Useful for
    // efficient streaming writers that still want the library's escaping rules.
    static void append_escaped_string(std::string& out, const std::string& value) {
        append_escaped(out, value);
    }

private:
    struct ParseError : std::runtime_error {
        std::size_t position;
        ParseError(const std::string& message, std::size_t p)
            : std::runtime_error(message), position(p) {}
    };

    static std::pair<std::size_t, std::size_t> line_column(std::string_view source, std::size_t position) {
        std::size_t line = 1;
        std::size_t column = 1;
        const std::size_t end = std::min(position, source.size());
        for (std::size_t i = 0; i < end; ++i) {
            if (source[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        return {line, column};
    }

    template <bool AllowComments>
    class Parser {
        friend class Document;
    public:
        Parser(std::string_view source, const ParseOptions& options)
            : source_(source), options_(options) {}

        Document parse_value(std::size_t depth = 0) {
            if (depth > options_.max_depth) fail("maximum JSON nesting depth exceeded");
            skip_whitespace();
            if (finished()) fail("expected JSON value");
            switch (peek()) {
                case '{': return parse_object(depth);
                case '[': return parse_array(depth);
                case '"': return Document(parse_string());
                case 't': consume_literal("true"); return Document(true);
                case 'f': consume_literal("false"); return Document(false);
                case 'n': consume_literal("null"); return Document(nullptr);
                default:
                    if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek())))
                        return parse_number();
                    fail("unexpected character while parsing JSON");
            }
            return {};
        }

        void skip_whitespace() {
            while (!finished()) {
                const char c = peek();
                if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                    advance();
                    continue;
                }
                if constexpr (!AllowComments) {
                    break;
                }
                if (!options_.allow_comments || c != '/' || position_ + 1 >= source_.size()) break;
                const char next = source_[position_ + 1];
                if (next == '/') {
                    position_ += 2;
                    while (!finished() && peek() != '\n' && peek() != '\r') advance();
                    continue;
                }
                if (next == '*') {
                    position_ += 2;
                    while (position_ + 1 < source_.size() &&
                           !(source_[position_] == '*' && source_[position_ + 1] == '/'))
                        advance();
                    if (position_ + 1 >= source_.size()) fail("unterminated block comment");
                    position_ += 2;
                    continue;
                }
                break;
            }
        }

        bool finished() const { return position_ >= source_.size(); }

        [[noreturn]] void fail(const std::string& message) const {
            throw ParseError(message, position_);
        }

    private:
        std::string_view source_;
        ParseOptions options_;
        std::size_t position_ = 0;

        char peek() const { return source_[position_]; }

        char advance() {
            return source_[position_++];
        }

        bool consume(char expected) {
            if (!finished() && peek() == expected) { advance(); return true; }
            return false;
        }

        void expect(char expected, const std::string& message) {
            if (!consume(expected)) fail(message);
        }

        void consume_literal(const char* literal) {
            while (*literal) {
                if (finished() || advance() != *literal++) fail("invalid JSON literal");
            }
        }

        Document parse_object(std::size_t depth) {
            expect('{', "expected '{'");
            Document result = Document::make_object();
            result.object.reserve(4);
            skip_whitespace();
            if (consume('}')) return result;
            while (true) {
                skip_whitespace();
                if (finished() || peek() != '"') fail("expected quoted object key");
                std::string key = parse_string();
                if (options_.duplicate_keys == DuplicateKeyPolicy::Reject && result.has(key))
                    fail("duplicate object key '" + key + "'");
                skip_whitespace();
                expect(':', "expected ':' after object key");
                result.object.emplace_back(std::move(key), parse_value(depth + 1));
                skip_whitespace();
                if (consume('}')) break;
                expect(',', "expected ',' between object members");
                skip_whitespace();
                if (consume('}')) {
                    if (!options_.allow_trailing_commas) fail("trailing comma in object");
                    break;
                }
            }
            return result;
        }

        Document parse_array(std::size_t depth) {
            expect('[', "expected '['");
            Document result = Document::make_array();
            result.array.reserve(2);
            skip_whitespace();
            if (consume(']')) return result;
            while (true) {
                result.array.push_back(parse_value(depth + 1));
                skip_whitespace();
                if (consume(']')) break;
                expect(',', "expected ',' between array members");
                skip_whitespace();
                if (consume(']')) {
                    if (!options_.allow_trailing_commas) fail("trailing comma in array");
                    break;
                }
            }
            return result;
        }

        static void append_utf8(std::string& out, std::uint32_t codepoint) {
            if (codepoint <= 0x7f) out.push_back(static_cast<char>(codepoint));
            else if (codepoint <= 0x7ff) {
                out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
                out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
            } else if (codepoint <= 0xffff) {
                out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
                out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
            } else {
                out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
                out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
                out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
            }
        }

        std::uint32_t parse_hex4() {
            std::uint32_t value = 0;
            for (int i = 0; i < 4; ++i) {
                if (finished()) fail("incomplete unicode escape");
                char c = advance();
                value <<= 4;
                if (c >= '0' && c <= '9') value |= c - '0';
                else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') value |= c - 'A' + 10;
                else fail("invalid unicode escape");
            }
            return value;
        }

        std::uint32_t parse_unicode_codepoint() {
            const std::uint32_t first = parse_hex4();

            if (first >= 0xdc00 && first <= 0xdfff)
                fail("unexpected low surrogate in unicode escape");

            if (first < 0xd800 || first > 0xdbff) return first;

            if (!consume('\\') || !consume('u'))
                fail("high surrogate must be followed by a low surrogate");

            const std::uint32_t second = parse_hex4();
            if (second < 0xdc00 || second > 0xdfff)
                fail("invalid low surrogate in unicode escape");

            return 0x10000u + ((first - 0xd800u) << 10u) + (second - 0xdc00u);
        }

        std::size_t validate_utf8_sequence(std::size_t start, std::size_t end) const {
            const auto byte = [&](std::size_t offset) {
                return static_cast<unsigned char>(source_[offset]);
            };
            const auto continuation = [&](std::size_t offset) {
                return offset < end && byte(offset) >= 0x80 && byte(offset) <= 0xbf;
            };
            const unsigned char first = byte(start);
            std::size_t length = 0;
            if (first >= 0xc2 && first <= 0xdf) {
                length = 2;
            } else if (first >= 0xe0 && first <= 0xef) {
                length = 3;
            } else if (first >= 0xf0 && first <= 0xf4) {
                length = 4;
            } else {
                throw ParseError("invalid UTF-8 in JSON string", start);
            }
            if (start + length > end)
                throw ParseError("incomplete UTF-8 sequence in JSON string", start);
            for (std::size_t i = 1; i < length; ++i) {
                if (!continuation(start + i))
                    throw ParseError("invalid UTF-8 continuation byte in JSON string", start + i);
            }
            const unsigned char second = byte(start + 1);
            if ((first == 0xe0 && second < 0xa0) ||
                (first == 0xed && second > 0x9f) ||
                (first == 0xf0 && second < 0x90) ||
                (first == 0xf4 && second > 0x8f))
                throw ParseError("invalid UTF-8 scalar value in JSON string", start);
            return start + length;
        }

        void validate_utf8_range(std::size_t start, std::size_t end) const {
            while (start < end) {
                const unsigned char c = static_cast<unsigned char>(source_[start]);
                if (c < 0x80) {
                    ++start;
                } else {
                    start = validate_utf8_sequence(start, end);
                }
            }
        }

        std::string parse_string() {
            expect('"', "expected string");
            const std::size_t raw_start = position_;
            std::size_t scan = position_;
            while (scan < source_.size()) {
                const unsigned char c = static_cast<unsigned char>(source_[scan]);
                if (c == '"') {
                    const std::size_t length = scan - raw_start;
                    validate_utf8_range(raw_start, scan);
                    position_ = scan + 1;
                    return std::string(source_.substr(raw_start, length));
                }
                if (c == '\\' || c < 0x20) break;
                ++scan;
            }

            std::string result;
            result.reserve(scan - raw_start + 8);
            while (!finished()) {
                char c = advance();
                if (c == '"') return result;
                if (static_cast<unsigned char>(c) < 0x20) fail("control character in JSON string");
                if (static_cast<unsigned char>(c) >= 0x80) {
                    const std::size_t start = position_ - 1;
                    const std::size_t finish = validate_utf8_sequence(start, source_.size());
                    result.append(source_.data() + start, finish - start);
                    position_ = finish;
                    continue;
                }
                if (c != '\\') { result.push_back(c); continue; }
                if (finished()) fail("incomplete escape sequence");
                switch (advance()) {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    case 'u': append_utf8(result, parse_unicode_codepoint()); break;
                    default: fail("invalid JSON escape sequence");
                }
            }
            fail("unterminated JSON string");
            return {};
        }

        Document parse_number() {
            std::size_t start = position_;
            const bool negative = consume('-');
            if (finished()) fail("invalid JSON number");
            if (consume('0')) {
                if (!finished() && std::isdigit(static_cast<unsigned char>(peek())))
                    fail("leading zero in JSON number");
            } else {
                if (!std::isdigit(static_cast<unsigned char>(peek()))) fail("invalid JSON number");
                while (!finished() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
            }
            const std::size_t integer_digits = position_ - start - (negative ? 1u : 0u);
            const bool has_fraction = consume('.');
            if (has_fraction) {
                if (finished() || !std::isdigit(static_cast<unsigned char>(peek()))) fail("invalid fraction");
                while (!finished() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
            }
            bool has_exponent = false;
            unsigned exponent_magnitude = 0;
            if (!finished() && (peek() == 'e' || peek() == 'E')) {
                has_exponent = true;
                advance();
                if (!finished() && (peek() == '+' || peek() == '-')) advance();
                if (finished() || !std::isdigit(static_cast<unsigned char>(peek()))) fail("invalid exponent");
                while (!finished() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    if (exponent_magnitude < 300)
                        exponent_magnitude = exponent_magnitude * 10u + static_cast<unsigned>(peek() - '0');
                    advance();
                }
            }
            const char* number_start = source_.data() + start;
            const char* number_end = source_.data() + position_;
            double value = 0.0;
            const std::from_chars_result converted =
                std::from_chars(number_start, number_end, value, std::chars_format::general);
            if (converted.ec == std::errc::result_out_of_range) {
                const std::string token(source_.substr(start, position_ - start));
                value = std::strtod(token.c_str(), nullptr);
            } else if (converted.ec != std::errc() || converted.ptr != number_end) {
                fail("invalid JSON number");
            }
            if (!std::isfinite(value)) fail("JSON number is outside the supported finite range");

            const std::string_view token(number_start, static_cast<std::size_t>(number_end - number_start));
            bool may_need_exact_spelling =
                (!has_exponent && !has_fraction && integer_digits > 15) ||
                (value == 0.0 && (has_fraction || negative)) ||
                exponent_magnitude >= 300;

            Document result(value);
            char canonical[64];
            const char* canonical_end = may_need_exact_spelling
                ? format_number(canonical, canonical + sizeof(canonical), value)
                : nullptr;
            if (may_need_exact_spelling &&
                (canonical_end == nullptr || token != std::string_view(canonical, canonical_end - canonical))) {
                result.type = Type::StrNumber;
                result.string.assign(token.data(), token.size());
            }
            return result;
        }
    };

    static char* format_number(char* begin, char* end, double value) {
        std::to_chars_result converted;
        if (std::floor(value) == value &&
            value >= -9223372036854775808.0 && value < 9223372036854775808.0) {
            converted = std::to_chars(begin, end, static_cast<long long>(value));
        } else {
            converted = std::to_chars(begin, end, value, std::chars_format::general);
        }
        return converted.ec == std::errc() ? converted.ptr : nullptr;
    }

    static void append_escaped(std::string& out, const std::string& value) {
        static constexpr char hex[] = "0123456789abcdef";
        for (unsigned char c : value) {
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
                        out.push_back(hex[(c >> 4) & 0x0f]);
                        out.push_back(hex[c & 0x0f]);
                    } else {
                        out.push_back(static_cast<char>(c));
                    }
            }
        }
    }

    static void append_indent(std::string& out, int indent, int level) {
        out.append(static_cast<std::size_t>(indent * level), ' ');
    }

    static void write(std::string& out, const Document& value, int indent, int level) {
        switch (value.type) {
            case Type::Null:
                out += "null";
                break;
            case Type::Boolean:
                out += value.boolean ? "true" : "false";
                break;
            case Type::Number: {
                char buffer[64];
                char* converted = format_number(buffer, buffer + sizeof(buffer), value.num);
                if (converted != nullptr) out.append(buffer, converted);
                else out += "0";
                break;
            }
            case Type::StrNumber:
                out += value.string;
                break;
            case Type::String:
                out.push_back('"');
                append_escaped(out, value.string);
                out.push_back('"');
                break;
            case Type::Array:
                out.push_back('[');
                if (!value.array.empty()) {
                    out.push_back('\n');
                    for (std::size_t i = 0; i < value.array.size(); ++i) {
                        append_indent(out, indent, level + 1);
                        write(out, value.array[i], indent, level + 1);
                        if (i + 1 != value.array.size()) out.push_back(',');
                        out.push_back('\n');
                    }
                    append_indent(out, indent, level);
                }
                out.push_back(']');
                break;
            case Type::Object:
                out.push_back('{');
                if (!value.object.empty()) {
                    out.push_back('\n');
                    for (std::size_t i = 0; i < value.object.size(); ++i) {
                        append_indent(out, indent, level + 1);
                        out.push_back('"');
                        append_escaped(out, value.object[i].first);
                        out += "\": ";
                        write(out, value.object[i].second, indent, level + 1);
                        if (i + 1 != value.object.size()) out.push_back(',');
                        out.push_back('\n');
                    }
                    append_indent(out, indent, level);
                }
                out.push_back('}');
                break;
        }
    }

};

} // namespace json
