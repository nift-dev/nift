#include "rt/module_loader.h"

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>
#include <system_error>

// rt-lint: cold-file compile-time module loading, once per module

namespace nift::rt {

ModuleSource load_module_source(const std::filesystem::path& path) {
    ModuleSource out;
    std::error_code ec;
    std::ifstream in;
    if (std::filesystem::is_regular_file(path, ec)) in.open(path, std::ios::binary);
    if (!in) {
        out.message = "script is not readable: " + path.generic_string();
        return out;
    }
    std::ostringstream text;
    text << in.rdbuf();
    out.text = text.str();
    out.found = true;
    return out;
}

std::filesystem::path module_identity(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path normal = std::filesystem::absolute(path).lexically_normal();
    std::filesystem::path identity = std::filesystem::weakly_canonical(normal, ec);
    if (ec) identity = normal;
#ifdef _WIN32
    auto native = identity.native();
    std::transform(native.begin(), native.end(), native.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    identity = std::filesystem::path(std::move(native));
#endif
    return identity;
}

} // namespace nift::rt
