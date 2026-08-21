#include "FileSystem.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cerrno>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <sys/stat.h>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
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

static bool temporary_owner_pid(const std::string& name, unsigned long long& pid) {
    constexpr const char* marker = ".nift-tmp-";
    const std::size_t marker_pos = name.rfind(marker);
    if (marker_pos == std::string::npos) return false;
    const std::size_t pid_begin = marker_pos + 10;
    const std::size_t pid_end = name.find('-', pid_begin);
    if (pid_end == std::string::npos || pid_end == pid_begin) return false;

    unsigned long long value = 0;
    for (std::size_t i = pid_begin; i < pid_end; ++i) {
        const unsigned char ch = static_cast<unsigned char>(name[i]);
        if (ch < '0' || ch > '9') return false;
        const unsigned digit = static_cast<unsigned>(ch - '0');
        if (value > (std::numeric_limits<unsigned long long>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    pid = value;
    return true;
}

static bool process_is_alive(unsigned long long pid) {
    if (pid == 0) return false;
#ifdef _WIN32
    if (pid > static_cast<unsigned long long>(std::numeric_limits<DWORD>::max())) return false;
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!process) return ::GetLastError() == ERROR_ACCESS_DENIED;
    DWORD exit_code = 0;
    const bool alive = ::GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
    ::CloseHandle(process);
    return alive;
#else
    if (pid > static_cast<unsigned long long>(std::numeric_limits<pid_t>::max())) return false;
    errno = 0;
    if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
    return errno == EPERM;
#endif
}

namespace {
std::atomic<std::uint64_t> recovery_epoch{0};
std::mutex recovery_mutex;
std::unordered_map<std::string, std::uint64_t> recovered_parent_epochs;
#ifdef NIFT_TEST_RECOVERY_STATS
std::atomic<std::uint64_t> recovery_scan_count{0};
#endif
}

void begin_recovery_epoch() {
    // Build passes are the recovery lifetime boundary. The expensive directory
    // sweep remains lazy (first write to each parent), but a later build pass in
    // the same long-running process gets another opportunity to recover a temp
    // left after the previous pass had already scanned that parent.
    recovery_epoch.fetch_add(1, std::memory_order_relaxed);
}

static void remove_stale_temporaries(const fs::path& path) {
    // Recovery once ran before every generated-file write, making a flat N-page
    // build O(N^2). Each parent is now scanned at most once per recovery epoch.
    // An epoch begins before each build pass; short-lived non-build commands use
    // their process-lifetime epoch. Temp names carry their owner PID so an
    // overlapping live Nift process is preserved conservatively.
    const fs::path parent = path.parent_path().empty() ? fs::path(".") : path.parent_path();
    const std::uint64_t epoch = recovery_epoch.load(std::memory_order_relaxed);

    // This micro-cache avoids path normalization, hashing and locking on the hot
    // repeated-write path. The epoch is part of the key so persistent threads
    // cannot accidentally turn once-per-build recovery back into once-per-process.
    struct RecentParent { fs::path path; std::uint64_t epoch = 0; };
    thread_local std::array<RecentParent, 4> recent_parents;
    thread_local std::size_t recent_count = 0;
    for (std::size_t i = 0; i < recent_count; ++i)
        if (recent_parents[i].epoch == epoch && recent_parents[i].path == parent) return;

    std::error_code key_error;
    fs::path key_path = fs::absolute(parent, key_error);
    if (key_error) key_path = parent;
    const std::string key = key_path.lexically_normal().string();

    {
        std::lock_guard<std::mutex> lock(recovery_mutex);
        const auto found = recovered_parent_epochs.find(key);
        if (found == recovered_parent_epochs.end() || found->second != epoch) {
            std::error_code error;
#ifdef NIFT_TEST_RECOVERY_STATS
            recovery_scan_count.fetch_add(1, std::memory_order_relaxed);
#endif
            for (const auto& entry : fs::directory_iterator(parent, error)) {
                if (error) break;
                const std::string name = entry.path().filename().string();
                unsigned long long owner_pid = 0;
                if (!temporary_owner_pid(name, owner_pid) || process_is_alive(owner_pid)) continue;
                std::error_code ignored;
                fs::remove(entry.path(), ignored);
            }
            if (error) return;
            recovered_parent_epochs[key] = epoch;
        }
    }

    RecentParent recent{parent, epoch};
    if (recent_count < recent_parents.size()) recent_parents[recent_count++] = std::move(recent);
    else {
        for (std::size_t i = 1; i < recent_parents.size(); ++i) recent_parents[i - 1] = std::move(recent_parents[i]);
        recent_parents.back() = std::move(recent);
    }
}

#ifdef NIFT_TEST_RECOVERY_STATS
std::uint64_t recovery_scan_count_for_tests() {
    return recovery_scan_count.load(std::memory_order_relaxed);
}

void reset_recovery_scan_count_for_tests() {
    recovery_scan_count.store(0, std::memory_order_relaxed);
}
#endif

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

static bool file_contents_equal(const fs::path& path, const std::string& contents) {
    std::error_code error;
    if (!fs::is_regular_file(path, error) || error) return false;
    const auto size = fs::file_size(path, error);
    if (error || size != contents.size()) return false;
    if (contents.empty()) return true;

    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    constexpr std::size_t chunk_size = 8192;
    std::array<char, chunk_size> buffer{};
    std::size_t offset = 0;
    while (offset < contents.size()) {
        const std::size_t wanted = std::min(chunk_size, contents.size() - offset);
        file.read(buffer.data(), static_cast<std::streamsize>(wanted));
        if (file.gcount() != static_cast<std::streamsize>(wanted)) return false;
        if (!std::equal(buffer.data(), buffer.data() + wanted, contents.data() + offset)) return false;
        offset += wanted;
    }
    return true;
}

static bool refresh_modified_time(const fs::path& path) {
    std::error_code error;
    fs::last_write_time(path, fs::file_time_type::clock::now(), error);
    return !error;
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


std::filesystem::perms file_permissions(const fs::path& path) {
    std::error_code error;
    return fs::status(path, error).permissions();
}

static bool apply_mode(const fs::path& path, fs::perms mode) {
#ifdef _WIN32
    std::error_code error;
    fs::permissions(path, mode, fs::perm_options::replace, error);
    return !error;
#else
    const auto bits = static_cast<unsigned>(mode & fs::perms::all);
    return ::chmod(path.c_str(), bits) == 0;
#endif
}

static bool is_readonly(const fs::path& path) {
    std::error_code error;
    const fs::perms permissions = fs::status(path, error).permissions();
    return !error && (permissions & fs::perms::owner_write) == fs::perms::none;
}

bool write_readonly_file(const fs::path& path, const std::string& contents, fs::perms mode) {
    ensure_parent_directory(path);
    remove_stale_temporaries(path);
    // A forced full build still renders and validates every page, but replacing
    // byte-identical files would pay an unnecessary temp-write + rename cost.
    // Refreshing the mtime preserves the build-current marker used by modified
    // mode while avoiding filesystem churn. Any content change still takes the
    // transactional path below and therefore retains last-good interruption
    // safety. If touching is unsupported, fall back to the transactional write.
    if (file_contents_equal(path, contents)) {
        if ((is_readonly(path) || apply_mode(path, mode)) && refresh_modified_time(path)) return true;
    }
    const fs::path temp = temporary_sibling(path);
    if (!write_temp_file(temp, contents)) {
        std::error_code cleanup;
        fs::remove(temp, cleanup);
        return false;
    }
    if (!apply_mode(temp, mode)) {
        std::error_code cleanup;
        fs::remove(temp, cleanup);
        return false;
    }
    return replace_file(temp, path);
}

bool write_readonly_files(const std::vector<std::pair<fs::path, std::string>>& files, fs::perms mode) {
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
        if (!write_temp_file(item.temp, contents) || !apply_mode(item.temp, mode)) {
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
