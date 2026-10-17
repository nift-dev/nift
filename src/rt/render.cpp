#include "rt/render.h"

#include "RuntimeValue.h"
#include "rt/string.h"

#include <charconv>

namespace nift::rt {

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
        case Kind::Bool: out += value.as_bool() ? "true" : "false"; return true;
        case Kind::Null: out += "null"; return true;
        default: return false;
    }
}

} // namespace nift::rt
