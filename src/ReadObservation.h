#pragma once
// Dynamic-read observations shared by the renderer (which records them) and
// the incremental build engine (which re-checks them). Runtime 2.0 CP02.
#include <filesystem>
#include <optional>
#include <string>

namespace nift::observe {

// Spelling stored in page metadata: project-relative (generic form) when the
// path is inside `root`, otherwise absolute. `resolve` inverts it.
std::string spell(const std::filesystem::path& root, const std::filesystem::path& path);
std::filesystem::path resolve(const std::filesystem::path& root, const std::string& spelling);

// Listing keys: "dir:<spelling>" for a directory listing, "glob:<spelling>"
// for a glob pattern. The fingerprint is a stable hash of the sorted entry
// names (directory) or matched paths (glob); a missing directory hashes to
// "missing". Both sides call the same function, so they agree by construction.
std::string directory_key(const std::filesystem::path& root, const std::filesystem::path& directory);
std::string glob_key(const std::filesystem::path& root, const std::filesystem::path& absolute_pattern);
std::string listing_fingerprint(const std::filesystem::path& root, const std::string& key);

// Hash of the whole process environment (sorted NAME=VALUE entries).
std::string environment_fingerprint();

// Current value of one environment variable (nullopt when unset).
std::optional<std::string> environment_value(const std::string& name);

} // namespace nift::observe
