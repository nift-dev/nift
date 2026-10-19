#pragma once
// CP16 Host layer: one path resolution per Host call (design §2.2 `Resolved`,
// §3). Same rules as legacy's resolve_path (ParserExpression.cpp): `~`/`~/`
// expands to HOME (USERPROFILE on Windows) only in standalone scripts;
// relative paths join the base (cwd for scripts, the project root for pages);
// then absolute + lexically_normal (`..` removed lexically, Q-H6). Containment
// is opt-in policy (--fs-root / embedder), never a default (D-002).
#include "rt/host/host_error.h"

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace nift::rt::host {

struct Resolved {
    std::string spelled;             // argument as given
    std::filesystem::path path;      // absolute, lexically normal
    std::string trace_path() const;  // generic spelling used by both trace versions
};

struct PathPolicy {
    bool standalone = true;           // scripts: ~ expansion, cwd base
    std::filesystem::path base;       // empty: current_path() (scripts) at resolve time
    // Environment lookup for HOME/USERPROFILE (injectable for tests).
    std::function<const char*(const char*)> getenv;
    // Optional containment: return false and set `reason` (without the
    // built-in prefix) to refuse a path. Unset: everything is allowed.
    std::function<bool(const std::filesystem::path&, std::string& reason)> allow;
};

// Returns Ok and fills `out`, or Fatal (`runtime.invalid_operation`,
// "<builtin>: <reason>") when the policy refuses the path.
HostStatus resolve_path(const PathPolicy& policy, std::string_view raw, std::string_view builtin, Resolved& out, HostError& error);

} // namespace nift::rt::host
