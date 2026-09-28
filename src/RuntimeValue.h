#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nift {

enum class RuntimeType { Null, Boolean, Number, StrNumber, String, Array, Object };

// Nift-owned recursive runtime substrate. StrNumber retains an exact JSON-number
// spelling when conversion to double would lose its decimal semantics.
class RuntimeValue {
public:
    RuntimeType type = RuntimeType::Null;
    double num = 0.0;
    bool boolean = false;
    std::string string;
    std::vector<RuntimeValue> array;
    std::vector<std::pair<std::string, RuntimeValue>> object;

    RuntimeValue() = default;
    RuntimeValue(std::nullptr_t) : type(RuntimeType::Null) {}
    RuntimeValue(bool value) : type(RuntimeType::Boolean), boolean(value) {}
    RuntimeValue(int value) : type(RuntimeType::Number), num(value) {}
    RuntimeValue(double value) : type(RuntimeType::Number), num(value) {}
    RuntimeValue(const char* value) : type(RuntimeType::String), string(value ? value : "") {}
    RuntimeValue(const std::string& value) : type(RuntimeType::String), string(value) {}
    RuntimeValue(std::string&& value) : type(RuntimeType::String), string(std::move(value)) {}

    static RuntimeValue make_array() { RuntimeValue value; value.type = RuntimeType::Array; return value; }
    static RuntimeValue make_object() { RuntimeValue value; value.type = RuntimeType::Object; return value; }

    bool is_null() const { return type == RuntimeType::Null; }
    bool is_bool() const { return type == RuntimeType::Boolean; }
    bool is_number() const { return type == RuntimeType::Number || type == RuntimeType::StrNumber; }
    bool is_string() const { return type == RuntimeType::String; }
    bool is_array() const { return type == RuntimeType::Array; }
    bool is_object() const { return type == RuntimeType::Object; }

    bool has(const std::string& key) const;
    RuntimeValue& operator[](const std::string& key);
    const RuntimeValue& operator[](const std::string& key) const;
    RuntimeValue& operator[](std::size_t index);
    const RuntimeValue& operator[](std::size_t index) const;
    void push_back(const RuntimeValue& value);
    void push_back(RuntimeValue&& value);

    std::string dump(int indent = 0) const;
};

bool runtime_equal(const RuntimeValue& left, const RuntimeValue& right);
bool runtime_numbers_equal(const RuntimeValue& left, const RuntimeValue& right);
bool runtime_compare_numbers_relational(const RuntimeValue& left, const RuntimeValue& right,
                                        int& comparison);
int runtime_compare_numbers(const RuntimeValue& left, const RuntimeValue& right);
std::string runtime_numeric_fingerprint(const RuntimeValue& value);
std::string runtime_fingerprint(const RuntimeValue& value);

} // namespace nift
