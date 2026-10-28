#include "rt/native.h"

#include "rt/render.h"


// rt-lint: cold-file names are resolved here at compile time only, never on the VM hot path

namespace nift::rt {

bool lookup_native(std::string_view name, NativeInfo& info) {
    if (name == "print") { info = NativeInfo{NativeId::Print, 1}; return true; }
    return false;
}

bool native_print(const Value& value, std::string& out, std::string& error) {
    if (!append_rendered(value, out)) {
        error = "print: value is not directly renderable";
        return false;
    }
    out.push_back('\n');
    return true;
}

} // namespace nift::rt
