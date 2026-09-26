#pragma once

#include "Json.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdlib.h>
#include <map>
#include <string>
#include <utility>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace nift_environment {

inline bool process_snapshot(json::Document& out, std::string& error) {
    (void)error;
    out = json::Document::make_object();
#if defined(_WIN32)
    char** current = ::_environ;
    std::map<std::string, std::pair<std::string, std::string>> values;
    if (!current) return true;
    for (; *current; ++current) {
        std::string entry(*current);
        const std::size_t eq = entry.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        std::string name = entry.substr(0, eq);
        std::string key = name;
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        values[key] = {std::move(name), entry.substr(eq + 1)};
    }
    for (const auto& kv : values) out[kv.second.first] = kv.second.second;
#else
    if (!::environ) return true;
    for (char** current = ::environ; *current; ++current) {
        std::string entry(*current);
        const std::size_t eq = entry.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        out[entry.substr(0, eq)] = entry.substr(eq + 1);
    }
#endif
    return true;
}

inline const char* host_os() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__) && defined(__MACH__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

inline const char* host_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__arm__) || defined(_M_ARM)
    return "arm";
#elif defined(__wasm32__)
    return "wasm32";
#elif defined(__wasm64__)
    return "wasm64";
#else
    return "unknown";
#endif
}

} // namespace nift_environment
