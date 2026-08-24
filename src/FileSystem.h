#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace filesystem {
std::string read_file(const std::filesystem::path& path);

// The read as the single authority: `nullopt` when the file is missing, not a
// regular file, or unreadable; `Some("")` for a valid empty file; `Some(...)`
// otherwise. Lets callers classify one read into the typed error semantics
// without separate exists/readable/open probes (performance-regression repair).
std::optional<std::string> read_file_checked(const std::filesystem::path& path);
void begin_recovery_epoch();
bool write_file(const std::filesystem::path& path, const std::string& contents);
bool write_readonly_file(const std::filesystem::path& path, const std::string& contents,
                         std::filesystem::perms mode = std::filesystem::perms::owner_read |
                                                         std::filesystem::perms::group_read |
                                                         std::filesystem::perms::others_read);
bool write_readonly_files(const std::vector<std::pair<std::filesystem::path, std::string>>& files,
                          std::filesystem::perms mode = std::filesystem::perms::owner_read |
                                                          std::filesystem::perms::group_read |
                                                          std::filesystem::perms::others_read);
std::filesystem::perms file_permissions(const std::filesystem::path& path);
bool remove_owned_file(const std::filesystem::path& path);
bool path_exists(const std::filesystem::path& path);
bool file_exists(const std::filesystem::path& path);
bool file_readable(const std::filesystem::path& path);
bool has_parent_component(std::string path);
bool valid_extension(const std::string& extension);
bool path_within(const std::filesystem::path& base, const std::filesystem::path& candidate);
std::string normalise_slashes(std::string path);
std::filesystem::file_time_type modified_time(const std::filesystem::path& path);
std::uint64_t hash_bytes(const std::string& contents);
std::uint64_t hash_path(const std::filesystem::path& path);
std::filesystem::path hash_file_path(const std::filesystem::path& root, const std::filesystem::path& path);
bool stored_hash_changed(const std::filesystem::path& root, const std::filesystem::path& path);
bool write_stored_hash(const std::filesystem::path& root, const std::filesystem::path& path);
#ifdef NIFT_TEST_RECOVERY_STATS
std::uint64_t recovery_scan_count_for_tests();
void reset_recovery_scan_count_for_tests();
#endif
}
