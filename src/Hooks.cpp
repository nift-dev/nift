#include "ProcessEnvironment.h"
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
                     const std::string& target, std::string& error, std::set<std::string>* dependencies = nullptr, std::map<std::string,std::string>* observations = nullptr, const std::set<std::string>* declared = nullptr) {
    fs::path p = fs::path(hook_path);
    if (p.is_relative()) p = root / p;
    p = fs::absolute(p).lexically_normal();
    if (!filesystem::path_exists(p)) { error = "build hook script does not exist: " + p.generic_string(); return false; }

    // Structured hook context via the process environment.
    {std::lock_guard<std::mutex> lock(nift_environment_mutex());
#ifdef _WIN32
    _putenv_s("NIFT_HOOK_PHASE", phase.c_str());
    _putenv_s("NIFT_HOOK_MODE", mode.c_str());
    if (target.empty()) _putenv_s("NIFT_HOOK_TARGET", "");
    else _putenv_s("NIFT_HOOK_TARGET", target.c_str());
#else
    ::setenv("NIFT_HOOK_PHASE", phase.c_str(), 1);
    ::setenv("NIFT_HOOK_MODE", mode.c_str(), 1);
    if (target.empty()) ::unsetenv("NIFT_HOOK_TARGET");
    else ::setenv("NIFT_HOOK_TARGET", target.c_str(), 1);
#endif

    }
    ScriptRenderHost host(root);
    TrackedInfo info;
    Parser parser(host, info);
    host.observe_dependencies = observations != nullptr;
    const auto bytes = filesystem::read_file(p);
    host.observe_dependency(p, bytes);
    auto rr = parser.run_script(bytes, p);
    if (!rr.ok) {
        error = "build hook '" + hook_path + "' failed: " + (rr.error.message.empty() ? "hook script failed" : rr.error.message);
        return false;
    }
    if (dependencies) {
        dependencies->insert(host.relative(p));
        dependencies->insert(rr.dependencies.begin(), rr.dependencies.end());
    }
    if (observations) {
        // Standalone hooks use their existing canonical relative identity;
        // preserve the owning consumer's declared aliases when recording bytes.
        std::map<std::string,std::vector<std::string>> aliases;
        if (declared) for (const auto& path : *declared)
            aliases[host.relative(root/path)].push_back(path);
        auto relevant = [&](const std::string& path) {
            return path == host.relative(p) || rr.dependencies.count(path) || aliases.count(path);
        };
        std::string ignored_conflict;
        for (const auto& [path, hash] : host.dependency_observations(ignored_conflict)) {
            if (!relevant(path)) continue;
            std::vector<std::string> names;
            if (path == host.relative(p) || rr.dependencies.count(path)) names.push_back(path);
            auto found = aliases.find(path);
            if (found != aliases.end()) names.insert(names.end(),found->second.begin(),found->second.end());
            for (const auto& name : names) {
                auto [it, inserted] = observations->emplace(name,hash);
                if (!inserted && it->second != hash) {
                    error = "hook dependency observed at multiple versions: " + name; return false;
                }
            }
        }
        for (const auto& path : host.dependency_conflicts()) if (relevant(path)) {
            error = "hook dependency observed at multiple versions: " + path; return false;
        }
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
                    const std::string& phase, const std::string& mode, std::string& error, std::set<std::string>* dependencies, std::map<std::string,std::string>* observations, const std::set<std::string>* declared) {
    if (info.build_hooks.empty()) return true;
    for (const auto& [key, path] : info.build_hooks) {
        if (key.rfind(phase + " ", 0) != 0) continue;
        if (!hook_matches(key, mode)) continue;
        if (!run_hook_script(root, path, phase, mode, info.name, error, dependencies, observations, declared)) return false;
    }
    return true;
}

} // namespace nift_hooks