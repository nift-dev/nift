#pragma once

#include "Json.h"
#include "RuntimeValue.h"

namespace nift {

RuntimeValue runtime_from_json(const json::Document& document);
json::Document runtime_to_json(const RuntimeValue& value);

} // namespace nift
