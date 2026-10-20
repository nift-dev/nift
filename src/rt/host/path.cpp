#include "rt/host/path.h"

#include <cstdlib>

namespace nift::rt::host {

namespace fs = std::filesystem;

std::string Resolved::trace_path() const {
    // Resolution already produces absolute, normal paths; adopted ones are
    // spelled the same way here (legacy trace_path).
    if (path.is_absolute() && path == path.lexically_normal()) return path.generic_string();
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    return (ec ? path : absolute).lexically_normal().generic_string();
}

HostStatus resolve_path(const PathPolicy& policy, std::string_view raw, std::string_view builtin, Resolved& out,
                        HostError& error, PathSemantics semantics) {
    out.spelled = std::string(raw);
    out.open_path.clear();
    std::string expanded(raw);
    if (policy.standalone && !expanded.empty() && expanded[0] == '~' &&
        (expanded.size() == 1 || expanded[1] == '/' || expanded[1] == '\\')) {
        auto lookup = [&](const char* name) -> const char* {
            return policy.getenv ? policy.getenv(name) : std::getenv(name);
        };
        const char* home = lookup("HOME");
#ifdef _WIN32
        if (!home) home = lookup("USERPROFILE");
#endif
        if (home) expanded = std::string(home) + expanded.substr(1);
    }
    fs::path path(expanded);
    if (path.is_relative()) {
        const fs::path base = policy.base.empty() ? fs::current_path() : policy.base;
        path = base / path;
    }
    if (semantics == PathSemantics::Os) {
        out.open_path = fs::absolute(path);
        std::error_code ec;
        fs::path canonical = fs::weakly_canonical(out.open_path, ec);
        out.path = ec ? out.open_path : canonical;
    } else {
        out.path = fs::absolute(path).lexically_normal();
    }
    if (policy.allow) {
        std::string reason;
        if (!policy.allow(out.path, reason))
            return make_error(error, "runtime.invalid_operation", std::string(builtin) + ": " + reason);
    }
    return HostStatus::Ok;
}

} // namespace nift::rt::host
