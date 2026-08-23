#pragma once

#include <cstddef>
#include <memory>
#include <string>

namespace nift {

class Value;
class ValueRef;
struct ValueAccess;   // internal implementation bridge (defined in src/)

// Mutable reference to a member/element inside a Value's document tree,
// enabling idiomatic structured construction such as:
//   Value user = Value::make_object();
//   user["name"] = Value("Nick");
//   user["projects"][0] = Value("nift");
//
// Lifetime contract: a ValueRef is a transient construction handle valid only
// while the Value it was obtained from is alive and that Value's storage is
// not replaced (e.g. by move-assignment into it). Never retain a ValueRef
// beyond the building expression it participates in.
class ValueRef {
public:
    ValueRef& operator=(const Value& value);
    ValueRef& operator=(const ValueRef& other);
    ValueRef operator[](const std::string& key);
    ValueRef operator[](std::size_t index);

private:
    friend class Value;
    friend struct ValueAccess;
    explicit ValueRef(void* node);
    void* node_ = nullptr;
};

// Nift's public value model. The internal representation is an implementation
// detail: nift::Value is Nift's contract (and must stay that way for the
// future independent Rust/Go/JS implementations), not an alias of the C++
// backing store.
//
// Value semantics (CP7c contract):
// - copy construction/assignment produce an independent equivalent value;
//   mutating either copy does not affect the other
// - move construction/assignment transfer ownership without copying and leave
//   the source as a valid Null Value; moves are nothrow
// - a default-constructed or moved-from Value is Null
// - type-mismatched reads return the type's default: number() -> 0.0,
//   boolean() -> false, string() -> empty
// - operator[](string) materialises a Null Value as an Object and otherwise
//   throws std::runtime_error on a non-object Value. operator[](size_t) and
//   push_back require an Array Value (a Null member must first be assigned
//   make_array()) and otherwise throw std::runtime_error.
class Value {
public:
    enum class Type { Null, Boolean, Number, String, Array, Object };

    Value();
    Value(bool value);
    Value(int value);
    Value(double value);
    Value(const char* value);
    Value(std::string value);
    Value(const Value&);
    Value(Value&&) noexcept;
    Value& operator=(const Value&);
    Value& operator=(Value&&) noexcept;
    ~Value();

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

    ValueRef operator[](const std::string& key);
    ValueRef operator[](std::size_t index);

private:
    friend struct ValueAccess;
    friend class ValueRef;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace nift
