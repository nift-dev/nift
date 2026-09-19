#include "Hooks.h"
#include "ScriptHost.h"
#include "Parser.h"
#include "FileSystem.h"
#include "Console.h"
#include <cstdlib>

namespace fs = std::filesystem;

namespace nift_hooks {

namespace {
bool hook_matches(const std::string& key, const std::string& mode) {
    // Generic keys run for every build mode.
    if (key == "pre build" || key == "post build") return true;
    if (mode == "all" && (key == "pre build -all" || key == "post build -all")) return true;
    if (mode == "auto" && (key == "pre build --auto" || key == "post build --auto")) return true;
    if (mode == "repair" && (key == "pre build --repair" || key == "post build --repair")) return true;
    return false;
}

bool run_hook_script(const fs::path& root, const std::string& hook_path,
                     const std::string& phase, const std::string& mode,
                     const std::string& target, std::string& error) {
    fs::path p = fs::path(hook_path);
    if (p.is_relative()) p = root / p;
    p = fs::absolute(p).lexically_normal();
    if (!filesystem::path_exists(p)) { error = "build hook script does not exist: " + p.generic_string(); return false; }

    // Structured hook context via the process environment.
    ::setenv("NIFT_HOOK_PHASE", phase.c_str(), 1);
    ::setenv("NIFT_HOOK_MODE", mode.c_str(), 1);
    if (target.empty()) ::unsetenv("NIFT_HOOK_TARGET");
    else ::setenv("NIFT_HOOK_TARGET", target.c_str(), 1);

    ScriptRenderHost host(root);
    TrackedInfo info;
    Parser parser(host, info);
    auto rr = parser.run_script(filesystem::read_file(p), p);
    if (!rr.ok) {
        error = "build hook '" + hook_path + "' failed: " + (rr.error.message.empty() ? "hook script failed" : rr.error.message);
        return false;
    }
    return true;
}
} // namespace

bool run_project_hooks(const fs::path& root, const Config& config,
                       const std::string& phase, const std::string& mode, std::string& error) {
    if (config.build_hooks.empty()) return true;
    for (const auto& [key, path] : config.build_hooks) {
        if (key.rfind(phase + " ", 0) != 0) continue;
        if (!hook_matches(key, mode)) continue;
        if (!run_hook_script(root, path, phase, mode, "", error)) return false;
    }
    return true;
}

bool run_file_hooks(const fs::path& root, const TrackedInfo& info,
                    const std::string& phase, const std::string& mode, std::string& error) {
    if (info.build_hooks.empty()) return true;
    for (const auto& [key, path] : info.build_hooks) {
        if (key.rfind(phase + " ", 0) != 0) continue;
        if (!hook_matches(key, mode)) continue;
        if (!run_hook_script(root, path, phase, mode, info.name, error)) return false;
    }
    return true;
}

} // namespace nift_hooks