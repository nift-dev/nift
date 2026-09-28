#include "RuntimeValue.h"

#include "RuntimeJson.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>

namespace nift {

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
} // namespace

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

json::Document runtime_to_json(const RuntimeValue& value) {
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
            for (const auto& item : value.array) document.array.push_back(runtime_to_json(item));
            break;
        case RuntimeType::Object:
            document = json::Document::make_object();
            document.object.reserve(value.object.size());
            for (const auto& entry : value.object)
                document.object.emplace_back(entry.first, runtime_to_json(entry.second));
            break;
    }
    return document;
}

} // namespace nift
