#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace filesystem {
std::string read_file(const std::filesystem::path& path);
bool write_file(const std::filesystem::path& path, const std::string& contents);
bool write_readonly_file(const std::filesystem::path& path, const std::string& contents);
bool path_exists(const std::filesystem::path& path);
bool file_exists(const std::filesystem::path& path);
bool has_parent_component(std::string path);
std::string normalise_slashes(std::string path);
std::time_t modified_time(const std::filesystem::path& path);
std::uint32_t hash_bytes(const std::string& contents);
std::uint32_t hash_path(const std::filesystem::path& path);
std::filesystem::path hash_file_path(const std::filesystem::path& root, const std::filesystem::path& path);
bool stored_hash_changed(const std::filesystem::path& root, const std::filesystem::path& path);
bool write_stored_hash(const std::filesystem::path& root, const std::filesystem::path& path);
}
