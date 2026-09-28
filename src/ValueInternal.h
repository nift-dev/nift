#pragma once

// Private implementation bridge for the public nift::Value abstraction.
//
// impl_ == nullptr is the canonical Null representation, so the mutable
// runtime accessor materialises an Impl on demand (mutation may allocate);
// the const accessor returns an empty value for a Null Value.

#include <memory>

#include "RuntimeValue.h"
#include "nift/value.h"

struct nift::Value::Impl {
    RuntimeValue value;
};

namespace nift {

struct ValueAccess {
    static RuntimeValue& runtime(Value& value) {
        if (!value.impl_) value.impl_ = std::make_shared<Value::Impl>();
        return value.impl_->value;
    }
    static const RuntimeValue& runtime(const Value& value) {
        static const RuntimeValue empty;
        return value.impl_ ? value.impl_->value : empty;
    }
};

} // namespace nift
