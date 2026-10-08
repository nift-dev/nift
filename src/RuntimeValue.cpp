#include "RuntimeValue.h"

#include "RuntimeJson.h"

#include <algorithm>
#ifdef NIFT_TEST_JSON_PREFLIGHT_STATS
#include <atomic>
#include <cstdio>
#include <cstdlib>
#endif
#include <charconv>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace nift {

namespace {
#ifdef NIFT_TEST_JSON_PREFLIGHT_STATS
struct JsonPreflightStats {
    std::atomic<unsigned long long> preflights{0}, conversions{0};
    ~JsonPreflightStats() {
        if (std::getenv("NIFT_TEST_JSON_PREFLIGHT_STATS"))
            std::fprintf(stderr, "json-conversion preflights=%llu conversions=%llu\n",
                         preflights.load(), conversions.load());
    }
} json_preflight_stats;
#endif
json::Document runtime_error_to_json(const RuntimeErrorData& error, std::size_t depth,
                                     std::unordered_set<const RuntimeErrorData*>& seen) {
    if (depth >= 16 || !seen.insert(&error).second)
        throw std::runtime_error("invalid Error cause chain");
    json::Document value = json::Document::make_object();
    value["message"] = json::Document(error.message);
    value["code"] = json::Document(error.code);
    value["category"] = json::Document(error.category);
    value["source"] = json::Document(error.source);
    value["line"] = json::Document(static_cast<double>(error.line));
    value["column"] = json::Document(static_cast<double>(error.column));
    value["cause"] = error.cause ? runtime_error_to_json(*error.cause, depth + 1, seen)
                                  : json::Document(nullptr);
    seen.erase(&error);
    return value;
}
}

bool runtime_valid_utf8(std::string_view value) {
    for (std::size_t i = 0; i < value.size();) {
        const auto first = static_cast<unsigned char>(value[i]);
        std::size_t width = 0;
        std::uint32_t codepoint = 0;
        if (first <= 0x7f) { width = 1; codepoint = first; }
        else if (first >= 0xc2 && first <= 0xdf) { width = 2; codepoint = first & 0x1f; }
        else if (first >= 0xe0 && first <= 0xef) { width = 3; codepoint = first & 0x0f; }
        else if (first >= 0xf0 && first <= 0xf4) { width = 4; codepoint = first & 0x07; }
        else return false;
        if (i + width > value.size()) return false;
        for (std::size_t j = 1; j < width; ++j) {
            const auto continuation = static_cast<unsigned char>(value[i + j]);
            if ((continuation & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (continuation & 0x3f);
        }
        if ((width == 2 && codepoint < 0x80) ||
            (width == 3 && codepoint < 0x800) ||
            (width == 4 && codepoint < 0x10000) ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff)
            return false;
        i += width;
    }
    return true;
}

bool runtime_valid_user_error_code(std::string_view code) {
    if (code.size() > 128) return false;
    std::size_t segments = 0;
    for (std::size_t start = 0; start <= code.size();) {
        const std::size_t end = code.find('.', start);
        const std::string_view segment = code.substr(start, end == std::string_view::npos
                                                            ? code.size() - start : end - start);
        if (segment.empty() || segment.size() > 32 || segment.front() < 'a' || segment.front() > 'z')
            return false;
        for (const unsigned char c : segment)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
                return false;
        ++segments;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return segments >= 2 && segments <= 8 && code.substr(0, code.find('.')) == "user";
}

RuntimeValue RuntimeValue::make_error(std::string message, std::string code,
                                      const RuntimeValue& cause, std::string source,
                                      std::size_t line, std::size_t column) {
    if (!runtime_valid_user_error_code(code)) throw std::invalid_argument("invalid user Error code");
    if (!cause.is_null() && !cause.is_error())
        throw std::invalid_argument("Error cause must be Error or null");
    std::size_t depth = 1;
    std::unordered_set<const RuntimeErrorData*> seen;
    for (auto current = cause.error; current; current = current->cause) {
        if (!seen.insert(current.get()).second || ++depth > 16)
            throw std::invalid_argument("Error cause chain exceeds 16 values or contains a cycle");
    }
    if (source.empty() || line == 0 || column == 0) { source.clear(); line = 0; column = 0; }
    const std::size_t dot = code.find('.');
    std::string category = code.substr(0, dot);
    return make_error(std::make_shared<const RuntimeErrorData>(
        std::move(message), std::move(code), std::move(category), std::move(source), line, column,
        cause.is_error() ? cause.error : nullptr));
}

RuntimeValue RuntimeValue::make_error(std::shared_ptr<const RuntimeErrorData> data) {
    if (!data) throw std::invalid_argument("Error backing must not be null");
    std::unordered_set<const RuntimeErrorData*> seen;
    std::size_t depth = 0;
    for (auto current = data; current; current = current->cause) {
        if (++depth > 16 || !seen.insert(current.get()).second)
            throw std::invalid_argument("Error cause chain exceeds 16 values or contains a cycle");
        const std::size_t dot = current->code.find('.');
        if (dot == std::string::npos || current->category != current->code.substr(0, dot))
            throw std::invalid_argument("Error code and category do not match");
        const bool complete_origin = !current->source.empty() && current->line != 0 && current->column != 0;
        const bool empty_origin = current->source.empty() && current->line == 0 && current->column == 0;
        if (!complete_origin && !empty_origin)
            throw std::invalid_argument("Error origin must be wholly present or absent");
    }
    RuntimeValue value; value.type = RuntimeType::Error; value.error = std::move(data); return value;
}

RuntimeValue RuntimeValue::make_error(std::string message, std::string code) {
    return make_error(std::move(message), std::move(code), RuntimeValue(nullptr));
}

bool runtime_error_member(const RuntimeValue& value, std::string_view member, RuntimeValue& result) {
    if (!value.is_error() || !value.error) return false;
    if (member == "message") result = RuntimeValue(value.error->message);
    else if (member == "code") result = RuntimeValue(value.error->code);
    else if (member == "category") result = RuntimeValue(value.error->category);
    else if (member == "source") result = RuntimeValue(value.error->source);
    else if (member == "line") result = runtime_unsigned_integer(value.error->line);
    else if (member == "column") result = runtime_unsigned_integer(value.error->column);
    else if (member == "cause") result = value.error->cause ? RuntimeValue::make_error(value.error->cause)
                                                             : RuntimeValue(nullptr);
    else return false;
    return true;
}

RuntimeValue runtime_error_with_origin(const RuntimeValue& value, std::string source,
                                       std::size_t line, std::size_t column) {
    if (!value.is_error() || !value.error) throw std::invalid_argument("expected Error value");
    if (!value.error->source.empty() && value.error->line != 0 && value.error->column != 0)
        return value;
    if (source.empty() || line == 0 || column == 0) return value;
    return RuntimeValue::make_error(std::make_shared<const RuntimeErrorData>(
        value.error->message, value.error->code, value.error->category, std::move(source),
        line, column, value.error->cause));
}

bool RuntimeValue::has(const std::string& key) const {
    if (!is_object()) return false;
    return std::find_if(object.begin(), object.end(), [&](const auto& entry) {
        return entry.first == key;
    }) != object.end();
}

RuntimeValue& RuntimeValue::operator[](const std::string& key) {
    if (is_null()) type = RuntimeType::Object;
    if (!is_object()) throw std::runtime_error("JSON value is not an object");
    for (auto& entry : object) if (entry.first == key) return entry.second;
    object.emplace_back(key, RuntimeValue{});
    return object.back().second;
}

const RuntimeValue& RuntimeValue::operator[](const std::string& key) const {
    if (!is_object()) throw std::runtime_error("JSON value is not an object");
    for (const auto& entry : object) if (entry.first == key) return entry.second;
    throw std::out_of_range("JSON object has no key '" + key + "'");
}

RuntimeValue& RuntimeValue::operator[](std::size_t index) {
    if (!is_array()) throw std::runtime_error("JSON value is not an array");
    return array.at(index);
}

const RuntimeValue& RuntimeValue::operator[](std::size_t index) const {
    if (!is_array()) throw std::runtime_error("JSON value is not an array");
    return array.at(index);
}

void RuntimeValue::push_back(const RuntimeValue& value) {
    if (is_null()) type = RuntimeType::Array;
    if (!is_array()) throw std::runtime_error("JSON value is not an array");
    array.push_back(value);
}

void RuntimeValue::push_back(RuntimeValue&& value) {
    if (is_null()) type = RuntimeType::Array;
    if (!is_array()) throw std::runtime_error("JSON value is not an array");
    array.push_back(std::move(value));
}

std::string RuntimeValue::dump(int indent) const {
    if (is_error()) {
        if (!error) throw std::runtime_error("invalid Error value");
        std::unordered_set<const RuntimeErrorData*> seen;
        return runtime_error_to_json(*error, 0, seen).dump(indent);
    }
    return runtime_to_json(*this).dump(indent);
}

namespace {
struct SignedDecimal {
    int sign = 0;
    std::string digits = "0";
};

SignedDecimal signed_decimal(std::string_view text) {
    SignedDecimal value;
    std::size_t position = 0;
    int sign = 1;
    if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
        if (text[position++] == '-') sign = -1;
    }
    while (position < text.size() && text[position] == '0') ++position;
    value.digits = position == text.size() ? "0" : std::string(text.substr(position));
    value.sign = value.digits == "0" ? 0 : sign;
    return value;
}

int compare_magnitude(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) return left.size() < right.size() ? -1 : 1;
    return left < right ? -1 : (left > right ? 1 : 0);
}

std::string add_magnitude(const std::string& left, const std::string& right) {
    std::string result;
    const std::size_t size = std::max(left.size(), right.size());
    result.reserve(size + 1);
    int carry = 0;
    for (std::size_t offset = 0; offset < size; ++offset) {
        const int a = offset < left.size() ? left[left.size() - 1 - offset] - '0' : 0;
        const int b = offset < right.size() ? right[right.size() - 1 - offset] - '0' : 0;
        const int sum = a + b + carry;
        result.push_back(static_cast<char>('0' + sum % 10));
        carry = sum / 10;
    }
    if (carry) result.push_back(static_cast<char>('0' + carry));
    std::reverse(result.begin(), result.end());
    return result;
}

std::string subtract_magnitude(const std::string& larger, const std::string& smaller) {
    std::string result;
    result.reserve(larger.size());
    int borrow = 0;
    for (std::size_t offset = 0; offset < larger.size(); ++offset) {
        int digit = larger[larger.size() - 1 - offset] - '0' - borrow;
        const int sub = offset < smaller.size() ? smaller[smaller.size() - 1 - offset] - '0' : 0;
        if (digit < sub) { digit += 10; borrow = 1; }
        else borrow = 0;
        result.push_back(static_cast<char>('0' + digit - sub));
    }
    while (result.size() > 1 && result.back() == '0') result.pop_back();
    std::reverse(result.begin(), result.end());
    return result;
}

SignedDecimal add_signed(SignedDecimal left, const SignedDecimal& right) {
    if (left.sign == 0) return right;
    if (right.sign == 0) return left;
    if (left.sign == right.sign) {
        left.digits = add_magnitude(left.digits, right.digits);
        return left;
    }
    const int comparison = compare_magnitude(left.digits, right.digits);
    if (comparison == 0) return {};
    if (comparison > 0) left.digits = subtract_magnitude(left.digits, right.digits);
    else {
        left.digits = subtract_magnitude(right.digits, left.digits);
        left.sign = right.sign;
    }
    return left;
}

SignedDecimal signed_size(std::size_t value, int sign) {
    if (value == 0) return {};
    return {sign, std::to_string(value)};
}

std::string render_signed(const SignedDecimal& value) {
    if (value.sign == 0) return "0";
    return value.sign < 0 ? "-" + value.digits : value.digits;
}

int compare_signed(const SignedDecimal& left, const SignedDecimal& right) {
    if (left.sign != right.sign) return left.sign < right.sign ? -1 : 1;
    if (left.sign == 0) return 0;
    const int magnitude = compare_magnitude(left.digits, right.digits);
    return left.sign < 0 ? -magnitude : magnitude;
}

enum class NumericKind { NegativeInfinity, Finite, PositiveInfinity, NaN };

struct NumericForm {
    NumericKind kind = NumericKind::Finite;
    int sign = 0;
    std::string coefficient = "0";
    SignedDecimal exponent;
};

bool parse_finite_decimal(std::string_view text, NumericForm& value) {
    std::size_t position = 0;
    int sign = 1;
    if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
        if (text[position++] == '-') sign = -1;
    }
    const std::size_t integer_begin = position;
    while (position < text.size() && text[position] >= '0' && text[position] <= '9') ++position;
    if (position == integer_begin) return false;
    std::string coefficient(text.substr(integer_begin, position - integer_begin));
    std::size_t fractional_digits = 0;
    if (position < text.size() && text[position] == '.') {
        const std::size_t fractional_begin = ++position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9') ++position;
        if (position == fractional_begin) return false;
        fractional_digits = position - fractional_begin;
        coefficient.append(text.substr(fractional_begin, fractional_digits));
    }
    SignedDecimal exponent;
    if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
        const std::size_t exponent_begin = ++position;
        if (position < text.size() && (text[position] == '+' || text[position] == '-')) ++position;
        const std::size_t digits_begin = position;
        while (position < text.size() && text[position] >= '0' && text[position] <= '9') ++position;
        if (position == digits_begin || position != text.size()) return false;
        exponent = signed_decimal(text.substr(exponent_begin));
    } else if (position != text.size()) return false;

    const std::size_t first_nonzero = coefficient.find_first_not_of('0');
    if (first_nonzero == std::string::npos) {
        value = {};
        return true;
    }
    coefficient.erase(0, first_nonzero);
    exponent = add_signed(std::move(exponent), signed_size(fractional_digits, -1));
    std::size_t trailing_zeros = 0;
    while (coefficient.size() > 1 && coefficient.back() == '0') {
        coefficient.pop_back();
        ++trailing_zeros;
    }
    exponent = add_signed(std::move(exponent), signed_size(trailing_zeros, 1));
    value.kind = NumericKind::Finite;
    value.sign = sign;
    value.coefficient = std::move(coefficient);
    value.exponent = std::move(exponent);
    return true;
}

NumericForm numeric_form(const RuntimeValue& value) {
    if (value.type == RuntimeType::Number) {
        if (std::isnan(value.num)) { NumericForm form; form.kind = NumericKind::NaN; return form; }
        if (std::isinf(value.num)) {
            NumericForm form;
            form.kind = value.num < 0 ? NumericKind::NegativeInfinity : NumericKind::PositiveInfinity;
            return form;
        }
        char buffer[64];
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value.num,
                                             std::chars_format::general);
        NumericForm form;
        if (converted.ec == std::errc() && parse_finite_decimal(
                std::string_view(buffer, static_cast<std::size_t>(converted.ptr - buffer)), form))
            return form;
        return {};
    }
    NumericForm form;
    if (parse_finite_decimal(value.string, form)) return form;
    RuntimeValue fallback(value.num);
    return numeric_form(fallback);
}

int compare_finite_magnitude(const NumericForm& left, const NumericForm& right) {
    const SignedDecimal left_scientific = add_signed(
        left.exponent, signed_size(left.coefficient.size() - 1, 1));
    const SignedDecimal right_scientific = add_signed(
        right.exponent, signed_size(right.coefficient.size() - 1, 1));
    const int exponent_comparison = compare_signed(left_scientific, right_scientific);
    if (exponent_comparison != 0) return exponent_comparison;
    const std::size_t size = std::max(left.coefficient.size(), right.coefficient.size());
    for (std::size_t i = 0; i < size; ++i) {
        const char a = i < left.coefficient.size() ? left.coefficient[i] : '0';
        const char b = i < right.coefficient.size() ? right.coefficient[i] : '0';
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;
}

int compare_numeric_forms(const NumericForm& left, const NumericForm& right) {
    if (left.kind != right.kind)
        return static_cast<int>(left.kind) < static_cast<int>(right.kind) ? -1 : 1;
    if (left.kind != NumericKind::Finite) return 0;
    if (left.sign != right.sign) return left.sign < right.sign ? -1 : 1;
    if (left.sign == 0) return 0;
    const int magnitude = compare_finite_magnitude(left, right);
    return left.sign < 0 ? -magnitude : magnitude;
}

bool finite_integer_digits(const NumericForm& form, std::size_t limit,
                           std::string& digits) {
    if (form.kind != NumericKind::Finite) return false;
    if (form.sign == 0) { digits = "0"; return true; }
    if (form.exponent.sign < 0) return false;
    std::size_t zeroes = 0;
    if (form.exponent.sign > 0) {
        if (form.exponent.digits.size() > std::to_string(limit).size()) return false;
        const auto converted = std::from_chars(form.exponent.digits.data(),
                                               form.exponent.digits.data() + form.exponent.digits.size(),
                                               zeroes);
        if (converted.ec != std::errc() ||
            converted.ptr != form.exponent.digits.data() + form.exponent.digits.size()) return false;
    }
    if (form.coefficient.size() > limit || zeroes > limit - form.coefficient.size()) return false;
    digits = form.coefficient;
    digits.append(zeroes, '0');
    return true;
}
} // namespace

bool runtime_number_is_zero(const RuntimeValue& value) {
    if (!value.is_number()) return false;
    // Plain double fast path (mirrors the exact-number decimal path: a double's
    // exact decimal form has sign 0 iff num == 0.0).
    if (value.type == RuntimeType::Number) return value.num == 0.0;
    const NumericForm form = numeric_form(value);
    return form.kind == NumericKind::Finite && form.sign == 0;
}

bool runtime_truthy(const RuntimeValue& value) {
    if (value.is_bool()) return value.boolean;
    if (value.is_null()) return false;
    if (value.is_number()) return !runtime_number_is_zero(value);
    if (value.is_string()) return !value.string.empty();
    if (value.is_array()) return !value.array.empty();
    if (value.is_object()) return !value.object.empty();
    if (value.is_bytes()) return value.bytes && !value.bytes->empty();
    if (value.is_timer()) return true;
    if (value.is_error()) return true;
    return false;
}

bool runtime_contains_error(const RuntimeValue& value) {
    if (value.is_error()) return true;
    if (value.is_array())
        return std::any_of(value.array.begin(), value.array.end(), runtime_contains_error);
    if (value.is_object())
        return std::any_of(value.object.begin(), value.object.end(), [](const auto& entry) {
            return runtime_contains_error(entry.second);
        });
    return false;
}

bool runtime_contains_timer(const RuntimeValue& value) {
    if (value.is_timer()) return true;
    if (value.is_array())
        return std::any_of(value.array.begin(), value.array.end(), runtime_contains_timer);
    if (value.is_object())
        return std::any_of(value.object.begin(), value.object.end(), [](const auto& entry) {
            return runtime_contains_timer(entry.second);
        });
    return false;
}

bool runtime_contains_reserved_handle(const RuntimeValue& value) {
    if (value.is_string() && value.string.rfind("\x1fnift:", 0) == 0) return true;
    if (value.is_array())
        return std::any_of(value.array.begin(), value.array.end(), runtime_contains_reserved_handle);
    if (value.is_object())
        return std::any_of(value.object.begin(), value.object.end(), [](const auto& entry) {
            return runtime_contains_reserved_handle(entry.second);
        });
    return false;
}

bool runtime_number_is_integer(const RuntimeValue& value) {
    if (!value.is_number()) return false;
    // Fast path for the common plain double: the exact-number decimal layer
    // (to_chars + parse_finite_decimal) is only needed to preserve a StrNumber's
    // exact JSON spelling. A Number's integer-ness is the v4.5 trunc check.
    if (value.type == RuntimeType::Number) return std::isfinite(value.num) && std::trunc(value.num) == value.num;
    const NumericForm form = numeric_form(value);
    return form.kind == NumericKind::Finite &&
           (form.sign == 0 || form.exponent.sign >= 0);
}

bool runtime_number_to_size(const RuntimeValue& value, std::size_t& result) {
    if (!value.is_number()) return false;
    if (value.type == RuntimeType::Number) {
        if (!std::isfinite(value.num) || value.num < 0.0 || std::trunc(value.num) != value.num ||
            value.num >= static_cast<double>(std::numeric_limits<std::size_t>::max())) return false;
        result = static_cast<std::size_t>(value.num);
        return true;
    }
    const NumericForm form = numeric_form(value);
    if (form.sign < 0) return false;
    std::string digits;
    const std::string maximum = std::to_string(std::numeric_limits<std::size_t>::max());
    if (!finite_integer_digits(form, maximum.size(), digits) ||
        compare_magnitude(digits, maximum) > 0) return false;
    const auto converted = std::from_chars(digits.data(), digits.data() + digits.size(), result);
    return converted.ec == std::errc() && converted.ptr == digits.data() + digits.size();
}

bool runtime_number_to_signed(const RuntimeValue& value, unsigned bits, std::int64_t& result) {
    if (bits == 0 || bits > 64) return false;
    if (!value.is_number()) return false;
    if (value.type == RuntimeType::Number) {
        const double bound = std::ldexp(1.0, bits - 1);
        if (!std::isfinite(value.num) || std::trunc(value.num) != value.num ||
            value.num < -bound || value.num >= bound) return false;
        result = static_cast<std::int64_t>(value.num);
        return true;
    }
    const NumericForm form = numeric_form(value);
    std::string digits;
    if (!finite_integer_digits(form, 19, digits)) return false;
    const std::string_view maximum = form.sign < 0 ? "9223372036854775808" : "9223372036854775807";
    if (digits.size() > maximum.size() ||
        (digits.size() == maximum.size() && digits > maximum)) return false;
    std::uint64_t magnitude = 0;
    const auto converted = std::from_chars(digits.data(), digits.data() + digits.size(), magnitude);
    if (converted.ec != std::errc() || converted.ptr != digits.data() + digits.size()) return false;
    std::int64_t converted_value = 0;
    if (form.sign < 0) {
        if (magnitude == std::uint64_t{1} << 63) converted_value = std::numeric_limits<std::int64_t>::min();
        else converted_value = -static_cast<std::int64_t>(magnitude);
    } else converted_value = static_cast<std::int64_t>(magnitude);
    if (bits < 64) {
        const std::int64_t minimum = -(std::int64_t{1} << (bits - 1));
        const std::int64_t maximum_value = (std::int64_t{1} << (bits - 1)) - 1;
        if (converted_value < minimum || converted_value > maximum_value) return false;
    }
    result = converted_value;
    return true;
}

bool runtime_number_to_unsigned(const RuntimeValue& value, unsigned bits, std::uint64_t& result) {
    if (bits == 0 || bits > 64 || !value.is_number()) return false;
    if (value.type == RuntimeType::Number) {
        const double bound = std::ldexp(1.0, bits);
        if (!std::isfinite(value.num) || std::trunc(value.num) != value.num ||
            value.num < 0 || value.num >= bound) return false;
        result = static_cast<std::uint64_t>(value.num);
        return true;
    }
    const NumericForm form = numeric_form(value);
    if (form.sign < 0) return false;
    std::string digits;
    const std::string maximum = std::to_string(std::numeric_limits<std::uint64_t>::max());
    if (!finite_integer_digits(form, maximum.size(), digits) ||
        compare_magnitude(digits, maximum) > 0) return false;
    std::uint64_t converted_value = 0;
    const auto converted = std::from_chars(digits.data(), digits.data() + digits.size(), converted_value);
    if (converted.ec != std::errc() || converted.ptr != digits.data() + digits.size()) return false;
    if (bits < 64 && converted_value > ((std::uint64_t{1} << bits) - 1)) return false;
    result = converted_value;
    return true;
}

bool runtime_number_to_i64(const RuntimeValue& value, std::int64_t& result) {
    return runtime_number_to_signed(value, 64, result);
}

RuntimeValue runtime_integer(std::int64_t value) {
    RuntimeValue result(static_cast<double>(value));
    if (value > 9007199254740992LL || value < -9007199254740992LL) {
        result.type = RuntimeType::StrNumber;
        result.string = std::to_string(value);
    }
    return result;
}

RuntimeValue runtime_unsigned_integer(std::uint64_t value) {
    RuntimeValue result(static_cast<double>(value));
    if (value > 9007199254740992ULL) {
        result.type = RuntimeType::StrNumber;
        result.string = std::to_string(value);
    }
    return result;
}

RuntimeValue runtime_number_negate(const RuntimeValue& value) {
    RuntimeValue result = value;
    if (result.type == RuntimeType::StrNumber) {
        if (!result.string.empty() && result.string.front() == '-') result.string.erase(0, 1);
        else result.string.insert(result.string.begin(), '-');
        result.num = -result.num;
    } else result.num = -result.num;
    return result;
}

int runtime_compare_numbers(const RuntimeValue& left, const RuntimeValue& right) {
    if (left.type == RuntimeType::Number && right.type == RuntimeType::Number) {
        const bool left_nan = std::isnan(left.num);
        const bool right_nan = std::isnan(right.num);
        if (left_nan || right_nan) return left_nan == right_nan ? 0 : (left_nan ? 1 : -1);
        return left.num < right.num ? -1 : (left.num > right.num ? 1 : 0);
    }
    return compare_numeric_forms(numeric_form(left), numeric_form(right));
}

bool runtime_numbers_equal(const RuntimeValue& left, const RuntimeValue& right) {
    if (left.type == RuntimeType::Number && right.type == RuntimeType::Number)
        return left.num == right.num;
    const NumericForm left_form = numeric_form(left);
    const NumericForm right_form = numeric_form(right);
    if (left_form.kind == NumericKind::NaN || right_form.kind == NumericKind::NaN) return false;
    return compare_numeric_forms(left_form, right_form) == 0;
}

bool runtime_compare_numbers_relational(const RuntimeValue& left, const RuntimeValue& right,
                                        int& comparison) {
    if (left.type == RuntimeType::Number && right.type == RuntimeType::Number) {
        if (std::isnan(left.num) || std::isnan(right.num)) return false;
        comparison = left.num < right.num ? -1 : (left.num > right.num ? 1 : 0);
        return true;
    }
    const NumericForm left_form = numeric_form(left);
    const NumericForm right_form = numeric_form(right);
    if (left_form.kind == NumericKind::NaN || right_form.kind == NumericKind::NaN) return false;
    comparison = compare_numeric_forms(left_form, right_form);
    return true;
}

std::string runtime_numeric_fingerprint(const RuntimeValue& value) {
    const NumericForm form = numeric_form(value);
    if (form.kind == NumericKind::NaN) return "nan";
    if (form.kind == NumericKind::NegativeInfinity) return "-inf";
    if (form.kind == NumericKind::PositiveInfinity) return "+inf";
    if (form.sign == 0) return "0";
    return std::string(form.sign < 0 ? "-" : "+") + form.coefficient + "e" +
           render_signed(form.exponent);
}

bool runtime_equal(const RuntimeValue& left, const RuntimeValue& right) {
    if (left.is_number() && right.is_number()) return runtime_numbers_equal(left, right);
    if (left.type != right.type) return false;
    if (left.is_null()) return true;
    if (left.is_bool()) return left.boolean == right.boolean;
    if (left.is_string()) return left.string == right.string;
    if (left.is_bytes()) {
        const RuntimeBytes empty;
        return (left.bytes ? *left.bytes : empty) == (right.bytes ? *right.bytes : empty);
    }
    if (left.is_timer())
        return left.timer && right.timer && left.timer->owner == right.timer->owner &&
               left.timer->instance == right.timer->instance;
    if (left.is_error()) {
        std::unordered_set<const RuntimeErrorData*> left_seen, right_seen;
        auto l = left.error;
        auto r = right.error;
        std::size_t depth = 0;
        while (l && r) {
            if (++depth > 16 || !left_seen.insert(l.get()).second || !right_seen.insert(r.get()).second)
                return false;
            if (l->message != r->message || l->code != r->code || l->category != r->category ||
                l->source != r->source || l->line != r->line || l->column != r->column)
                return false;
            l = l->cause;
            r = r->cause;
        }
        return !l && !r;
    }
    if (left.is_array()) {
        if (left.array.size() != right.array.size()) return false;
        for (std::size_t i = 0; i < left.array.size(); ++i)
            if (!runtime_equal(left.array[i], right.array[i])) return false;
        return true;
    }
    if (left.is_object()) {
        if (left.object.size() != right.object.size()) return false;
        std::vector<bool> matched(right.object.size(), false);
        for (const auto& entry : left.object) {
            bool found = false;
            for (std::size_t i = 0; i < right.object.size(); ++i) {
                if (!matched[i] && entry.first == right.object[i].first &&
                    runtime_equal(entry.second, right.object[i].second)) {
                    matched[i] = true;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }
    return false;
}

namespace {
void append_fingerprint(std::string& out, const RuntimeValue& value) {
    if (value.is_number()) {
        out += "2:" + runtime_numeric_fingerprint(value);
        return;
    }
    out += std::to_string(static_cast<int>(value.type)) + ":";
    if (value.is_null()) return;
    if (value.is_bool()) { out += value.boolean ? "1" : "0"; return; }
    if (value.is_string()) {
        out += std::to_string(value.string.size()) + ":" + value.string;
        return;
    }
    if (value.is_bytes()) {
        const RuntimeBytes empty;
        const auto& bytes = value.bytes ? *value.bytes : empty;
        out += std::to_string(bytes.size()) + ":";
        if (!bytes.empty())
            out.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return;
    }
    if (value.is_timer()) {
        if (value.timer) out += std::to_string(value.timer->owner) + ":" + std::to_string(value.timer->instance);
        return;
    }
    if (value.is_error()) {
        if (!value.error) return;
        std::unordered_set<const RuntimeErrorData*> seen;
        auto current=value.error;std::size_t depth=0;
        while(current&&depth++<16&&seen.insert(current.get()).second){
            auto append_text=[&](const std::string& text){out+=std::to_string(text.size())+":"+text;};
            append_text(current->message);append_text(current->code);append_text(current->category);append_text(current->source);
            out+=std::to_string(current->line)+":"+std::to_string(current->column)+":";
            current=current->cause;
        }
        out+=current?"invalid":"null";
        return;
    }
    if (value.is_array()) {
        out += std::to_string(value.array.size()) + "[";
        for (const auto& item : value.array) append_fingerprint(out, item);
        out += "]";
        return;
    }
    out += std::to_string(value.object.size()) + "{";
    std::vector<std::string> entries;
    entries.reserve(value.object.size());
    for (const auto& entry : value.object) {
        std::string encoded = std::to_string(entry.first.size()) + ":" + entry.first;
        append_fingerprint(encoded, entry.second);
        entries.push_back(std::move(encoded));
    }
    std::sort(entries.begin(), entries.end());
    for (const auto& entry : entries)
        out += std::to_string(entry.size()) + ":" + entry;
    out += "}";
}
} // namespace

std::string runtime_fingerprint(const RuntimeValue& value) {
    std::string result;
    append_fingerprint(result, value);
    return result;
}

RuntimeValue runtime_from_json(const json::Document& document) {
    RuntimeValue value;
    switch (document.type) {
        case json::Type::Null: break;
        case json::Type::Boolean:
            value.type = RuntimeType::Boolean; value.boolean = document.boolean; break;
        case json::Type::Number:
            value.type = RuntimeType::Number; value.num = document.num; break;
        case json::Type::StrNumber:
            value.type = RuntimeType::StrNumber; value.num = document.num; value.string = document.string; break;
        case json::Type::String:
            value.type = RuntimeType::String; value.string = document.string; break;
        case json::Type::Array:
            value = RuntimeValue::make_array();
            value.array.reserve(document.array.size());
            for (const auto& item : document.array) value.array.push_back(runtime_from_json(item));
            break;
        case json::Type::Object:
            value = RuntimeValue::make_object();
            value.object.reserve(document.object.size());
            for (const auto& entry : document.object)
                value.object.emplace_back(entry.first, runtime_from_json(entry.second));
            break;
    }
    return value;
}

namespace {
bool runtime_to_json_without_timer(const RuntimeValue& value, json::Document& output, std::string& error) {
#ifdef NIFT_TEST_JSON_PREFLIGHT_STATS
    ++json_preflight_stats.conversions;
#endif
    json::Document document;
    switch (value.type) {
        case RuntimeType::Null: break;
        case RuntimeType::Boolean:
            document.type = json::Type::Boolean; document.boolean = value.boolean; break;
        case RuntimeType::Number:
            document.type = json::Type::Number; document.num = value.num; break;
        case RuntimeType::StrNumber:
            document.type = json::Type::StrNumber; document.num = value.num; document.string = value.string; break;
        case RuntimeType::String:
            document.type = json::Type::String; document.string = value.string; break;
        case RuntimeType::Array:
            document = json::Document::make_array();
            document.array.reserve(value.array.size());
            for (const auto& item : value.array) {
                json::Document converted;
                if (!runtime_to_json_without_timer(item, converted, error)) return false;
                document.array.push_back(std::move(converted));
            }
            break;
        case RuntimeType::Object:
            document = json::Document::make_object();
            document.object.reserve(value.object.size());
            for (const auto& entry : value.object) {
                json::Document converted;
                if (!runtime_to_json_without_timer(entry.second, converted, error)) return false;
                document.object.emplace_back(entry.first, std::move(converted));
            }
            break;
        case RuntimeType::Bytes:
            error = "bytes values are not JSON serializable";
            return false;
        case RuntimeType::Timer:
            error = "timer values are not JSON serializable";
            return false;
        case RuntimeType::Error:
            error = "Error values are not JSON serializable";
            return false;
    }
    output = std::move(document);
    error.clear();
    return true;
}

} // namespace

bool runtime_to_json(const RuntimeValue& value, json::Document& output, std::string& error) {
#ifdef NIFT_TEST_JSON_PREFLIGHT_STATS
    ++json_preflight_stats.preflights;
#endif
    // Preserve whole-value timer error priority, but do not rescan every
    // subtree while recursively converting that already checked value.
    if (runtime_contains_timer(value)) {
        error = "timer values are not JSON serializable";
        return false;
    }
    return runtime_to_json_without_timer(value, output, error);
}

json::Document runtime_to_json(const RuntimeValue& value) {
    json::Document document;
    std::string error;
    if (!runtime_to_json(value, document, error)) throw std::runtime_error(error);
    return document;
}

} // namespace nift
