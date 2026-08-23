#include "nift/value.h"

#include "ValueInternal.h"

#include <cstddef>
#include <string>
#include <utility>

namespace nift {

Value::Value() : impl_(std::make_shared<Impl>()) {}
Value::Value(bool value) : impl_(std::make_shared<Impl>()) {
    impl_->doc.type = json::Type::Boolean;
    impl_->doc.boolean = value;
}
Value::Value(int value) : impl_(std::make_shared<Impl>()) {
    impl_->doc.type = json::Type::Number;
    impl_->doc.num = value;
}
Value::Value(double value) : impl_(std::make_shared<Impl>()) {
    impl_->doc.type = json::Type::Number;
    impl_->doc.num = value;
}
Value::Value(const char* value) : impl_(std::make_shared<Impl>()) {
    impl_->doc.type = json::Type::String;
    impl_->doc.string = value ? value : "";
}
Value::Value(std::string value) : impl_(std::make_shared<Impl>()) {
    impl_->doc.type = json::Type::String;
    impl_->doc.string = std::move(value);
}
// Deep-copy value semantics: copying a Value produces an independent
// equivalent value (mutation of either must not affect the other); moving
// transfers ownership without copying.
Value::Value(const Value& other) : impl_(std::make_shared<Impl>(*other.impl_)) {}
Value::Value(Value&& other) noexcept = default;
Value& Value::operator=(const Value& other) {
    if (this != &other) impl_ = std::make_shared<Impl>(*other.impl_);
    return *this;
}
Value& Value::operator=(Value&& other) noexcept = default;
Value::~Value() = default;

Value Value::make_array() {
    Value value;
    value.impl_->doc = json::Document::make_array();
    return value;
}
Value Value::make_object() {
    Value value;
    value.impl_->doc = json::Document::make_object();
    return value;
}

Value::Type Value::type() const {
    switch (impl_->doc.type) {
        case json::Type::Null: return Type::Null;
        case json::Type::Boolean: return Type::Boolean;
        case json::Type::Number: return Type::Number;
        case json::Type::String: return Type::String;
        case json::Type::Array: return Type::Array;
        case json::Type::Object: return Type::Object;
    }
    return Type::Null;
}
bool Value::is_null() const { return impl_->doc.is_null(); }
bool Value::is_bool() const { return impl_->doc.is_bool(); }
bool Value::is_number() const { return impl_->doc.is_number(); }
bool Value::is_string() const { return impl_->doc.is_string(); }
bool Value::is_array() const { return impl_->doc.is_array(); }
bool Value::is_object() const { return impl_->doc.is_object(); }

double Value::number() const { return impl_->doc.num; }
bool Value::boolean() const { return impl_->doc.boolean; }
const std::string& Value::string() const { return impl_->doc.string; }

void Value::push_back(const Value& value) { impl_->doc.push_back(value.impl_->doc); }

ValueRef Value::operator[](const std::string& key) {
    return ValueRef(reinterpret_cast<void*>(&impl_->doc[key]));
}
ValueRef Value::operator[](std::size_t index) {
    return ValueRef(reinterpret_cast<void*>(&impl_->doc[index]));
}

ValueRef::ValueRef(void* node) : node_(node) {}

ValueRef& ValueRef::operator=(const Value& value) {
    *reinterpret_cast<json::Document*>(node_) = value.impl_->doc;
    return *this;
}
ValueRef& ValueRef::operator=(const ValueRef& other) {
    *reinterpret_cast<json::Document*>(node_) = *reinterpret_cast<json::Document*>(other.node_);
    return *this;
}
ValueRef ValueRef::operator[](const std::string& key) {
    return ValueRef(reinterpret_cast<void*>(&(*reinterpret_cast<json::Document*>(node_))[key]));
}
ValueRef ValueRef::operator[](std::size_t index) {
    return ValueRef(reinterpret_cast<void*>(&(*reinterpret_cast<json::Document*>(node_))[index]));
}

} // namespace nift
