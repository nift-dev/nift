#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace filesystem {
std::string read_file(const std::filesystem::path& path);
bool write_file(const std::filesystem::path& path, const std::string& contents);
bool write_readonly_file(const std::filesystem::path& path, const std::string& contents);
bool write_readonly_files(const std::vector<std::pair<std::filesystem::path, std::string>>& files);
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
}
