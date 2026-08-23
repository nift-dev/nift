#pragma once

// Private implementation bridge for the public nift::Value abstraction. This
// header is internal to the C++ implementation and never part of the public
// include surface: consumers of <nift/nift.h> must not need to know Jsonic++
// exists. See include/nift/value.h for the public contract.
//
// impl_ == nullptr is the canonical Null representation, so the mutable
// document accessor materialises an Impl on demand (mutation may allocate);
// the const accessor returns an empty document for a Null Value.

#include <memory>

#include "Json.h"
#include "nift/value.h"

struct nift::Value::Impl {
    json::Document doc;
};

namespace nift {

struct ValueAccess {
    static json::Document& doc(Value& value) {
        if (!value.impl_) value.impl_ = std::make_shared<Value::Impl>();
        return value.impl_->doc;
    }
    static const json::Document& doc(const Value& value) {
        static const json::Document empty;
        return value.impl_ ? value.impl_->doc : empty;
    }
};

} // namespace nift
