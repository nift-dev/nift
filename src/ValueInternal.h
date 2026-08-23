#pragma once

// Private implementation bridge for the public nift::Value abstraction. This
// header is internal to the C++ implementation and never part of the public
// include surface: consumers of <nift/nift.h> must not need to know Jsonic++
// exists. See include/nift/value.h for the public contract.

#include "Json.h"
#include "nift/value.h"

struct nift::Value::Impl {
    json::Document doc;
};

namespace nift {

struct ValueAccess {
    static json::Document& doc(Value& value) { return value.impl_->doc; }
    static const json::Document& doc(const Value& value) { return value.impl_->doc; }
};

} // namespace nift
