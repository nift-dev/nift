#pragma once

#include "FileSystem.h"
#include "JsonFile.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace package_metadata {

struct Dependency {
    std::string source;
    std::string ref;
};

struct Manifest {
    bool has_identity = false;
    bool has_dependencies = false;
    std::string name;
    std::string version;
    std::string entry;
    bool has_description = false;
    std::string description;
    std::map<std::string, Dependency> dependencies;
};

struct LockEntry {
    std::string source;
    std::string requested;
    std::string commit;
};

using Lock = std::map<std::string, LockEntry>;

inline bool safe_external_text(const std::string& value) {
    if (value.empty() || value.front() == '-') return false;
    return std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
}

// Canonical source identity used for package acquisition and graph conflict
// detection. Reused by CLI package commands; do not create a second normalizer.
inline std::string git_source(const std::string& source){
    if(source.find('/')==std::string::npos && source.find(':')==std::string::npos && source.find('.')==std::string::npos) return "https://github.com/nift-packages/"+source+".git";
    if(source.rfind("github:",0)==0){std::string r=source.substr(7);return "https://github.com/"+r+(r.size()>4&&r.substr(r.size()-4)==".git"?"":".git");}
    if(source.find("://")!=std::string::npos||source.rfind("git@",0)==0)return source;
    return source;
}
inline bool source_is_local_path(const std::string& source){
    const std::filesystem::path path(source);
    if(path.is_absolute()||path.has_root_name()||source=="."||source==".."||source.rfind("./",0)==0||source.rfind("../",0)==0||
       source.rfind(".\\",0)==0||source.rfind("..\\",0)==0)return true;
    if(source.find("://")!=std::string::npos||source.rfind("git@",0)==0||source.rfind("github:",0)==0)return false;
    const auto colon=source.find(':');
    if(colon!=std::string::npos&&colon>0&&colon+1<source.size())return false;
    return source.find('/')!=std::string::npos||source.find('\\')!=std::string::npos;
}

inline bool portable_entry_component(const std::string& value) {
    if (value.empty() || value == "." || value == ".." ||
        !((value.front() >= 'a' && value.front() <= 'z') || (value.front() >= '0' && value.front() <= '9')) ||
        value.back() == '.') return false;
    for (unsigned char c : value)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    const std::string base = value.substr(0, value.find('.'));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul") return false;
    return !(base.size() == 4 && (base.rfind("com", 0) == 0 || base.rfind("lpt", 0) == 0) &&
             base[3] >= '1' && base[3] <= '9');
}

inline bool valid_semver_identifier(const std::string& value, bool prerelease) {
    if (value.empty()) return false;
    const bool numeric = std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
    if (prerelease && numeric && value.size() > 1 && value.front() == '0') return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-';
    });
}

inline bool valid_semver(const std::string& value) {
    if (value.empty()) return false;
    const auto plus = value.find('+');
    if (plus != std::string::npos && value.find('+', plus + 1) != std::string::npos) return false;
    const std::string before_build = value.substr(0, plus);
    const std::string build = plus == std::string::npos ? std::string() : value.substr(plus + 1);
    const auto dash = before_build.find('-');
    const std::string core = before_build.substr(0, dash);
    const std::string prerelease = dash == std::string::npos ? std::string() : before_build.substr(dash + 1);

    std::size_t start = 0;
    int core_parts = 0;
    while (start <= core.size()) {
        const auto end = core.find('.', start);
        const std::string part = core.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || (part.size() > 1 && part.front() == '0') ||
            !std::all_of(part.begin(), part.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) return false;
        ++core_parts;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (core_parts != 3) return false;

    auto validate_suffix = [](const std::string& suffix, bool pre) {
        if (suffix.empty()) return false;
        std::size_t begin = 0;
        while (begin <= suffix.size()) {
            const auto end = suffix.find('.', begin);
            if (!valid_semver_identifier(suffix.substr(begin, end == std::string::npos ? std::string::npos : end - begin), pre)) return false;
            if (end == std::string::npos) break;
            begin = end + 1;
        }
        return true;
    };
    if (dash != std::string::npos && !validate_suffix(prerelease, true)) return false;
    if (plus != std::string::npos && !validate_suffix(build, false)) return false;
    return true;
}

inline std::vector<std::string> split_identifiers(const std::string& value) {
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find('.', begin);
        result.push_back(value.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

inline int compare_semver(const std::string& left, const std::string& right) {
    auto without_build = [](const std::string& value) { return value.substr(0, value.find('+')); };
    const std::string a = without_build(left), b = without_build(right);
    const auto adash = a.find('-'), bdash = b.find('-');
    const auto acore = split_identifiers(a.substr(0, adash));
    const auto bcore = split_identifiers(b.substr(0, bdash));
    for (std::size_t i = 0; i < 3; ++i) {
        if (acore[i].size() != bcore[i].size()) return acore[i].size() < bcore[i].size() ? -1 : 1;
        if (acore[i] != bcore[i]) return acore[i] < bcore[i] ? -1 : 1;
    }
    if (adash == std::string::npos || bdash == std::string::npos) {
        if (adash == bdash) return 0;
        return adash == std::string::npos ? 1 : -1;
    }
    const auto apre = split_identifiers(a.substr(adash + 1));
    const auto bpre = split_identifiers(b.substr(bdash + 1));
    for (std::size_t i = 0; i < std::min(apre.size(), bpre.size()); ++i) {
        const bool an = std::all_of(apre[i].begin(), apre[i].end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
        const bool bn = std::all_of(bpre[i].begin(), bpre[i].end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
        if (an && bn) {
            if (apre[i].size() != bpre[i].size()) return apre[i].size() < bpre[i].size() ? -1 : 1;
            if (apre[i] != bpre[i]) return apre[i] < bpre[i] ? -1 : 1;
        } else if (an != bn) return an ? -1 : 1;
        else if (apre[i] != bpre[i]) return apre[i] < bpre[i] ? -1 : 1;
    }
    if (apre.size() == bpre.size()) return 0;
    return apre.size() < bpre.size() ? -1 : 1;
}

inline bool exact_fields(const json::Document& value, const std::set<std::string>& allowed,
                         const std::string& context, std::string& error) {
    if (!value.is_object()) { error = context + " must be an object"; return false; }
    for (const auto& field : value.object) {
        if (!allowed.count(field.first)) {
            error = context + " has unknown field '" + field.first + "'";
            return false;
        }
    }
    return true;
}

inline bool parse_dependencies(const json::Document& value, std::map<std::string, Dependency>& out,
                               std::string& error) {
    if (!value.is_object()) { error = "manifest dependencies must be an object"; return false; }
    for (const auto& item : value.object) {
        if (!filesystem::valid_package_name(item.first)) { error = "invalid dependency package name: " + item.first; return false; }
        if (!exact_fields(item.second, {"source", "ref"}, "dependency '" + item.first + "'", error)) return false;
        if (!item.second.has("source") || !item.second["source"].is_string() || !safe_external_text(item.second["source"].string)) {
            error = "dependency '" + item.first + "' requires a non-empty safe string source"; return false;
        }
        if (!item.second.has("ref") || !item.second["ref"].is_string() || !safe_external_text(item.second["ref"].string)) {
            error = "dependency '" + item.first + "' requires a non-empty safe string ref"; return false;
        }
        out.emplace(item.first, Dependency{item.second["source"].string, item.second["ref"].string});
    }
    return true;
}

inline bool parse_manifest(const json::Document& document, bool require_package_identity,
                           Manifest& out, std::string& error) {
    if (!exact_fields(document, {"name", "version", "entry", "description", "dependencies"}, "manifest.json", error)) return false;
    const bool any_identity = document.has("name") || document.has("version") || document.has("entry") || document.has("description");
    const bool complete_identity = document.has("name") && document.has("version") && document.has("entry");
    if (require_package_identity && !complete_identity) { error = "package manifest requires name, version, and entry"; return false; }
    if (any_identity && !complete_identity) { error = "package identity requires name, version, and entry together"; return false; }
    if (!complete_identity && !document.has("dependencies")) { error = "project manifest requires dependencies"; return false; }
    if (complete_identity) {
        if (!document["name"].is_string() || !filesystem::valid_package_name(document["name"].string)) { error = "manifest name is invalid"; return false; }
        if (!document["version"].is_string() || !valid_semver(document["version"].string)) { error = "manifest version must be semantic versioning"; return false; }
        if (!document["entry"].is_string() || !safe_external_text(document["entry"].string)) { error = "manifest entry must be a non-empty portable string"; return false; }
        const std::filesystem::path entry(document["entry"].string);
        const std::string filename = document["entry"].string.substr(document["entry"].string.find_last_of('/') + 1);
        bool portable_components = true; std::size_t component_start = 0;
        while (component_start <= document["entry"].string.size()) {
            const auto component_end = document["entry"].string.find('/', component_start);
            if (!portable_entry_component(document["entry"].string.substr(component_start, component_end == std::string::npos ? std::string::npos : component_end - component_start))) { portable_components = false; break; }
            if (component_end == std::string::npos) break;
            component_start = component_end + 1;
        }
        if (document["entry"].string.find('\\') != std::string::npos || document["entry"].string.find(':') != std::string::npos ||
            document["entry"].string.find("//") != std::string::npos || document["entry"].string.rfind("./",0)==0 ||
            document["entry"].string.find("/./") != std::string::npos || filename.empty() || filename.front()=='.' ||
            !portable_components || entry.is_absolute() || entry.has_root_name() || filesystem::has_parent_component(document["entry"].string) || entry.extension() != ".f") {
            error = "manifest entry must be a contained relative .f path"; return false;
        }
        out.has_identity = true;
        out.name = document["name"].string;
        out.version = document["version"].string;
        out.entry = document["entry"].string;
        if (document.has("description")) {
            if (!document["description"].is_string()) { error = "manifest description must be a string"; return false; }
            out.has_description = true;
            out.description = document["description"].string;
        }
    }
    if (document.has("dependencies")) {
        out.has_dependencies = true;
        if (!parse_dependencies(document["dependencies"], out.dependencies, error)) return false;
    }
    return true;
}

inline bool load_manifest(const std::filesystem::path& path, bool require_package_identity,
                          Manifest& out, std::string& error) {
    json::Document document;
    if (!load_json_file(path, document, error)) { error = path.filename().string() + ": " + error; return false; }
    return parse_manifest(document, require_package_identity, out, error);
}

inline bool load_package(const std::filesystem::path& root, Manifest& out,
                         std::filesystem::path& entry, std::string& error) {
    if (!load_manifest(root/"manifest.json", true, out, error)) return false;
    entry = (root/out.entry).lexically_normal();
    if (!filesystem::path_within(root, entry)) { error = "package entry escapes the package directory: " + out.entry; return false; }
    if (!filesystem::file_exists(entry)) { error = "package entry does not exist: " + out.entry; return false; }
    return true;
}

inline json::Document manifest_document(const Manifest& manifest) {
    json::Document result = json::Document::make_object();
    if (manifest.has_identity) {
        result["name"] = json::Document(manifest.name);
        result["version"] = json::Document(manifest.version);
        result["entry"] = json::Document(manifest.entry);
        if (manifest.has_description) result["description"] = json::Document(manifest.description);
    }
    if (manifest.has_dependencies || !manifest.has_identity || !manifest.dependencies.empty()) {
        result["dependencies"] = json::Document::make_object();
        for (const auto& item : manifest.dependencies) {
            json::Document dependency = json::Document::make_object();
            dependency["source"] = json::Document(item.second.source);
            dependency["ref"] = json::Document(item.second.ref);
            result["dependencies"][item.first] = dependency;
        }
    }
    return result;
}

inline bool exact_commit(const std::string& value) {
    if (value.size() != 40 && value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

inline bool parse_lock(const json::Document& document, Lock& out, std::string& error) {
    if (!document.is_object()) { error = "package lock must be an object"; return false; }
    for (const auto& item : document.object) {
        if (!filesystem::valid_package_name(item.first)) { error = "invalid locked package name: " + item.first; return false; }
        if (!exact_fields(item.second, {"source", "requested", "commit"}, "lock entry '" + item.first + "'", error)) return false;
        for (const std::string field : {"source", "requested", "commit"}) {
            if (!item.second.has(field) || !item.second[field].is_string() || !safe_external_text(item.second[field].string)) {
                error = "lock entry '" + item.first + "' requires a non-empty safe string " + field; return false;
            }
        }
        const std::string commit = item.second["commit"].string;
        if (commit != "local" && !exact_commit(commit)) { error = "lock entry '" + item.first + "' has an invalid exact commit"; return false; }
        out.emplace(item.first, LockEntry{item.second["source"].string, item.second["requested"].string, commit});
    }
    return true;
}

inline bool load_lock(const std::filesystem::path& path, Lock& out, bool& exists, std::string& error) {
    exists = filesystem::path_exists(path);
    if (!exists) return true;
    json::Document document;
    if (!load_json_file(path, document, error)) { error = "packages.lock.json: " + error; return false; }
    return parse_lock(document, out, error);
}

inline bool validate_lock(const Manifest& manifest, const Lock& lock, std::string& error) {
    if (lock.size() != manifest.dependencies.size()) { error = "package lock does not match manifest dependencies"; return false; }
    for (const auto& item : manifest.dependencies) {
        const auto found = lock.find(item.first);
        if (found == lock.end()) { error = "package lock is missing dependency: " + item.first; return false; }
        if (found->second.source != item.second.source || found->second.requested != item.second.ref) {
            error = "package lock is inconsistent for dependency: " + item.first; return false;
        }
        const bool local = item.second.ref == "local";
        if (local != (found->second.commit == "local")) {
            error = "package lock local/exact revision mismatch for dependency: " + item.first; return false;
        }
    }
    return true;
}

inline json::Document lock_document(const Lock& lock) {
    json::Document result = json::Document::make_object();
    for (const auto& item : lock) {
        json::Document entry = json::Document::make_object();
        entry["source"] = json::Document(item.second.source);
        entry["requested"] = json::Document(item.second.requested);
        entry["commit"] = json::Document(item.second.commit);
        result[item.first] = entry;
    }
    return result;
}

}  // namespace package_metadata
