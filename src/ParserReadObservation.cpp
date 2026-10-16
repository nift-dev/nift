#include "ReadObservation.h"
#include "ParserHelpers.h"
#include "ProcessEnvironment.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

#ifdef _WIN32
#include <stdlib.h>
#define NIFT_ENVIRON _environ
#else
extern char** environ;
#define NIFT_ENVIRON environ
#endif

namespace fs = std::filesystem;

namespace nift::observe {
namespace {

std::uint64_t fnv1a(std::uint64_t hash, const std::string& text) {
    for (unsigned char c : text) { hash ^= c; hash *= 1099511628211ull; }
    hash ^= 0xffu; hash *= 1099511628211ull;   // separator so ["ab","c"] != ["a","bc"]
    return hash;
}

std::string hex(std::uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(value));
    return buffer;
}

std::string hash_sorted(std::vector<std::string> items) {
    std::sort(items.begin(), items.end());
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& item : items) hash = fnv1a(hash, item);
    return hex(hash);
}

fs::path normal_absolute(const fs::path& path) {
    std::error_code error;
    fs::path absolute = fs::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

} // namespace

bool environment_name_is_volatile(const std::string& name) {
    // Set per invocation by the shell or by Nift itself, never by a user who
    // wants a page to change: the last command (_), working directory,
    // shell depth, and the NIFT_HOOK_* values build hooks receive.
    return name == "_" || name == "PWD" || name == "OLDPWD" || name == "SHLVL" ||
           name.rfind("NIFT_HOOK_", 0) == 0;
}

std::string spell(const fs::path& root, const fs::path& path) {
    const fs::path absolute = normal_absolute(path);
    const fs::path base = normal_absolute(root);
    const fs::path relative = absolute.lexically_relative(base);
    if (!relative.empty() && *relative.begin() != "..") return relative.generic_string();
    return absolute.generic_string();
}

fs::path resolve(const fs::path& root, const std::string& spelling) {
    const fs::path path(spelling);
    return path.is_absolute() ? path : (root / path).lexically_normal();
}

std::string directory_key(const fs::path& root, const fs::path& directory) {
    return "dir:" + spell(root, directory);
}

std::string glob_key(const fs::path& root, const fs::path& absolute_pattern) {
    return "glob:" + spell(root, absolute_pattern);
}

std::string listing_fingerprint(const fs::path& root, const std::string& key) {
    if (key.rfind("dir:", 0) == 0) {
        const fs::path directory = resolve(root, key.substr(4));
        std::error_code error;
        if (!fs::is_directory(directory, error)) return "missing";
        std::vector<std::string> names;
        for (fs::directory_iterator it(directory, error), end; !error && it != end; it.increment(error))
            names.push_back(it->path().filename().generic_string());
        if (error) return "unreadable";
        return hash_sorted(std::move(names));
    }
    if (key.rfind("glob:", 0) == 0) {
        std::vector<std::string> matches;
        for (const auto& match : nift::detail::glob_expand(resolve(root, key.substr(5))))
            matches.push_back(match.generic_string());
        return hash_sorted(std::move(matches));
    }
    return "unknown";
}

std::string environment_fingerprint() {
    std::vector<std::string> entries;
    std::lock_guard<std::mutex> lock(nift_environment_mutex());
    for (char** entry = NIFT_ENVIRON; entry && *entry; ++entry) {
        const std::string text(*entry);
        const std::string name = text.substr(0, text.find('='));
        if (environment_name_is_volatile(name)) continue;
        entries.push_back(text);
    }
    return hash_sorted(std::move(entries));
}

std::optional<std::string> environment_value(const std::string& name) {
    std::lock_guard<std::mutex> lock(nift_environment_mutex());
    const char* value = std::getenv(name.c_str());
    if (!value) return std::nullopt;
    return std::string(value);
}

} // namespace nift::observe
