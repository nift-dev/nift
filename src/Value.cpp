#include "nift/value.h"

#include "ValueInternal.h"
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace nift {

// impl_ == nullptr is the canonical Null representation. Read operations
// treat it as Null without allocating; mutation materialises an Impl on
// demand (allocation is allowed there). Move construction/assignment are pure
// pointer moves: nothrow, no allocation, and the moved-from source is left as
// a valid Null Value (impl_ == nullptr).

Value::Value() = default;
Value::Value(bool value) : impl_(std::make_shared<Impl>()) {
    impl_->value = RuntimeValue(value);
}
Value::Value(int value) : impl_(std::make_shared<Impl>()) {
    impl_->value = RuntimeValue(value);
}
Value::Value(double value) : impl_(std::make_shared<Impl>()) {
    impl_->value = RuntimeValue(value);
}
Value::Value(const char* value) : impl_(std::make_shared<Impl>()) {
    impl_->value = RuntimeValue(value);
}
Value::Value(std::string value) : impl_(std::make_shared<Impl>()) {
    impl_->value = RuntimeValue(std::move(value));
}
Value::Value(Bytes value) : impl_(std::make_shared<Impl>()) {
    impl_->value = RuntimeValue(std::move(value));
}

// Deep-copy value semantics: copying a Value produces an independent
// equivalent value; copying a Null Value stays Null.
Value::Value(const Value& other)
    : impl_(other.impl_ ? std::make_shared<Impl>(*other.impl_) : nullptr) {}
Value::Value(Value&& other) noexcept = default;
Value& Value::operator=(const Value& other) {
    if (this != &other)
        impl_ = other.impl_ ? std::make_shared<Impl>(*other.impl_) : nullptr;
    return *this;
}
Value& Value::operator=(Value&& other) noexcept = default;
Value::~Value() = default;

Value Value::make_array() {
    Value value;
    value.impl_ = std::make_shared<Impl>();
    value.impl_->value = RuntimeValue::make_array();
    return value;
}
Value Value::make_object() {
    Value value;
    value.impl_ = std::make_shared<Impl>();
    value.impl_->value = RuntimeValue::make_object();
    return value;
}

Value::Type Value::type() const {
    if (!impl_) return Type::Null;
    switch (impl_->value.type) {
        case RuntimeType::Null: return Type::Null;
        case RuntimeType::Boolean: return Type::Boolean;
        case RuntimeType::Number:
        case RuntimeType::StrNumber: return Type::Number;
        case RuntimeType::String: return Type::String;
        case RuntimeType::Array: return Type::Array;
        case RuntimeType::Object: return Type::Object;
        case RuntimeType::Bytes: return Type::Bytes;
        case RuntimeType::Timer:
            throw std::logic_error("timer value escaped the runtime boundary");
    }
    return Type::Null;
}
bool Value::is_null() const { return !impl_ || impl_->value.is_null(); }
bool Value::is_bool() const { return impl_ && impl_->value.is_bool(); }
bool Value::is_number() const { return impl_ && impl_->value.is_number(); }
bool Value::is_string() const { return impl_ && impl_->value.is_string(); }
bool Value::is_array() const { return impl_ && impl_->value.is_array(); }
bool Value::is_object() const { return impl_ && impl_->value.is_object(); }
bool Value::is_bytes() const { return impl_ && impl_->value.is_bytes(); }

double Value::number() const { return impl_ ? impl_->value.num : 0.0; }
bool Value::boolean() const { return impl_ && impl_->value.boolean; }
const std::string& Value::string() const {
    static const std::string empty;
    return impl_ ? impl_->value.string : empty;
}
const Value::Bytes& Value::bytes() const {
    static const Bytes empty;
    return impl_ && impl_->value.bytes ? *impl_->value.bytes : empty;
}
std::string Value::json() const { return impl_ ? impl_->value.dump(0) : std::string("null"); }

void Value::push_back(const Value& value) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    impl_->value.push_back(value.impl_ ? value.impl_->value : RuntimeValue{});
}

ValueRef Value::operator[](const std::string& key) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    return ValueRef(reinterpret_cast<void*>(&impl_->value[key]));
}
ValueRef Value::operator[](std::size_t index) {
    if (!impl_) impl_ = std::make_shared<Impl>();
    return ValueRef(reinterpret_cast<void*>(&impl_->value[index]));
}

ValueRef::ValueRef(void* node) : node_(node) {}

ValueRef& ValueRef::operator=(const Value& value) {
    *reinterpret_cast<RuntimeValue*>(node_) = value.impl_ ? value.impl_->value : RuntimeValue{};
    return *this;
}
ValueRef& ValueRef::operator=(const ValueRef& other) {
    *reinterpret_cast<RuntimeValue*>(node_) = *reinterpret_cast<RuntimeValue*>(other.node_);
    return *this;
}
ValueRef ValueRef::operator[](const std::string& key) {
    return ValueRef(reinterpret_cast<void*>(&(*reinterpret_cast<RuntimeValue*>(node_))[key]));
}
ValueRef ValueRef::operator[](std::size_t index) {
    return ValueRef(reinterpret_cast<void*>(&(*reinterpret_cast<RuntimeValue*>(node_))[index]));
}

} // namespace nift
