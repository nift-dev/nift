#include "WatchList.h"
#include "Console.h"
#include "FileSystem.h"
#include "JsonFile.h"
#include "ProjectInfo.h"

#include <algorithm>
#include <set>

namespace fs = std::filesystem;

namespace {
bool require_string(const json::Document& object, const std::string& key, std::string& value) {
    if (!object.is_object() || !object.has(key) || !object[key].is_string()) return false;
    value = object[key].string;
    return true;
}
}

bool WatchList::load(const fs::path& project_root) {
    directories.clear();
    const fs::path watched_path = project_root / ".nift/.watch/watched.json";
    if (!filesystem::path_exists(watched_path)) return true;

    json::Document watched;
    std::string parse_error;
    if (!load_json_file(watched_path, watched, parse_error) || !watched.is_object() || !watched.has("watched") || !watched["watched"].is_array()) {
        console::error(console::path(watched_path.generic_string(), true) + ": invalid watch state" + (parse_error.empty() ? "" : " (" + parse_error + ")"));
        return false;
    }

    for (const auto& watched_value : watched["watched"].array) {
        if (!watched_value.is_string()) {
            console::error(console::path(watched_path.generic_string(), true) + ": 'watched' array contains a non-string value");
            return false;
        }

        WatchDirectory directory;
        directory.path = watched_value.string;
        const fs::path extension_path = project_root / ".nift/.watch" / fs::path(directory.path) / "exts.json";
        json::Document extensions;
        parse_error.clear();
        if (!load_json_file(extension_path, extensions, parse_error) || !extensions.is_object() || !extensions.has("exts") || !extensions["exts"].is_array()) {
            console::error(console::path(extension_path.generic_string(), true) + ": invalid watch extension configuration" + (parse_error.empty() ? "" : " (" + parse_error + ")"));
            return false;
        }

        std::set<std::string> seen_extensions;
        for (const auto& value : extensions["exts"].array) {
            WatchExtension extension;
            if (!value.is_object() || !require_string(value, "content-ext", extension.content_ext) ||
                !require_string(value, "template", extension.template_path) ||
                !require_string(value, "output-ext", extension.output_ext)) {
                console::error(console::path(extension_path.generic_string(), true) + ": each 'exts' member must be an object with string content-ext/template/output-ext fields");
                return false;
            }
            if (!seen_extensions.insert(extension.content_ext).second) {
                console::error(console::path(extension_path.generic_string(), true) + ": duplicate watched content extension '" + extension.content_ext + "'");
                return false;
            }
            directory.extensions.push_back(extension);
        }
        directories.push_back(directory);
    }
    return true;
}

bool WatchList::save(const fs::path& project_root) const {
    json::Document watched = json::Document::make_object();
    watched["watched"] = json::Document::make_array();
    for (const auto& directory : directories) watched["watched"].push_back(directory.path);
    if (!save_json_file(project_root / ".nift/.watch/watched.json", watched)) return false;

    for (const auto& directory : directories) {
        json::Document extension_document = json::Document::make_object();
        extension_document["exts"] = json::Document::make_array();
        for (const auto& extension : directory.extensions) {
            json::Document value = json::Document::make_object();
            value["content-ext"] = extension.content_ext;
            value["template"] = extension.template_path;
            value["output-ext"] = extension.output_ext;
            extension_document["exts"].push_back(value);
        }

        const fs::path base = project_root / ".nift/.watch" / fs::path(directory.path);
        if (!save_json_file(base / "exts.json", extension_document)) return false;
        if (!filesystem::path_exists(base / "tracked.json")) {
            json::Document tracked = json::Document::make_object();
            tracked["tracked"] = json::Document::make_array();
            if (!save_json_file(base / "tracked.json", tracked)) return false;
        }
    }
    return true;
}

bool WatchList::reconcile(ProjectInfo& project) {
    bool tracking_changed = false;
    for (const auto& directory : directories) {
        const fs::path watched_directory = project.root / directory.path;
        if (!filesystem::path_exists(watched_directory)) continue;

        std::set<std::string> seen_names;
        for (const auto& extension : directory.extensions) {
            std::error_code error;
            for (const auto& entry : fs::recursive_directory_iterator(watched_directory, error)) {
                if (!entry.is_regular_file() || entry.path().extension() != extension.content_ext) continue;
                std::string relative = fs::relative(entry.path(), project.root / project.config.content_dir, error).generic_string();
                if (error || relative.size() < extension.content_ext.size()) continue;
                relative.resize(relative.size() - extension.content_ext.size());
                seen_names.insert(relative);
                if (!project.find(relative)) {
                    project.tracked.push_back({relative, fs::path(relative).filename().string(), extension.template_path, extension.content_ext, extension.output_ext});
                    project.invalidate_tracked_index();
                    tracking_changed = true;
                }
            }
        }

        const fs::path state_path = project.root / ".nift/.watch" / fs::path(directory.path) / "tracked.json";
        json::Document previous;
        std::string parse_error;
        if (filesystem::path_exists(state_path)) {
            if (!load_json_file(state_path, previous, parse_error) || !previous.is_object() || !previous.has("tracked") || !previous["tracked"].is_array()) {
                console::error(console::path(state_path.generic_string(), true) + ": invalid tracked watch state");
                return false;
            }
            for (const auto& value : previous["tracked"].array) {
                if (!value.is_string()) {
                    console::error(console::path(state_path.generic_string(), true) + ": 'tracked' must contain strings");
                    return false;
                }
                if (!seen_names.count(value.string)) {
                    auto it = std::find_if(project.tracked.begin(), project.tracked.end(), [&](const TrackedInfo& info) { return info.name == value.string; });
                    if (it != project.tracked.end()) {
                        std::error_code error;
                        fs::remove(project.output_path(*it), error);
                        project.tracked.erase(it);
                        project.invalidate_tracked_index();
                        tracking_changed = true;
                    }
                }
            }
        }

        json::Document current = json::Document::make_object();
        current["tracked"] = json::Document::make_array();
        for (const auto& name : seen_names) current["tracked"].push_back(name);
        if (!save_json_file(state_path, current)) return false;
    }

    return !tracking_changed || project.save_tracking();
}

bool WatchList::add(ProjectInfo& project, std::string directory, const WatchExtension& extension) {
    directory = filesystem::normalise_slashes(directory);
    if (filesystem::has_parent_component(directory)) {
        console::error("watch directory cannot contain a '..' path component");
        return false;
    }
    if (!directory.empty() && directory.back() != '/') directory += '/';

    auto existing = std::find_if(directories.begin(), directories.end(), [&](const WatchDirectory& value) { return value.path == directory; });
    if (existing == directories.end()) {
        directories.push_back({directory, {extension}});
    } else {
        for (const auto& prior : existing->extensions) {
            if (prior.content_ext == extension.content_ext) {
                console::error("watch directory already has an entry for content extension '" + extension.content_ext + "'");
                return false;
            }
        }
        existing->extensions.push_back(extension);
    }
    return save(project.root);
}

bool WatchList::remove(ProjectInfo& project, std::string directory) {
    directory = filesystem::normalise_slashes(directory);
    if (!directory.empty() && directory.back() != '/') directory += '/';
    directories.erase(std::remove_if(directories.begin(), directories.end(), [&](const WatchDirectory& value) { return value.path == directory; }), directories.end());
    return save(project.root);
}
