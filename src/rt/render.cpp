#include "rt/render.h"
#include "rt/heap.h"

#include "RuntimeValue.h"
#include "rt/string.h"

#include <charconv>

namespace nift::rt {
std::string callable_marker(const FunctionObject& function) {
    return std::string("\x1fnift:callable:") + (function.is_lambda
        ? "lambda:" + std::to_string(function.display_id) : "named:" + function.name);
}


bool append_rendered(const Value& value, std::string& out) {
    switch (value.kind()) {
        case Kind::String: {
            const std::string_view text = string_view_of(value);
            // Legacy treats "\x1fnift:" strings as handles; the VM never
            // creates them (literals containing 0x1f are not compiled).
            out.append(text.data(), text.size());
            return true;
        }
        case Kind::Int: {
            // The VM only keeps integers below 2^53 in magnitude (larger ones
            // bail), where legacy holds the same value as an exact double and
            // prints its integer digits.
            char buffer[24];
            const auto result = std::to_chars(buffer, buffer + sizeof buffer, value.as_int());
            out.append(buffer, static_cast<std::size_t>(result.ptr - buffer));
            return true;
        }
        case Kind::Double: out += nift::RuntimeValue(value.as_double()).dump(0); return true;
        case Kind::Decimal: {
            // Exact numbers beyond int64 (only -INT64_MIN reaches the VM):
            // legacy prints its StrNumber spelling, 9223372036854775808
            // (review CP12 round 1, D3).
            const std::string_view spelling = decimal_spelling(value);
            out.append(spelling.data(), spelling.size());
            return true;
        }
        case Kind::Bool: out += value.as_bool() ? "true" : "false"; return true;
        case Kind::Null: out += "null"; return true;
        default: return false;
    }
}

} // namespace nift::rt
