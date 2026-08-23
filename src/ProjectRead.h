#pragma once
#include "Types.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

// The single implementation of Nift project-read semantics, shared by the CLI
// (ProjectInfo) and the SSR layer (ProjectState).
//
// Everything here is pure: the functions read only their arguments, touch the
// filesystem only through reads (no writes, ever), and report failures through
// error strings rather than printing. Build/write/watch/hash responsibilities
// deliberately stay above this layer, in ProjectInfo.
//
// PA1b convergence: ProjectInfo::load_config/load_tracking and all path
// geometry delegate here, so there is exactly one implementation of config
// parsing/validation, tracking parsing/validation, tracked-name rules and path
// geometry rather than two copies kept in agreement by tests.
namespace project_read {

// Path geometry. These are the exact geometry the CLI applies; both the
// snapshot loader (validation of derived paths) and the public accessors use
// them, so geometry is defined once.
std::string mapped_name(const TrackedInfo& info);
std::filesystem::path content_path_of(const std::filesystem::path& root, const Config& config,
                                      const TrackedInfo& info);
std::filesystem::path output_path_of(const std::filesystem::path& root, const Config& config,
                                     const TrackedInfo& info);
std::filesystem::path pagination_output_path_of(const std::filesystem::path& root, const Config& config,
                                                const TrackedInfo& info, std::size_t page);
std::string relative_of(const std::filesystem::path& root, const std::filesystem::path& path);

// Validation predicates for config/tracking names.
bool valid_contract_name(const std::string& name);
bool reserved_contract_name(const std::string& name);
bool valid_tracked_name(const std::string& name);

// Pure config/tracking parsing and validation. On failure returns false with a
// non-empty error (message text matches the CLI's console output, without the
// ".nift/config.json: " path prefix the CLI adds).
bool load_config(const std::filesystem::path& root, Config& config, std::string& error);
bool load_tracking(const std::filesystem::path& root, const Config& config,
                   std::vector<TrackedInfo>& tracked, std::string& error);

} // namespace project_read
