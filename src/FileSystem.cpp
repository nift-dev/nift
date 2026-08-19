#include "FileSystem.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <fstream>
#include <sstream>
#include <thread>
#include <sys/stat.h>
#include <unordered_set>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace filesystem {

std::string read_file(const fs::path& path) {
    std::error_code type_error;
    if (!fs::is_regular_file(path, type_error)) return {};
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

static void remove_stale_temporaries(const fs::path& path) {
    const fs::path parent = path.parent_path().empty() ? fs::path(".") : path.parent_path();
    const std::string prefix = path.filename().string() + ".nift-tmp-";
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(parent, error)) {
        if (error) break;
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
            std::error_code ignored;
            fs::remove(entry.path(), ignored);
        }
    }
}

static fs::path temporary_sibling(const fs::path& path) {
    static std::atomic<unsigned long long> counter{0};
#ifdef _WIN32
    const auto pid = static_cast<unsigned long long>(::GetCurrentProcessId());
#else
    const auto pid = static_cast<unsigned long long>(::getpid());
#endif
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    fs::path temp = path;
    temp += ".nift-tmp-" + std::to_string(pid) + "-" + std::to_string(id);
    return temp;
}

static bool replace_file(const fs::path& temp, const fs::path& path) {
#ifdef _WIN32
    if (fs::exists(path)) {
        std::error_code ignored;
        fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write |
                              fs::perms::group_read | fs::perms::others_read,
                        fs::perm_options::replace, ignored);
    }
    if (::MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    std::error_code cleanup;
    fs::remove(temp, cleanup);
    return false;
#else
    std::error_code error;
    fs::rename(temp, path, error);
    if (!error) return true;
    fs::remove(temp, error);
    return false;
#endif
}

static bool write_temp_file(const fs::path& temp, const std::string& contents) {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    file.close();
    return bool(file);
}

bool write_file(const fs::path& path, const std::string& contents) {
    ensure_parent_directory(path);
    remove_stale_temporaries(path);
    const fs::path temp = temporary_sibling(path);
    if (!write_temp_file(temp, contents)) {
        std::error_code cleanup;
        fs::remove(temp, cleanup);
        return false;
    }
    return replace_file(temp, path);
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
    remove_stale_temporaries(path);
    const fs::path temp = temporary_sibling(path);
    if (!write_temp_file(temp, contents)) {
        std::error_code cleanup;
        fs::remove(temp, cleanup);
        return false;
    }
    if (!make_readonly(temp)) {
        std::error_code cleanup;
        fs::remove(temp, cleanup);
        return false;
    }
    return replace_file(temp, path);
}

bool write_readonly_files(const std::vector<std::pair<fs::path, std::string>>& files) {
    if (files.empty()) return true;
    struct Staged { fs::path path; fs::path temp; fs::path backup; bool had_original = false; bool committed = false; };
    std::vector<Staged> staged;
    staged.reserve(files.size());

    auto cleanup = [&] {
        for (auto& item : staged) {
            std::error_code ignored;
            if (!item.temp.empty()) fs::remove(item.temp, ignored);
            ignored.clear();
            if (!item.backup.empty()) fs::remove(item.backup, ignored);
        }
    };

    // Stage every new file before replacing any existing output. This makes
    // write/permission failures all-or-none across a pagination set.
    for (const auto& [path, contents] : files) {
        ensure_parent_directory(path);
        remove_stale_temporaries(path);
        Staged item;
        item.path = path;
        item.temp = temporary_sibling(path);
        if (!write_temp_file(item.temp, contents) || !make_readonly(item.temp)) {
            staged.push_back(std::move(item));
            cleanup();
            return false;
        }
        std::error_code error;
        item.had_original = fs::exists(path, error) && !error;
        if (item.had_original) {
            item.backup = temporary_sibling(path);
            fs::copy_file(path, item.backup, fs::copy_options::overwrite_existing, error);
            if (error) { staged.push_back(std::move(item)); cleanup(); return false; }
        }
        staged.push_back(std::move(item));
    }

    for (std::size_t i = 0; i < staged.size(); ++i) {
        if (!replace_file(staged[i].temp, staged[i].path)) {
            // Best-effort rollback of outputs already replaced in this group.
            for (std::size_t j = 0; j < i; ++j) {
                if (!staged[j].committed) continue;
                if (staged[j].had_original) {
                    replace_file(staged[j].backup, staged[j].path);
                    staged[j].backup.clear();
                } else {
                    remove_owned_file(staged[j].path);
                }
            }
            cleanup();
            return false;
        }
        staged[i].temp.clear();
        staged[i].committed = true;
    }
    cleanup();
    return true;
}

bool remove_owned_file(const fs::path& path) {
    std::error_code error;
    const bool exists = fs::exists(path, error);
    if (error) return false;
    if (!exists) return true;

    // Generated output and metadata are deliberately read-only. POSIX permits
    // unlinking them from a writable directory, but Windows requires clearing
    // the read-only attribute before removal.
    fs::permissions(path, fs::perms::owner_write, fs::perm_options::add, error);
    if (error) return false;
    error.clear();
    return fs::remove(path, error) && !error;
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

bool file_readable(const fs::path& path) {
    std::error_code error;
    if (!fs::is_regular_file(path, error)) return false;
    std::ifstream file(path, std::ios::binary);
    return bool(file);
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

bool valid_extension(const std::string& extension) {
    return !extension.empty() && extension.front() == '.' &&
           extension.find('/') == std::string::npos &&
           extension.find('\\') == std::string::npos;
}

bool path_within(const fs::path& base, const fs::path& candidate) {
    const fs::path normalized_base = fs::absolute(base).lexically_normal();
    const fs::path normalized_candidate = fs::absolute(candidate).lexically_normal();

    // Reject obvious lexical escapes first.
    const fs::path lexical_relative = normalized_candidate.lexically_relative(normalized_base);
    if (lexical_relative.empty()) {
        if (normalized_candidate != normalized_base) return false;
    } else if (*lexical_relative.begin() == "..") {
        return false;
    }

    // Then resolve existing prefixes/symlinks. weakly_canonical also handles a
    // non-existent leaf while resolving any symlinked parent directories.
    std::error_code error;
    const fs::path canonical_base = fs::weakly_canonical(normalized_base, error);
    if (error) return false;
    const fs::path canonical_candidate = fs::weakly_canonical(normalized_candidate, error);
    if (error) return false;

    const fs::path canonical_relative = canonical_candidate.lexically_relative(canonical_base);
    if (canonical_relative.empty()) return canonical_candidate == canonical_base;
    return *canonical_relative.begin() != "..";
}

fs::file_time_type modified_time(const fs::path& path) {
    std::error_code error;
    const auto value = fs::last_write_time(path, error);
    return error ? fs::file_time_type::min() : value;
}

std::uint64_t hash_bytes(const std::string& contents) {
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : contents) { hash ^= c; hash *= 1099511628211ull; }
    return hash;
}

static std::uint64_t hash_directory(const fs::path& directory) {
    std::vector<fs::path> children;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(directory, error)) children.push_back(entry.path().filename());
    std::sort(children.begin(), children.end());

    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& name : children) {
        const std::string relative_name = name.generic_string();
        hash ^= hash_bytes(relative_name);
        hash *= 1099511628211ull;
        hash ^= hash_path(directory / name);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::uint64_t hash_path(const fs::path& path) {
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
    const auto stored_hash = static_cast<std::uint64_t>(std::strtoull(stored.c_str(), &end, 10));
    if (end == stored.c_str()) return true;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0') return true;
    return stored_hash != hash_path(path);
}

bool write_stored_hash(const fs::path& root, const fs::path& path) {
    return write_file(hash_file_path(root, path), std::to_string(hash_path(path)) + "\n");
}

} // namespace filesystem
