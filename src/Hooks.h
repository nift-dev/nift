#pragma once
#include "Types.h"
#include <filesystem>
#include <string>

// v4.4 build-hook execution. Project hooks live in `.nift/config.json`
// (`pre build`, `post build`, `pre build -all`, `post build -all`,
// `pre build --auto`, `post build --auto`, and the `--repair` variants);
// per-file hooks live in `.nift/tracked.json` under the same keys. Hook
// scripts are ordinary Nift `.f` files executed through the native Parser
// (never a subprocess). Matching is additive: the generic `pre build` key
// runs for every build mode, and the mode-specific keys (`-all`, `--auto`,
// `--repair`) run additionally only for that mode.
namespace nift_hooks {

// Effective hook mode derived from the build entry point.
inline std::string effective_mode(bool force, bool repair, const std::string& hint) {
    if (repair) return "repair";
    if (force) return "all";
    if (hint == "auto") return "auto";
    return "updated";
}

// Run the project-level hooks matching phase ("pre"/"post") and mode.
// Returns true on success; on failure `error` describes the failing hook.
bool run_project_hooks(const std::filesystem::path& root, const Config& config,
                       const std::string& phase, const std::string& mode, std::string& error);

// Run the per-file hooks for one tracked page matching phase and mode.
bool run_file_hooks(const std::filesystem::path& root, const TrackedInfo& info,
                    const std::string& phase, const std::string& mode, std::string& error);

}