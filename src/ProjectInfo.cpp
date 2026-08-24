#include "ProjectInfo.h"
#include "ProjectInfoHost.h"
#include "ProjectRead.h"
#include <minify/Minify.h>
#include "BuildProgress.h"
#include "Console.h"
#include "FileSystem.h"
#include "JsonFile.h"
#include "Parser.h"
#include "WatchList.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <iostream>
#include <map>
#include <thread>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
// Keep ordinary builds informative without flooding the terminal on large sites.
constexpr std::size_t detailed_build_output_limit = 10;
constexpr std::size_t summary_path_sample_limit = 5;

fs::path find_project_root(fs::path path = fs::current_path()) {
    std::error_code error;
    path = fs::absolute(path, error);
    while (true) {
        if (filesystem::path_exists(path / ".nift/config.json")) return path;
        const fs::path parent = path.parent_path();
        if (parent == path) return {};
        path = parent;
    }
}
}

ProjectInfo::ProjectInfo() : watch_(new WatchList) {}
ProjectInfo::~ProjectInfo() { delete watch_; }
WatchList& ProjectInfo::watch_list() { return *watch_; }

bool ProjectInfo::open() {
    root = find_project_root();
    if (root.empty()) {
        console::error("not inside a Nift project");
        return false;
    }
    return load_config() && load_tracking() && watch_->load(root);
}

bool ProjectInfo::load_config() {
    const fs::path path = root / ".nift/config.json";
    std::string error;
    if (!project_read::load_config(root, config, error)) {
        console::error(console::path(relative(path), true) + ": " + error);
        return false;
    }
    return true;
}

bool ProjectInfo::load_tracking() {
    const fs::path path = root / ".nift/tracked.json";
    std::string error;
    if (!project_read::load_tracking(root, config, tracked, error)) {
        console::error(console::path(relative(path), true) + ": " + error);
        tracked.clear();
        return false;
    }
    rebuild_tracked_index();
    return true;
}


bool ProjectInfo::save_tracking() const {
    rebuild_tracked_index();
    json::Document document = json::Document::make_object();
    document["tracked"] = json::Document::make_array();
    for (const auto& info : tracked) {
        json::Document entry = json::Document::make_object();
        entry["name"] = info.name;
        entry["title"] = info.title;
        if (!info.template_path.empty()) entry["template"] = info.template_path;
        if (!info.content_ext.empty()) entry["content-ext"] = info.content_ext;
        if (!info.output_ext.empty()) entry["output-ext"] = info.output_ext;
        if (info.minify.has_value()) entry["minify"] = *info.minify;
        if (info.paginate.has_value()) {
            json::Document paginate = json::Document::make_object();
            paginate["items-per-page"] = static_cast<double>(info.paginate->items_per_page);
            if (info.paginate->template_path.has_value()) paginate["template"] = *info.paginate->template_path;
            if (info.paginate->separator_path.has_value()) paginate["separator"] = *info.paginate->separator_path;
            entry["paginate"] = paginate;
        }
        document["tracked"].push_back(entry);
    }
    return save_json_file(root / ".nift/tracked.json", document);
}

void ProjectInfo::rebuild_tracked_index() const {
    tracked_index_.clear();
    tracked_index_.reserve(tracked.size());
    for (std::size_t i = 0; i < tracked.size(); ++i) tracked_index_[tracked[i].name] = i;
    {
        std::lock_guard<std::mutex> lock(tracked_output_index_mutex_);
        tracked_output_index_.clear();
        tracked_output_index_valid_ = false;
    }
    tracked_index_size_ = tracked.size();
}

bool ProjectInfo::is_tracked_output(const fs::path& path) const {
    if (tracked_index_size_ != tracked.size()) rebuild_tracked_index();
    std::lock_guard<std::mutex> lock(tracked_output_index_mutex_);
    if (!tracked_output_index_valid_) {
        tracked_output_index_.clear();
        tracked_output_index_.reserve(tracked.size());
        for (const auto& info : tracked)
            tracked_output_index_.insert(output_path(info).lexically_normal().generic_string());
        tracked_output_index_valid_ = true;
    }
    return tracked_output_index_.count(path.lexically_normal().generic_string()) != 0;
}

TrackedInfo* ProjectInfo::find(const std::string& name) {
    if (tracked_index_size_ != tracked.size()) rebuild_tracked_index();
    const auto it = tracked_index_.find(name);
    return it == tracked_index_.end() ? nullptr : &tracked[it->second];
}

const TrackedInfo* ProjectInfo::find(const std::string& name) const {
    if (tracked_index_size_ != tracked.size()) rebuild_tracked_index();
    const auto it = tracked_index_.find(name);
    return it == tracked_index_.end() ? nullptr : &tracked[it->second];
}

bool ProjectInfo::conflicts_with_tracked_path(const TrackedInfo& candidate, const std::string& ignored_name) const {
    const fs::path candidate_content = content_path(candidate).lexically_normal();
    const fs::path candidate_output = output_path(candidate).lexically_normal();
    for (const auto& existing : tracked) {
        if (!ignored_name.empty() && existing.name == ignored_name) continue;
        if (content_path(existing).lexically_normal() == candidate_content ||
            output_path(existing).lexically_normal() == candidate_output)
            return true;
    }
    return false;
}

void ProjectInfo::invalidate_tracked_index() {
    tracked_index_size_ = static_cast<std::size_t>(-1);
}


fs::path ProjectInfo::content_path(const TrackedInfo& info) const {
    return project_read::content_path_of(root, config, info);
}

fs::path ProjectInfo::output_path(const TrackedInfo& info) const {
    return project_read::output_path_of(root, config, info);
}

fs::path ProjectInfo::pagination_output_path(const TrackedInfo& info, std::size_t page) const {
    return project_read::pagination_output_path_of(root, config, info, page);
}

fs::path ProjectInfo::info_path(const TrackedInfo& info) const {
    const fs::path output = output_path(info).lexically_normal();
    fs::path relative_output_path = output.lexically_relative(root.lexically_normal());
    if (relative_output_path.empty()) relative_output_path = output;
    std::string relative_output = relative_output_path.generic_string();
    const std::string extension = info.output_ext.empty() ? config.output_ext : info.output_ext;
    if (relative_output.size() >= extension.size() &&
        relative_output.compare(relative_output.size() - extension.size(), extension.size(), extension) == 0)
        relative_output.resize(relative_output.size() - extension.size());
    return root / ".nift" / (relative_output + ".info.json");
}

std::string ProjectInfo::relative(const fs::path& path) const {
    return project_read::relative_of(root, path);
}

const std::string* ProjectInfo::read_shared_source(const fs::path& path) const {
    const std::string key = path.lexically_normal().generic_string();
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        const auto it = shared_source_cache_.find(key);
        if (it != shared_source_cache_.end()) return it->second.get();
    }

    auto contents = std::make_unique<const std::string>(filesystem::read_file(path));
    const std::string* result = contents.get();
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        auto [it, inserted] = shared_source_cache_.emplace(key, std::move(contents));
        if (!inserted) result = it->second.get();
    }
    return result;
}

std::shared_ptr<const json::Document> ProjectInfo::read_shared_json(const fs::path& path, std::string& error) const {
    const fs::path normalized = fs::absolute(path).lexically_normal();
    const std::string key = normalized.generic_string();

    std::lock_guard<std::mutex> lock(json_cache_mutex_);
    const auto existing = shared_json_cache_.find(key);
    if (existing != shared_json_cache_.end()) return existing->second;

    if (!filesystem::path_exists(normalized)) {
        error = "JSON file does not exist";
        return {};
    }
    if (!filesystem::file_readable(normalized)) {
        error = "JSON file is not readable";
        return {};
    }

    const std::string source = filesystem::read_file(normalized);
    auto document = std::make_shared<json::Document>();
    if (!json::Document::parse(source, *document, error)) return {};

    std::shared_ptr<const json::Document> immutable = document;
    shared_json_cache_.emplace(key, immutable);
    return immutable;
}


bool ProjectInfo::load_user_dependencies(const TrackedInfo& info, std::set<std::string>& dependencies, BuildError* build_error) const {
    fs::path path = content_path(info);
    path.replace_extension(".deps.json");
    if (!filesystem::path_exists(path)) return true;

    json::Document document;
    std::string error;
    if (!load_json_file(path, document, error) || !document.is_object() || !document.has("dependencies") || !document["dependencies"].is_array()) {
        if (build_error) *build_error = {info.name, path, 0, "invalid user dependencies JSON" + (error.empty() ? "" : ": " + error)};
        return false;
    }

    dependencies.insert(relative(path));
    for (const auto& value : document["dependencies"].array) {
        if (!value.is_string()) {
            if (build_error) *build_error = {info.name, path, 0, "'dependencies' array contains a non-string member"};
            return false;
        }
        const fs::path dependency_name(value.string);
        if (dependency_name.is_absolute() || filesystem::has_parent_component(value.string)) {
            if (build_error) *build_error = {info.name, path, 0, "dependency must be a project-relative path: " + value.string};
            return false;
        }
        if (!filesystem::path_exists(root / dependency_name)) {
            if (build_error) *build_error = {info.name, path, 0, "dependency does not exist: " + value.string};
            return false;
        }
        dependencies.insert(value.string);
    }
    return true;
}

bool ProjectInfo::hash_changed_cached(const fs::path& dependency) const {
    const std::string key = dependency.lexically_normal().generic_string();
    {
        std::lock_guard<std::mutex> lock(hash_mutex_);
        const auto it = hash_change_cache_.find(key);
        if (it != hash_change_cache_.end()) return it->second;
    }

    const bool changed = filesystem::stored_hash_changed(root, dependency);
    {
        std::lock_guard<std::mutex> lock(hash_mutex_);
        // Keep this cache deliberately small. Its job is to retain hot shared
        // dependencies (templates/partials), not every one-off content file.
        constexpr std::size_t max_cached_hash_results = 512;
        if (hash_change_cache_.size() < max_cached_hash_results)
            hash_change_cache_.emplace(key, changed);
    }
    return changed;
}

void ProjectInfo::reset_build_caches() {
    {
        std::lock_guard<std::mutex> lock(hash_mutex_);
        hash_change_cache_.clear();
        refreshed_hashes_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        shared_source_cache_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(json_cache_mutex_);
        shared_json_cache_.clear();
    }
}

void ProjectInfo::refresh_hash_once(const fs::path& dependency) {
    const fs::path normalized = dependency.lexically_normal();
    const fs::path content_root = (root / config.content_dir).lexically_normal();
    const fs::path relative_to_content = normalized.lexically_relative(content_root);

    // Content files are normally unique to one tracked page, so remembering all
    // 10,000 of them merely to prove they are unique wastes memory. Shared
    // templates, partials and explicit dependencies still use the dedupe set.
    const bool is_content_file = !relative_to_content.empty() &&
                                 *relative_to_content.begin() != "..";
    if (!is_content_file) {
        const std::string key = normalized.generic_string();
        std::lock_guard<std::mutex> lock(hash_mutex_);
        if (!refreshed_hashes_.insert(key).second) return;
    }
    filesystem::write_stored_hash(root, normalized);
}

bool ProjectInfo::metadata_path_is_safe(const fs::path& path) const {
    const fs::path normalized_root = root.lexically_normal();
    const fs::path normalized = path.lexically_normal();
    const fs::path lexical = normalized.lexically_relative(normalized_root);
    if (lexical.empty()) {
        if (normalized != normalized_root) return false;
    } else if (*lexical.begin() == "..") {
        return false;
    }

    const fs::path parent = normalized.parent_path();
    const std::string parent_key = parent.generic_string();
    bool parent_safe = false;
    {
        std::lock_guard<std::mutex> lock(metadata_path_mutex_);
        const auto it = metadata_parent_safety_cache_.find(parent_key);
        if (it != metadata_parent_safety_cache_.end()) parent_safe = it->second;
        else {
            parent_safe = filesystem::path_within(root, parent);
            metadata_parent_safety_cache_.emplace(parent_key, parent_safe);
        }
    }
    if (!parent_safe) return false;

    // A safe parent plus a non-symlink leaf cannot escape the project. Only a
    // symlink leaf needs the more expensive canonical containment check.
    std::error_code error;
    const fs::file_status status = fs::symlink_status(normalized, error);
    if (error && error != std::errc::no_such_file_or_directory) return false;
    if (!error && fs::is_symlink(status)) return filesystem::path_within(root, normalized);
    return true;
}

bool ProjectInfo::dependency_changed(const fs::path& dependency, fs::file_time_type page_info_mtime) const {
    if (!filesystem::path_exists(dependency)) return true;
    if (config.incremental_mode == "modified")
        return filesystem::modified_time(dependency) > page_info_mtime;
    if (config.incremental_mode == "hash")
        return hash_changed_cached(dependency);
    return filesystem::modified_time(dependency) > page_info_mtime ||
           hash_changed_cached(dependency);
}

std::vector<std::string> ProjectInfo::build_reasons(const TrackedInfo& info) const {
    std::vector<std::string> reasons;
    const fs::path output = output_path(info);
    const fs::path page_info = info_path(info);

    if (!filesystem::path_exists(output)) reasons.push_back("generated output is missing");
    if (!filesystem::path_exists(page_info)) {
        reasons.push_back("page build metadata is missing");
        return reasons;
    }

    json::Document document;
    std::string error;
    if (!load_json_file(page_info, document, error) || !document.is_object() ||
        !document.has("dependencies") || !document["dependencies"].is_array() ||
        !document.has("reqs") || !document["reqs"].is_array() ||
        !document.has("name") || !document["name"].is_string() ||
        !document.has("title") || !document["title"].is_string() ||
        !document.has("template") || !document["template"].is_string() ||
        !document.has("content") || !document["content"].is_string() ||
        !document.has("output") || !document["output"].is_string() ||
        !document.has("minify") || !document["minify"].is_bool() ||
        !document.has("minify-version") || !document["minify-version"].is_number() ||
        !document.has("pagination") || !document["pagination"].is_bool() ||
        !document.has("pagination-items-per-page") || !document["pagination-items-per-page"].is_number() ||
        !document.has("pagination-template") || !document["pagination-template"].is_string() ||
        !document.has("pagination-separator") || !document["pagination-separator"].is_string() ||
        !document.has("pagination-pages") || !document["pagination-pages"].is_number()) {
        reasons.push_back("page build metadata is invalid or from an older metadata format");
        return reasons;
    }

    if (document["name"].string != info.name) reasons.push_back("tracked name changed");
    if (document["title"].string != info.title) reasons.push_back("tracked title changed");
    if (document["template"].string != info.template_path) reasons.push_back("tracked template changed");
    if (document["content"].string != relative(content_path(info))) reasons.push_back("tracked content path changed");
    if (document["output"].string != relative(output_path(info))) reasons.push_back("tracked output path changed");

    const bool current_paginate = info.paginate.has_value();
    if (document["pagination"].boolean != current_paginate) reasons.push_back("pagination setting changed");
    const double stored_pages_value = document["pagination-pages"].num;
    const bool stored_pages_valid = std::isfinite(stored_pages_value) && std::floor(stored_pages_value) == stored_pages_value && stored_pages_value >= 0;
    const std::size_t stored_pages = stored_pages_valid ? static_cast<std::size_t>(stored_pages_value) : 0;
    if (!stored_pages_valid) reasons.push_back("page build metadata has invalid pagination page count");
    if (current_paginate) {
        const fs::path current_content = content_path(info);
        const fs::path effective_template = info.paginate->template_path.has_value()
            ? (root / *info.paginate->template_path).lexically_normal()
            : current_content.parent_path() / (current_content.stem().generic_string() + ".paginate.html");
        std::string effective_separator;
        if (info.paginate->separator_path.has_value()) effective_separator = relative((root / *info.paginate->separator_path).lexically_normal());
        else {
            const fs::path candidate = current_content.parent_path() / (current_content.stem().generic_string() + ".separator.html");
            if (filesystem::path_exists(candidate)) effective_separator = relative(candidate);
        }
        if (!std::isfinite(document["pagination-items-per-page"].num) ||
            std::floor(document["pagination-items-per-page"].num) != document["pagination-items-per-page"].num ||
            document["pagination-items-per-page"].num != static_cast<double>(info.paginate->items_per_page))
            reasons.push_back("pagination items-per-page changed");
        if (document["pagination-template"].string != relative(effective_template)) reasons.push_back("pagination template changed");
        if (document["pagination-separator"].string != effective_separator) reasons.push_back("pagination separator changed");
        for (std::size_t page = 2; page <= stored_pages; ++page) {
            if (!filesystem::path_exists(pagination_output_path(info, page)))
                reasons.push_back("generated pagination output is missing: " + relative(pagination_output_path(info, page)));
        }
    } else if (document["pagination-items-per-page"].num != 0 ||
               !document["pagination-template"].string.empty() ||
               !document["pagination-separator"].string.empty() || stored_pages != 0) {
        reasons.push_back("pagination setting changed");
    }

    std::string current_output_extension = info.output_ext.empty() ? config.output_ext : info.output_ext;
    std::transform(current_output_extension.begin(), current_output_extension.end(), current_output_extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool current_minify = info.minify.has_value()
        ? *info.minify
        : config.minify_exts.count(current_output_extension) != 0;
    if (document["minify"].boolean != current_minify) reasons.push_back("minification setting changed");
    const fs::file_time_type page_info_mtime = filesystem::modified_time(page_info);
    const int expected_minify_version = current_minify ? minify::format_version : 0;
    if (!std::isfinite(document["minify-version"].num) ||
        std::floor(document["minify-version"].num) != document["minify-version"].num ||
        document["minify-version"].num != expected_minify_version)
        reasons.push_back("minifier version changed");

    for (const auto& value : document["dependencies"].array) {
        if (!value.is_string()) {
            reasons.push_back("page build metadata has an invalid dependency");
            continue;
        }

        const fs::path dependency = (root / value.string).lexically_normal();
        if (!metadata_path_is_safe(dependency)) {
            reasons.push_back("page build metadata has an invalid dependency");
            continue;
        }
        if (!filesystem::path_exists(dependency))
            reasons.push_back("dependency removed: " + value.string);
        else if (dependency_changed(dependency, page_info_mtime))
            reasons.push_back("dependency changed: " + value.string);
    }

    for (const auto& value : document["reqs"].array) {
        if (!value.is_string()) {
            reasons.push_back("page build metadata has an invalid requirement");
            continue;
        }
        const fs::path requirement = (root / value.string).lexically_normal();
        if (!metadata_path_is_safe(requirement)) {
            reasons.push_back("page build metadata has an invalid requirement");
            continue;
        }
        // A requirement produced by another currently tracked item is a checked
        // project relationship, not an existence check on that producer's
        // current artifact. The producer owns its own build state: if its output
        // is missing it will be selected independently, and if its build fails
        // the overall invocation fails without making otherwise-valid referrers
        // stale. Concrete/untracked requirements still rebuild the referrer when
        // their path disappears.
        if (!filesystem::path_exists(requirement) && !is_tracked_output(requirement))
            reasons.push_back("required path missing: " + value.string);
    }

    // A user .deps.json file can itself be added or become invalid between builds.
    // Its dependencies are also present in page-info after a successful build, but
    // loading it here catches sidecar metadata changes before parsing starts.
    std::set<std::string> user_dependencies;
    if (!load_user_dependencies(info, user_dependencies, nullptr)) {
        reasons.push_back("user dependency metadata is invalid");
    } else {
        for (const auto& dependency_name : user_dependencies) {
            const std::string reason = "dependency changed: " + dependency_name;
            const std::string removed = "dependency removed: " + dependency_name;
            if (std::find(reasons.begin(), reasons.end(), reason) != reasons.end() ||
                std::find(reasons.begin(), reasons.end(), removed) != reasons.end()) continue;

            const fs::path dependency = root / dependency_name;
            if (!filesystem::path_exists(dependency))
                reasons.push_back(removed);
            else if (dependency_changed(dependency, page_info_mtime))
                reasons.push_back(reason);
        }
    }

    return reasons;
}

bool ProjectInfo::needs_build(const TrackedInfo& info, std::string* reason) const {
    const auto reasons = build_reasons(info);
    if (reasons.empty()) return false;
    if (reason) *reason = reasons.front();
    return true;
}

void ProjectInfo::print_build_error(const BuildError& error) const {
    std::lock_guard<std::mutex> lock(console::output_mutex);
    std::cerr << console::error_label() << " while building " << console::path(error.tracked_name, true) << '\n';
    if (!error.source_file.empty()) {
        std::cerr << "  " << console::path(relative(error.source_file), true);
        if (error.line) {
            std::cerr << ':' << error.line;
            if (error.column) std::cerr << ':' << error.column;
        }
        std::cerr << '\n';
    }
    std::cerr << "  " << console::highlight_diagnostic_message(error.message) << '\n';
    if (!error.source_line.empty()) {
        const std::size_t byte_start = error.column ? std::min(error.column - 1, error.source_line.size()) : 0;
        const std::size_t byte_length = std::min(error.source_length, error.source_line.size() - byte_start);
        const auto source_prefix = std::string_view(error.source_line).substr(0, byte_start);
        const auto source_span = std::string_view(error.source_line).substr(byte_start, byte_length);
        const std::size_t display_start = console::display_width(source_prefix);
        const std::size_t display_length = std::max<std::size_t>(1, console::display_width(source_span));
        const std::string expanded_prefix = console::expand_tabs(source_prefix);
        const std::string expanded_span = console::expand_tabs(source_span);
        const std::string expanded = console::expand_tabs(error.source_line);
        std::cerr << "    " << console::highlight_nift_source(
            expanded, expanded_prefix.size(), expanded_span.size()) << '\n';
        if (error.column) {
            const std::string marker = "^" + std::string(display_length > 1 ? display_length - 1 : 0, '~');
            std::cerr << "    " << std::string(display_start, ' ')
                      << console::diagnostic_offender(marker) << '\n';
        }
    }
}

bool ProjectInfo::write_page_info(const TrackedInfo& info, const std::set<std::string>& dependencies, const std::set<std::string>& reqs, std::size_t pagination_pages) const {
    const std::string content_name = relative(content_path(info));
    const std::string output_name = relative(output_path(info));
    std::string output_extension = info.output_ext.empty() ? config.output_ext : info.output_ext;
    std::transform(output_extension.begin(), output_extension.end(), output_extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool effective_minify = info.minify.has_value()
        ? *info.minify
        : config.minify_exts.count(output_extension) != 0;
    std::string pagination_template;
    std::string pagination_separator;
    std::size_t pagination_items_per_page = 0;
    if (info.paginate.has_value()) {
        pagination_items_per_page = info.paginate->items_per_page;
        const fs::path content = content_path(info);
        const fs::path template_path = info.paginate->template_path.has_value()
            ? (root / *info.paginate->template_path).lexically_normal()
            : content.parent_path() / (content.stem().generic_string() + ".paginate.html");
        pagination_template = relative(template_path);
        if (info.paginate->separator_path.has_value()) {
            pagination_separator = relative((root / *info.paginate->separator_path).lexically_normal());
        } else {
            const fs::path candidate = content.parent_path() / (content.stem().generic_string() + ".separator.html");
            if (filesystem::path_exists(candidate)) pagination_separator = relative(candidate);
        }
    }
    std::size_t estimated_size = 240 + info.name.size() + info.title.size() + info.template_path.size() + content_name.size() + output_name.size() + pagination_template.size() + pagination_separator.size();
    for (const auto& dependency : dependencies) estimated_size += dependency.size() + 10;
    for (const auto& requirement : reqs) estimated_size += requirement.size() + 10;

    std::string output;
    output.reserve(estimated_size);
    output += "{\n  \"name\": \"";
    json::Document::append_escaped_string(output, info.name);
    output += "\",\n  \"title\": \"";
    json::Document::append_escaped_string(output, info.title);
    output += "\",\n  \"template\": \"";
    json::Document::append_escaped_string(output, info.template_path);
    output += "\",\n  \"content\": \"";
    json::Document::append_escaped_string(output, content_name);
    output += "\",\n  \"output\": \"";
    json::Document::append_escaped_string(output, output_name);
    output += "\",\n  \"minify\": ";
    output += effective_minify ? "true" : "false";
    output += ",\n  \"minify-version\": ";
    output += std::to_string(effective_minify ? minify::format_version : 0);
    output += ",\n  \"pagination\": ";
    output += info.paginate.has_value() ? "true" : "false";
    output += ",\n  \"pagination-items-per-page\": ";
    output += std::to_string(pagination_items_per_page);
    output += ",\n  \"pagination-template\": \"";
    json::Document::append_escaped_string(output, pagination_template);
    output += "\",\n  \"pagination-separator\": \"";
    json::Document::append_escaped_string(output, pagination_separator);
    output += "\",\n  \"pagination-pages\": ";
    output += std::to_string(pagination_pages);
    output += ",\n  \"dependencies\": [";

    if (!dependencies.empty()) output.push_back('\n');
    std::size_t index = 0;
    for (const auto& dependency : dependencies) {
        output += "    \"";
        json::Document::append_escaped_string(output, dependency);
        output.push_back('\"');
        if (++index != dependencies.size()) output.push_back(',');
        output.push_back('\n');
    }
    if (!dependencies.empty()) output += "  ";
    output += "],\n  \"reqs\": [";
    if (!reqs.empty()) output.push_back('\n');
    index = 0;
    for (const auto& requirement : reqs) {
        output += "    \"";
        json::Document::append_escaped_string(output, requirement);
        output.push_back('\"');
        if (++index != reqs.size()) output.push_back(',');
        output.push_back('\n');
    }
    if (!reqs.empty()) output += "  ";
    output += "]\n}\n";

    return filesystem::write_readonly_file(info_path(info), output);
}

bool ProjectInfo::build_one(TrackedInfo& info) {
    // Only paginated pages consult the previous page-count metadata; reading it
    // for every page was a redundant happy-path filesystem probe
    // (performance-regression repair).
    std::size_t previous_pagination_pages = 0;
    if (info.paginate.has_value()) {
        const fs::path previous_info_path = info_path(info);
        if (filesystem::path_exists(previous_info_path)) {
            json::Document previous; std::string previous_error;
            if (load_json_file(previous_info_path, previous, previous_error) && previous.is_object() &&
                previous.has("pagination-pages") && previous["pagination-pages"].is_number() &&
                std::isfinite(previous["pagination-pages"].num) && previous["pagination-pages"].num >= 0 &&
                std::floor(previous["pagination-pages"].num) == previous["pagination-pages"].num) {
                previous_pagination_pages = static_cast<std::size_t>(previous["pagination-pages"].num);
            }
        }
    }
    const fs::path content = content_path(info);
    if (!filesystem::path_exists(content)) {
        print_build_error({info.name, content, 0, "content file does not exist"});
        return false;
    }

    ProjectInfoHost host(*this);
    Parser parser(host, info);
    RenderResult result = parser.render();
    if (!result.ok) { print_build_error(result.error); return false; }
    if (!load_user_dependencies(info, result.dependencies, &result.error)) { print_build_error(result.error); return false; }

    const fs::path output = output_path(info);
    const std::string extension = info.output_ext.empty() ? config.output_ext : info.output_ext;
    std::string normalized_extension = extension;
    std::transform(normalized_extension.begin(), normalized_extension.end(), normalized_extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool should_minify = info.minify.has_value()
        ? *info.minify
        : config.minify_exts.count(normalized_extension) != 0;
    if (should_minify) {
        minify::Format format;
        if (!minify::format_for_extension(extension, format)) {
            print_build_error({info.name, output, 0, "no minifier is available for output extension " + extension});
            return false;
        }
        auto minify_output = [&](std::string& page_output) {
            std::string minified, minify_error;
            if (!minify::run(format, page_output, minified, minify_error)) {
                print_build_error({info.name, output, 0, "minification failed" +
                                   (minify_error.empty() ? std::string() : ": " + minify_error)});
                return false;
            }
            page_output = std::move(minified);
            return true;
        };
        if (!result.pagination_outputs.empty()) {
            for (auto& page_output : result.pagination_outputs) if (!minify_output(page_output)) return false;
            result.output = result.pagination_outputs.front();
        } else if (!minify_output(result.output)) return false;
    }

    // Outputs deterministically preserve the source content file's permissions
    // (an executable script stays executable; a normal content file keeps its
    // ordinary mode), falling back to read-only when the source mode cannot be
    // read. `info`-metadata writes keep the default read-only mode.
    fs::perms output_mode = filesystem::file_permissions(content_path(info));
    if (output_mode == fs::perms::unknown || output_mode == fs::perms::none)
        output_mode = fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read;

    if (!result.pagination_outputs.empty()) {
        std::vector<std::pair<fs::path, std::string>> page_files;
        page_files.reserve(result.pagination_outputs.size());
        for (std::size_t page = 1; page <= result.pagination_outputs.size(); ++page)
            page_files.emplace_back(pagination_output_path(info, page), result.pagination_outputs[page - 1]);
        if (!filesystem::write_readonly_files(page_files, output_mode)) {
            print_build_error({info.name, output, 0, "failed to commit generated pagination outputs"});
            return false;
        }
    } else if (!filesystem::write_readonly_file(output, result.output, output_mode)) {
        print_build_error({info.name, output, 0, "failed to write generated output"});
        return false;
    }

    if (config.incremental_mode != "modified") {
        for (const auto& dependency : result.dependencies) {
            const fs::path dependency_path = root / dependency;
            if (filesystem::path_exists(dependency_path)) refresh_hash_once(dependency_path);
        }
    }

    const std::size_t new_pagination_pages = result.pagination_outputs.size();
    const std::size_t keep_pages = std::max<std::size_t>(1, new_pagination_pages);
    for (std::size_t page = keep_pages + 1; page <= previous_pagination_pages; ++page) {
        const fs::path stale = pagination_output_path(info, page);
        if (!filesystem::remove_owned_file(stale)) {
            print_build_error({info.name, stale, 0, "failed to remove stale pagination output"});
            return false;
        }
    }

    if (!write_page_info(info, result.dependencies, result.reqs, new_pagination_pages)) {
        print_build_error({info.name, info_path(info), 0, "failed to write page build metadata"});
        return false;
    }

    return true;
}

int ProjectInfo::build_many(const std::vector<BuildJob>& jobs, bool targeted, bool full_detail, std::size_t requested_count) {
    if (jobs.empty()) {
        std::lock_guard<std::mutex> lock(console::output_mutex);
        if (targeted) return 1;
        std::cout << console::good("✓") << ' ' << requested_count << " tracked "
                  << (requested_count == 1 ? "page is" : "pages are") << " up to date\n";
        return 0;
    }

    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    std::size_t thread_count = config.build_threads < 0 ? static_cast<std::size_t>(-static_cast<long long>(config.build_threads)) * hardware : (config.build_threads == 0 ? hardware : static_cast<std::size_t>(config.build_threads));
    thread_count = std::max<std::size_t>(1, std::min(thread_count, jobs.size()));

    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> completed{0};
    std::vector<unsigned char> succeeded(jobs.size(), 0);
    BuildProgress progress(jobs.size(), completed);

    auto worker = [&] {
        while (true) {
            const std::size_t index = next.fetch_add(1);
            if (index >= jobs.size()) break;
            succeeded[index] = build_one(*jobs[index].info) ? 1 : 0;
            ++completed;
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) workers.emplace_back(worker);
    for (auto& worker_thread : workers) worker_thread.join();

    std::size_t successful_count = 0;
    for (unsigned char success : succeeded) if (success) ++successful_count;
    const std::size_t missing_requested = targeted && requested_count > jobs.size() ? requested_count - jobs.size() : 0;
    const std::size_t failed_count = jobs.size() - successful_count + missing_requested;

    std::lock_guard<std::mutex> lock(console::output_mutex);

    bool has_rebuild_reasons = false;
    for (const auto& job : jobs) {
        if (!job.reasons.empty()) { has_rebuild_reasons = true; break; }
    }

    const bool detailed = full_detail || (has_rebuild_reasons && jobs.size() <= detailed_build_output_limit);
    if (detailed) {
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            if (!succeeded[i]) continue;
            std::cout << console::good("built") << ' ' << console::path(jobs[i].info->name) << '\n';
            for (const auto& reason : jobs[i].reasons)
                std::cout << console::dim("    ↳ " + reason) << '\n';
        }
    } else if (has_rebuild_reasons) {
        std::map<std::string, std::size_t> reason_counts;
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            if (!succeeded[i]) continue;
            for (const auto& reason : jobs[i].reasons) ++reason_counts[reason];
        }

        if (!reason_counts.empty()) {
            std::cout << console::dim("rebuild causes:") << '\n';
            for (const auto& [reason, count] : reason_counts)
                std::cout << "  " << console::dim("↳ " + reason + " → " + std::to_string(count) + (count == 1 ? " page" : " pages")) << '\n';
        }

        std::cout << console::dim("affected pages: ");
        std::size_t shown = 0;
        for (std::size_t i = 0; i < jobs.size() && shown < summary_path_sample_limit; ++i) {
            if (!succeeded[i]) continue;
            if (shown) std::cout << console::dim(", ");
            std::cout << console::path(jobs[i].info->name);
            ++shown;
        }
        if (successful_count > shown) std::cout << console::dim("  +" + std::to_string(successful_count - shown) + " more");
        std::cout << '\n';
    }

    if (targeted) {
        // Keep the historical wording because scripts and the regression suite rely on it.
        if (failed_count == 0) std::cout << console::good("📦") << " all " << successful_count << " specified files built successfully\n";
        else std::cout << successful_count << " of " << requested_count << " specified files built successfully\n";
    } else if (failed_count == 0) {
        const bool incremental = !jobs.empty() && !jobs.front().reasons.empty();
        std::cout << console::good("📦") << ' ' << successful_count << ' '
                  << (successful_count == 1 ? "page " : "pages ")
                  << (incremental ? "rebuilt" : "built") << " successfully\n";
    } else {
        std::cout << successful_count << " of " << jobs.size() << " pages built successfully\n";
    }

    return failed_count == 0 ? 0 : 1;
}

int ProjectInfo::build_all(bool force, bool explain) {
    filesystem::begin_recovery_epoch();
    if (!reconcile_watch()) return 1;
    reset_build_caches();

    std::vector<BuildJob> jobs;
    jobs.reserve(tracked.size());
    if (force || tracked.size() < 2) {
        for (auto& info : tracked) {
            if (force) jobs.push_back({&info, {}});
            else {
                auto reasons = build_reasons(info);
                if (!reasons.empty()) jobs.push_back({&info, std::move(reasons)});
            }
        }
    } else {
        const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
        std::size_t thread_count = config.build_threads < 0 ? static_cast<std::size_t>(-static_cast<long long>(config.build_threads)) * hardware : (config.build_threads == 0 ? hardware : static_cast<std::size_t>(config.build_threads));
        thread_count = std::max<std::size_t>(1, std::min(thread_count, tracked.size()));

        std::vector<std::vector<std::string>> reasons(tracked.size());
        std::atomic<std::size_t> next{0};
        std::vector<std::thread> workers;
        workers.reserve(thread_count);
        for (std::size_t t = 0; t < thread_count; ++t) {
            workers.emplace_back([&] {
                while (true) {
                    const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
                    if (index >= tracked.size()) break;
                    reasons[index] = build_reasons(tracked[index]);
                }
            });
        }
        for (auto& worker : workers) worker.join();
        for (std::size_t i = 0; i < tracked.size(); ++i)
            if (!reasons[i].empty()) jobs.push_back({&tracked[i], std::move(reasons[i])});
    }

    return build_many(jobs, false, explain, tracked.size());
}

int ProjectInfo::build_names(const std::vector<std::string>& names, bool, bool explain) {
    filesystem::begin_recovery_epoch();
    if (!reconcile_watch()) return 1;
    reset_build_caches();
    std::unordered_set<std::string> seen_names;
    for (const auto& name : names) {
        if (!seen_names.insert(name).second) {
            console::error("duplicate tracked name requested for build: '" + name + "'");
            return 1;
        }
    }
    std::vector<BuildJob> jobs;
    for (const auto& name : names) {
        if (TrackedInfo* info = find(name)) jobs.push_back({info, {}});
        else {
            std::lock_guard<std::mutex> lock(console::output_mutex);
            std::cout << "not tracking:\n " << name << '\n';
        }
    }
    if (jobs.empty()) return 1;
    return build_many(jobs, true, explain, names.size());
}

bool ProjectInfo::reconcile_watch() { return watch_->reconcile(*this); }
