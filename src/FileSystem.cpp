#include "FileSystem.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unordered_set>

namespace fs = std::filesystem;
namespace filesystem {

std::string read_file(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    const std::streamoff size = file.tellg();
    if (size <= 0) return {};
    std::string contents(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(contents.data(), size);
    if (!file && !file.eof()) return {};
    return contents;
}

static void ensure_parent_directory(const fs::path& path) {
    const fs::path parent = path.parent_path();
    if (parent.empty()) return;

    // Build workers tend to alternate between a very small number of output and
    // metadata directories. A tiny path cache avoids allocating/normalising a
    // string and hashing it for every generated file.
    thread_local std::array<fs::path, 4> recent_parents;
    thread_local std::size_t recent_count = 0;
    for (std::size_t i = 0; i < recent_count; ++i)
        if (recent_parents[i] == parent) return;

    std::error_code error;
    fs::create_directories(parent, error);
    if (!error) {
        if (recent_count < recent_parents.size()) recent_parents[recent_count++] = parent;
        else {
            for (std::size_t i = 1; i < recent_parents.size(); ++i) recent_parents[i - 1] = std::move(recent_parents[i]);
            recent_parents.back() = parent;
        }
    }
}

bool write_file(const fs::path& path, const std::string& contents) {
    ensure_parent_directory(path);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return bool(file);
}


static bool make_writable(const fs::path& path) {
#ifdef _WIN32
    std::error_code error;
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write |
                          fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace, error);
    return !error;
#else
    return ::chmod(path.c_str(), 0644) == 0;
#endif
}

static bool make_readonly(const fs::path& path) {
#ifdef _WIN32
    std::error_code error;
    fs::permissions(path, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read,
                    fs::perm_options::replace, error);
    return !error;
#else
    return ::chmod(path.c_str(), 0444) == 0;
#endif
}

bool write_readonly_file(const fs::path& path, const std::string& contents) {
    ensure_parent_directory(path);
    auto try_write = [&]() {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file) return false;
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        return bool(file);
    };

    if (!try_write()) {
        if (!make_writable(path) || !try_write()) return false;
    }

    return make_readonly(path);
}

bool path_exists(const fs::path& path) {
#ifdef _WIN32
    std::error_code error;
    return fs::exists(path, error);
#else
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
#endif
}

bool file_exists(const fs::path& path) {
#ifdef _WIN32
    std::error_code error;
    return fs::is_regular_file(path, error);
#else
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
}

std::string normalise_slashes(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

bool has_parent_component(std::string path) {
    path = normalise_slashes(path);
    std::stringstream stream(path);
    std::string component;
    while (std::getline(stream, component, '/')) if (component == "..") return true;
    return false;
}

std::time_t modified_time(const fs::path& path) {
    struct stat info {};
#ifdef _WIN32
    return ::stat(path.string().c_str(), &info) == 0 ? info.st_mtime : 0;
#else
    return ::stat(path.c_str(), &info) == 0 ? info.st_mtime : 0;
#endif
}

std::uint32_t hash_bytes(const std::string& contents) {
    std::uint32_t hash = 2166136261u;
    for (unsigned char c : contents) { hash ^= c; hash *= 16777619u; }
    return hash;
}

static std::uint32_t hash_directory(const fs::path& directory) {
    std::vector<fs::path> children;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(directory, error)) children.push_back(entry.path().filename());
    std::sort(children.begin(), children.end());

    std::uint32_t hash = 2166136261u;
    for (const auto& name : children) {
        const std::string relative_name = name.generic_string();
        hash ^= hash_bytes(relative_name);
        hash *= 16777619u;
        hash ^= hash_path(directory / name);
        hash *= 16777619u;
    }
    return hash;
}

std::uint32_t hash_path(const fs::path& path) {
    std::error_code error;
    if (fs::is_directory(path, error)) return hash_directory(path);
    return hash_bytes(read_file(path));
}

fs::path hash_file_path(const fs::path& root, const fs::path& path) {
    fs::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
    if (relative.empty()) relative = path.lexically_normal();
    return root / ".nift" / (relative.generic_string() + ".hash");
}

bool stored_hash_changed(const fs::path& root, const fs::path& path) {
    const fs::path hash_path_file = hash_file_path(root, path);
    if (!file_exists(hash_path_file)) return true;
    const std::string stored = read_file(hash_path_file);
    char* end = nullptr;
    const auto stored_hash = static_cast<std::uint32_t>(std::strtoul(stored.c_str(), &end, 10));
    if (end == stored.c_str()) return true;
    return stored_hash != hash_path(path);
}

bool write_stored_hash(const fs::path& root, const fs::path& path) {
    return write_file(hash_file_path(root, path), std::to_string(hash_path(path)) + "\n");
}

} // namespace filesystem
