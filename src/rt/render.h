#pragma once
// Text rendering of scalar values for the Runtime 2.0 VM (CP11), identical to
// the legacy `render_expression_value` for the kinds the VM produces:
// strings as-is, numbers as legacy RuntimeValue::dump (shortest round trip,
// integral values without ".0"), `true`/`false`, `null`.
#include "rt/value.h"

#include <string>

namespace nift::rt {
struct FunctionObject;
std::string callable_marker(const FunctionObject& function);

// Appends the rendering of `value` to `out`. Returns false (appending
// nothing) for values legacy does not render as text (arrays, objects,
// bytes, errors, callables, handles, decimals).
bool append_rendered(const Value& value, std::string& out);

} // namespace nift::rt
