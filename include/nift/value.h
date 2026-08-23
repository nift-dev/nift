#pragma once

#include <cstddef>
#include <string>
#include "Json.h"

namespace nift {

class Value;

// Mutable reference to a member/element inside a Value's document tree,
// enabling idiomatic structured construction such as:
//   Value user = Value::make_object();
//   user["name"] = Value("Nick");
//   user["projects"][0] = Value("nift");
class ValueRef {
public:
    explicit ValueRef(json::Document* node) : node_(node) {}

    ValueRef& operator=(const Value& value);
    ValueRef& operator=(const ValueRef& other);

    ValueRef operator[](const std::string& key) { return ValueRef(&(*node_)[key]); }
    ValueRef operator[](std::size_t index) { return ValueRef(&(*node_)[index]); }

private:
    json::Document* node_;
};

// Nift's public value model. Backed internally by Jsonic++'s json::Document,
// but nift::Value is Nift's contract rather than a Jsonic++ alias, so future
// Nift implementations (Rust/Go/JS) define their own equivalent value type.
class Value {
public:
    enum class Type { Null, Boolean, Number, String, Array, Object };

    Value() = default;
    Value(bool value);
    Value(int value);
    Value(double value);
    Value(const char* value);
    Value(std::string value);

    static Value make_array();
    static Value make_object();

    Type type() const;
    bool is_null() const;
    bool is_bool() const;
    bool is_number() const;
    bool is_string() const;
    bool is_array() const;
    bool is_object() const;

    double number() const;
    bool boolean() const;
    const std::string& string() const;

    void push_back(const Value& value);

    ValueRef operator[](const std::string& key) { return ValueRef(&value_[key]); }
    ValueRef operator[](std::size_t index) { return ValueRef(&value_[index]); }

    // Internal escape hatch for the Embedded Nift engine (bindings build on
    // this). Not part of the public construction surface beyond set().
    json::Document& document();
    const json::Document& document() const;

private:
    json::Document value_;
};

inline ValueRef& ValueRef::operator=(const Value& value) {
    *node_ = value.document();
    return *this;
}
inline ValueRef& ValueRef::operator=(const ValueRef& other) {
    *node_ = *other.node_;
    return *this;
}

inline Value::Value(bool value) { value_.type = json::Type::Boolean; value_.boolean = value; }
inline Value::Value(int value) { value_.type = json::Type::Number; value_.num = value; }
inline Value::Value(double value) { value_.type = json::Type::Number; value_.num = value; }
inline Value::Value(const char* value) { value_.type = json::Type::String; value_.string = value ? value : ""; }
inline Value::Value(std::string value) { value_.type = json::Type::String; value_.string = std::move(value); }

inline Value Value::make_array() { Value v; v.value_ = json::Document::make_array(); return v; }
inline Value Value::make_object() { Value v; v.value_ = json::Document::make_object(); return v; }

inline Value::Type Value::type() const {
    switch (value_.type) {
        case json::Type::Null: return Type::Null;
        case json::Type::Boolean: return Type::Boolean;
        case json::Type::Number: return Type::Number;
        case json::Type::String: return Type::String;
        case json::Type::Array: return Type::Array;
        case json::Type::Object: return Type::Object;
    }
    return Type::Null;
}
inline bool Value::is_null() const { return value_.is_null(); }
inline bool Value::is_bool() const { return value_.is_bool(); }
inline bool Value::is_number() const { return value_.is_number(); }
inline bool Value::is_string() const { return value_.is_string(); }
inline bool Value::is_array() const { return value_.is_array(); }
inline bool Value::is_object() const { return value_.is_object(); }

inline double Value::number() const { return value_.num; }
inline bool Value::boolean() const { return value_.boolean; }
inline const std::string& Value::string() const { return value_.string; }

inline void Value::push_back(const Value& value) { value_.push_back(value.document()); }
inline json::Document& Value::document() { return value_; }
inline const json::Document& Value::document() const { return value_; }

} // namespace nift
