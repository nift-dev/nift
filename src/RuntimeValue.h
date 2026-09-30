#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nift {

enum class RuntimeType { Null, Boolean, Number, StrNumber, String, Array, Object, Bytes, Timer };

using RuntimeBytes = std::vector<std::uint8_t>;
struct RuntimeTimerIdentity {
    const std::uint64_t owner;
    const std::uint64_t instance;
};

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
    std::shared_ptr<const RuntimeBytes> bytes;
    std::shared_ptr<const RuntimeTimerIdentity> timer;

    RuntimeValue() = default;
    RuntimeValue(std::nullptr_t) : type(RuntimeType::Null) {}
    RuntimeValue(bool value) : type(RuntimeType::Boolean), boolean(value) {}
    RuntimeValue(int value) : type(RuntimeType::Number), num(value) {}
    RuntimeValue(double value) : type(RuntimeType::Number), num(value) {}
    RuntimeValue(const char* value) : type(RuntimeType::String), string(value ? value : "") {}
    RuntimeValue(const std::string& value) : type(RuntimeType::String), string(value) {}
    RuntimeValue(std::string&& value) : type(RuntimeType::String), string(std::move(value)) {}
    explicit RuntimeValue(RuntimeBytes value)
        : type(RuntimeType::Bytes), bytes(std::make_shared<const RuntimeBytes>(std::move(value))) {}

    static RuntimeValue make_array() { RuntimeValue value; value.type = RuntimeType::Array; return value; }
    static RuntimeValue make_object() { RuntimeValue value; value.type = RuntimeType::Object; return value; }
    static RuntimeValue make_timer(std::uint64_t owner, std::uint64_t instance) {
        RuntimeValue value; value.type = RuntimeType::Timer;
        value.timer = std::make_shared<const RuntimeTimerIdentity>(RuntimeTimerIdentity{owner, instance});
        return value;
    }

    bool is_null() const { return type == RuntimeType::Null; }
    bool is_bool() const { return type == RuntimeType::Boolean; }
    bool is_number() const { return type == RuntimeType::Number || type == RuntimeType::StrNumber; }
    bool is_string() const { return type == RuntimeType::String; }
    bool is_array() const { return type == RuntimeType::Array; }
    bool is_object() const { return type == RuntimeType::Object; }
    bool is_bytes() const { return type == RuntimeType::Bytes; }
    bool is_timer() const { return type == RuntimeType::Timer; }

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
bool runtime_number_is_zero(const RuntimeValue& value);
bool runtime_truthy(const RuntimeValue& value);
bool runtime_number_is_integer(const RuntimeValue& value);
bool runtime_number_to_size(const RuntimeValue& value, std::size_t& result);
bool runtime_number_to_signed(const RuntimeValue& value, unsigned bits, std::int64_t& result);
bool runtime_number_to_unsigned(const RuntimeValue& value, unsigned bits, std::uint64_t& result);
bool runtime_number_to_i64(const RuntimeValue& value, std::int64_t& result);
RuntimeValue runtime_integer(std::int64_t value);
RuntimeValue runtime_unsigned_integer(std::uint64_t value);
RuntimeValue runtime_number_negate(const RuntimeValue& value);
bool runtime_numbers_equal(const RuntimeValue& left, const RuntimeValue& right);
bool runtime_compare_numbers_relational(const RuntimeValue& left, const RuntimeValue& right,
                                        int& comparison);
int runtime_compare_numbers(const RuntimeValue& left, const RuntimeValue& right);
std::string runtime_numeric_fingerprint(const RuntimeValue& value);
std::string runtime_fingerprint(const RuntimeValue& value);
bool runtime_valid_utf8(std::string_view value);
bool runtime_contains_timer(const RuntimeValue& value);
bool runtime_contains_reserved_handle(const RuntimeValue& value);

} // namespace nift
