#include "Stage.h"
#include <condition_variable>
#include "ProjectInfo.h"
#include "RuntimeJson.h"
#include "ProjectInfoHost.h"
#include "ProjectOwnership.h"
#include "ProjectRead.h"
#include "Hooks.h"
#include <minify/Minify.h>
#include "BuildProgress.h"
#include "Console.h"
#include "FileSystem.h"
#include "JsonFile.h"
#include "Parser.h"
#include "WatchList.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <iostream>
#include <map>
#include <thread>
#include <unordered_set>

namespace fs = std::filesystem;

namespace {
// DAG outputs can be read through symlinks. Share their cache identity while
// preserving the ordinary no-dependency path and retaining lexical fallback.
std::string build_cache_key(const fs::path& path, bool canonical) {
    if (canonical) {
        const auto resolved = filesystem::resolved_path(path);
        if (resolved) return resolved->generic_string();
    }
    return path.lexically_normal().generic_string();
}

std::string build_script_signature(const TrackedInfo& info) {
    std::string signature;
    for (const auto* hooks : {&info.build_hooks, &info.pipeline().discovered_hooks})
        for (const auto& hook : *hooks) {
            signature += hook.first; signature += '\0';
            signature += hook.second; signature += '\0';
        }
    if (!signature.empty()) {
        // Native scripts can inspect these tracked metadata values.
        signature += "type"; signature += '\0';
        signature += info.type.value_or(""); signature += '\0';
        signature += "frontmatter"; signature += '\0';
        signature += info.frontmatter.value_or(""); signature += '\0';
    }
    return signature;
}

// Keep ordinary builds informative without flooding the terminal on large sites.
constexpr std::size_t detailed_build_output_limit = 10;
constexpr std::size_t summary_path_sample_limit = 5;

// A directory is a Nift project root only where the relevant project state
// exists: BOTH .nift/config.json and .nift/tracked.json. There is no global
// Nift configuration; in particular the historical ~/.nift global config
// directory (config.json with no tracked.json) must never be mistaken for a
// project root by the upward walk.
fs::path find_project_root(fs::path path = fs::current_path()) {
    std::error_code error;
    path = fs::absolute(path, error);
    while (true) {
        if (filesystem::path_exists(path / ".nift/config.json") &&
            filesystem::path_exists(path / ".nift/tracked.json"))
            return path;
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
 stage::Scope stage_scope("open");
    root = find_project_root();
    if (root.empty()) {
        console::error("not a Nift project");
        return false;
    }
    return load_config() && load_tracking() && watch_->load(root);
}

bool ProjectInfo::load_config() {
    invalidate_content_model();
    const fs::path path = root / ".nift/config.json";
    std::string error;
    if (!project_read::load_config(root, config, error)) {
        console::error(console::path(relative(path), true) + ": " + error);
        return false;
    }
    return true;
}

bool ProjectInfo::load_tracking() {
 stage::Scope stage_scope("tracking");
    invalidate_hierarchy();
    const fs::path path = root / ".nift/tracked.json";
    std::string error;
    if (!project_read::load_tracking(root, config, tracked, error)) {
        console::error(console::path(relative(path), true) + ": " + error);
        tracked.clear();
        return false;
    }
    rebuild_tracked_index();
    has_build_dependencies=false;
    for(const auto& info:tracked) has_build_dependencies=has_build_dependencies||!info.pipeline().depends.empty();
    return true;
}


bool ProjectInfo::save_tracking() const {
    rebuild_tracked_index();
    invalidate_hierarchy();
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
        if(info.type) entry["type"]=*info.type;
        if(info.frontmatter) entry["frontmatter"]=*info.frontmatter;
        for(const auto& hook:info.build_hooks) entry[hook.first]=hook.second;
        if(!info.pipeline().depends.empty()){entry["depends"]=json::Document::make_array();for(const auto& name:info.pipeline().depends)entry["depends"].push_back(json::Document(name));}
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

std::shared_ptr<const content_model::Model> ProjectInfo::content_model_value() const {
    std::lock_guard<std::mutex> lock(content_model_mutex_);
    if (!content_model_value_) {
        content_model_value_ = std::make_shared<const content_model::Model>(
            content_model::load(root, config.schema_files, config.taxonomy_files));
    }
    return content_model_value_;
}

std::shared_ptr<const HierarchyIndex> ProjectInfo::hierarchy_index() const {
    std::lock_guard<std::mutex> lock(hierarchy_mutex_);
    if (!hierarchy_) {
        hierarchy_ = std::make_shared<const HierarchyIndex>(HierarchyIndex::build(tracked));
        // Publish the compact structural fingerprint when the index is built so
        // hierarchy consumers that render during this build record the value.
        const fs::path path = root / ".nift/hierarchy.fingerprint";
        const std::string fingerprint = hierarchy_->fingerprint();
        if (!filesystem::read_file_checked(path) || *filesystem::read_file_checked(path) != fingerprint)
            filesystem::write_file(path, fingerprint);
    }
    return hierarchy_;
}

void ProjectInfo::refresh_hierarchy_fingerprint() const {
    const auto hi = hierarchy_index();
    if (!hi) return;
    const std::string fingerprint = hi->fingerprint();
    const fs::path path = root / ".nift/hierarchy.fingerprint";
    if (auto existing = filesystem::read_file_checked(path); existing && *existing == fingerprint)
        return;
    filesystem::write_file(path, fingerprint);
}

std::shared_ptr<const nift::RuntimeValue> ProjectInfo::runtime_project_value() const {
    std::lock_guard<std::mutex> lock(project_value_mutex_);
    if (!runtime_project_value_) {
        const auto content_model = content_model_value();
        const auto project_value = make_project_value(root, config, tracked, content_model.get());
        if (project_fingerprint_.empty()) {
            project_fingerprint_ = project_fingerprint_of(*project_value);
            write_project_fingerprint(project_fingerprint_);
        }
        runtime_project_value_ = std::make_shared<const nift::RuntimeValue>(
            nift::runtime_from_json(*project_value));
    }
    return runtime_project_value_;
}

std::string ProjectInfo::project_fingerprint_of(const json::Document& model) {
    const std::string serialized = model.dump(0);
    std::uint64_t hash = 1469598103934665603ull;
    for (unsigned char byte : serialized) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(hash));
    return std::string(buffer);
}

void ProjectInfo::refresh_project_fingerprint() const {
    std::lock_guard<std::mutex> lock(project_value_mutex_);
    if (project_fingerprint_.empty()) {
        const auto content_model = content_model_value();
        const auto model = make_project_value(root, config, tracked, content_model.get());
        project_fingerprint_ = project_fingerprint_of(*model);
        write_project_fingerprint(project_fingerprint_);
    }
}

void ProjectInfo::invalidate_project_value() const {
    std::lock_guard<std::mutex> lock(project_value_mutex_);
    runtime_project_value_.reset();
    project_fingerprint_.clear();
}

void ProjectInfo::invalidate_content_model() const {
    {
        std::lock_guard<std::mutex> lock(content_model_mutex_);
        content_model_value_.reset();
    }
    invalidate_project_value();
}

void ProjectInfo::invalidate_hierarchy() const {
    {
        std::lock_guard<std::mutex> lock(hierarchy_mutex_);
        hierarchy_.reset();
    }
    invalidate_project_value();
}

void ProjectInfo::write_project_fingerprint(const std::string& fingerprint) const {
    const fs::path path = root / ".nift/project.fingerprint";
    if (auto existing = filesystem::read_file_checked(path); existing && *existing == fingerprint)
        return;
    filesystem::write_file(path, fingerprint);
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
    invalidate_hierarchy();
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
    const std::string key = build_cache_key(path, has_build_dependencies);
    if (has_build_dependencies) {
        // Keep an in-flight read from publishing old output bytes after a
        // producer has invalidated that identity at scheduler completion.
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        const auto existing = shared_source_cache_.find(key);
        if (existing != shared_source_cache_.end()) return existing->second.get();
        const auto read = filesystem::read_file_checked(path);
        if (!read) return nullptr;
        auto contents = std::make_unique<const std::string>(*read);
        const auto result = contents.get();
        shared_source_cache_.emplace(key, std::move(contents));
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        const auto it = shared_source_cache_.find(key);
        if (it != shared_source_cache_.end()) return it->second.get();
    }

    // The read is the authority: nullptr for missing/unreadable/non-regular
    // (only successful reads are cached; the error path re-probes per render).
    // Do not convert a failed read into an empty file: that would silently
    // render unreadable content as empty instead of reporting it.
    const auto read = filesystem::read_file_checked(path);
    if (!read.has_value()) return nullptr;
    auto contents = std::make_unique<const std::string>(*read);
    const std::string* result = contents.get();
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        auto [it, inserted] = shared_source_cache_.emplace(key, std::move(contents));
        if (!inserted) result = it->second.get();
    }
    return result;
}

std::uint64_t ProjectInfo::observed_source_hash(const std::string* bytes) const {
    std::lock_guard<std::mutex> lock(source_cache_mutex_);
    auto [it, inserted] = observed_source_hashes_.emplace(bytes, 0);
    if (inserted) it->second = filesystem::hash_bytes(*bytes);
    return it->second;
}

std::shared_ptr<const json::Document> ProjectInfo::read_shared_json(const fs::path& path, std::string& error) const {
    const fs::path normalized = fs::absolute(path).lexically_normal();
    const std::string key = build_cache_key(normalized, has_build_dependencies);

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

    const auto bytes = read_shared_source(normalized);
    if (!bytes) { error = "JSON file is not readable"; return {}; }
    const std::string& source = *bytes;
    auto document = std::make_shared<json::Document>();
    if (!nift_json::parse(source, *document, error)) return {};

    std::shared_ptr<const json::Document> immutable = document;
    shared_json_cache_.emplace(key, immutable);
    return immutable;
}

std::shared_ptr<const nift::RuntimeValue> ProjectInfo::read_shared_runtime_json(
    const fs::path& path, std::string& error) const {
    const fs::path normalized = fs::absolute(path).lexically_normal();
    const std::string key = build_cache_key(normalized, has_build_dependencies);
    std::lock_guard<std::mutex> lock(json_cache_mutex_);
    const auto existing = shared_runtime_json_cache_.find(key);
    if (existing != shared_runtime_json_cache_.end()) return existing->second;
    if (!filesystem::path_exists(normalized)) { error = "JSON file does not exist"; return {}; }
    if (!filesystem::file_readable(normalized)) { error = "JSON file is not readable"; return {}; }
    json::Document document;
    const auto bytes = read_shared_source(normalized);
    if (!bytes) { error = "JSON file is not readable"; return {}; }
    if (!nift_json::parse(*bytes, document, error)) return {};
    auto value = std::make_shared<const nift::RuntimeValue>(nift::runtime_from_json(document));
    shared_runtime_json_cache_.emplace(key, value);
    return value;
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
        dependencies.insert(relative((root / dependency_name).lexically_normal()));
    }
    return true;
}

std::uint64_t ProjectInfo::current_hash_cached(const fs::path& dependency) const {
    const std::string key = build_cache_key(dependency, has_build_dependencies);
    std::lock_guard<std::mutex> lock(hash_mutex_);
    auto it = current_hash_cache_.find(key);
    if (it != current_hash_cache_.end()) return it->second;
    const auto value = filesystem::hash_path(dependency);
    current_hash_cache_.emplace(key, value);
    return value;
}

void ProjectInfo::invalidate_output_caches(const fs::path& output) {
    const auto key = build_cache_key(output, true);
    {
        std::lock_guard<std::mutex> lock(hash_mutex_);
        // A declared directory hash also changes when a descendant output changes.
        for (fs::path path(key); !path.empty();) {
            current_hash_cache_.erase(path.generic_string());
            const auto parent = path.parent_path();
            if (parent == path) break;
            path = parent;
        }
    }
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        auto found = shared_source_cache_.find(key);
        if (found != shared_source_cache_.end()) {
            retired_sources_.push_back(std::move(found->second));
            shared_source_cache_.erase(found);
        }
    }
    {
        std::lock_guard<std::mutex> lock(json_cache_mutex_);
        shared_json_cache_.erase(key);
        shared_runtime_json_cache_.erase(key);
    }
}

void ProjectInfo::reset_build_caches() {
    {
        std::lock_guard<std::mutex> lock(hash_mutex_);
        current_hash_cache_.clear();
        refreshed_hashes_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        shared_source_cache_.clear();
        observed_source_hashes_.clear();
        retired_sources_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(json_cache_mutex_);
        shared_json_cache_.clear();
        shared_runtime_json_cache_.clear();
    }
    invalidate_content_model();
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
        const std::string key = build_cache_key(normalized, has_build_dependencies);
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

bool ProjectInfo::dependency_changed(const fs::path& dependency, fs::file_time_type page_info_mtime, const json::Document& snapshots, const std::string& name) const {
    if (!filesystem::path_exists(dependency)) return true;
    // Strictly-greater comparison misses edits whose mtime lands in the same
    // filesystem timestamp quantum as the page-info write (equal timestamps
    // are common on coarse-resolution or batched-update filesystems). An equal
    // timestamp is indistinguishable from "edited right at the last build", so
    // modified mode treats it as potentially stale and rebuilds: a build must
    // never report success while a source change went unnoticed. Hash mode is
    // unaffected (content hashing, no timestamps); hybrid keeps both signals.
    if (config.incremental_mode == "modified")
        return filesystem::modified_time(dependency) >= page_info_mtime;
    if (!snapshots.is_object() || !snapshots.has(name) || !snapshots[name].is_string()) return true;
    const auto& value = snapshots[name].string;
    if (value.empty() || value.size() > 20 || value.find_first_not_of("0123456789") != std::string::npos) return true;
    if (config.incremental_mode == "hybrid" && filesystem::modified_time(dependency) >= page_info_mtime) return true;
    return value != std::to_string(current_hash_cached(dependency));
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

    const std::string current_hooks = build_script_signature(info);
    const std::string old_hooks=document.has("build-hooks")&&document["build-hooks"].is_string()?document["build-hooks"].string:std::string{};
    if(old_hooks!=current_hooks) reasons.push_back("build scripts changed");
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
        if (value.string == ".nift/project.fingerprint") {
            // Value-based project dependency: the fingerprint file is rewritten
            // only when the shared project model changes, and the page records
            // the value it was built against, so staleness is decided by
            // comparing values instead of mtimes (which can collide between the
            // fingerprint write and the page-info write within one fast build).
            refresh_project_fingerprint();
            if (config.incremental_mode != "modified" && dependency_changed(dependency,page_info_mtime,document["dependency-hashes"],value.string))
                reasons.push_back("dependency snapshot changed or invalid: " + value.string);
            const std::string current = filesystem::read_file_checked(root / ".nift/project.fingerprint").value_or(std::string{});
            std::string stored;
            if (document.has("project-fingerprint") && document["project-fingerprint"].is_string())
                stored = document["project-fingerprint"].string;
            if (current != stored)
                reasons.push_back("project model changed");
            continue;
        }
        if (value.string == ".nift/hierarchy.fingerprint") {
            // Compact structural hierarchy dependency: one fingerprint value
            // covering the page name/parentage structure, so add/remove/rename/
            // reparent invalidates hierarchy consumers without a per-page x
            // project-size dependency list.
            refresh_hierarchy_fingerprint();
            if (config.incremental_mode != "modified" && dependency_changed(dependency,page_info_mtime,document["dependency-hashes"],value.string))
                reasons.push_back("dependency snapshot changed or invalid: " + value.string);
            const std::string current = filesystem::read_file_checked(root / ".nift/hierarchy.fingerprint").value_or(std::string{});
            std::string stored;
            if (document.has("hierarchy-fingerprint") && document["hierarchy-fingerprint"].is_string())
                stored = document["hierarchy-fingerprint"].string;
            if (current != stored)
                reasons.push_back("hierarchy structure changed");
            continue;
        }
        if (!filesystem::path_exists(dependency))
            reasons.push_back("dependency removed: " + value.string);
        else if (dependency_changed(dependency, page_info_mtime, document["dependency-hashes"], value.string))
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
            else if (dependency_changed(dependency, page_info_mtime, document["dependency-hashes"], dependency_name))
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
        const console::SourceExcerpt excerpt = console::render_source_excerpt(
            error.source_line, error.column, error.source_length);
        std::cerr << "    " << console::highlight_nift_source(
            excerpt.excerpt, excerpt.span_byte_start, excerpt.span_byte_length) << '\n';
        if (error.column) {
            const std::string marker =
                "^" + std::string(excerpt.underline_columns > 1 ? excerpt.underline_columns - 1 : 0, '~');
            std::cerr << "    " << std::string(excerpt.caret_columns, ' ')
                      << console::diagnostic_offender(marker) << '\n';
        }
    }
}

void ProjectInfo::report_build_error(const BuildError& error, std::optional<BuildError>* out_error) const {
    if (out_error) {
        *out_error = error;
        return;
    }
    print_build_error(error);
}

bool ProjectInfo::write_page_info(const TrackedInfo& info, const std::set<std::string>& dependencies, const std::set<std::string>& reqs, std::size_t pagination_pages, const std::map<std::string,std::string>& snapshots) const {
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
    const std::string hooks_signature = build_script_signature(info);
    if(!hooks_signature.empty()) {output += ",\n  \"build-hooks\": \"";json::Document::append_escaped_string(output,hooks_signature);output += "\"";}
    output += ",\n  \"project-fingerprint\": \"";
    if (dependencies.count(".nift/project.fingerprint") != 0)
        json::Document::append_escaped_string(output,
            filesystem::read_file_checked(root / ".nift/project.fingerprint").value_or(std::string{}));
    output += "\",\n  \"hierarchy-fingerprint\": \"";
    if (dependencies.count(".nift/hierarchy.fingerprint") != 0)
        json::Document::append_escaped_string(output,
            filesystem::read_file_checked(root / ".nift/hierarchy.fingerprint").value_or(std::string{}));
    output += "\",\n  \"dependencies\": [";

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
    output += "],\n  \"dependency-hashes\": {";
    std::size_t hash_index = 0;
    for (const auto& [path, hash] : snapshots) {
        if (hash_index++) output += ",";
        output += "\n    \"";
        json::Document::append_escaped_string(output, path);
        output += "\": \"" + hash + "\"";
    }
    output += "\n  }\n}\n";

    // Certification metadata uses the existing atomic readonly replacement.
    return filesystem::write_readonly_file(info_path(info), output);
}

bool ProjectInfo::build_one(TrackedInfo& info, std::optional<BuildError>* out_error) {
    std::map<std::string,std::string> snapshots;
    const bool hashing = config.incremental_mode != "modified";
    std::set<std::string> declared;
    BuildError declaration_error;
    if (!load_user_dependencies(info, declared, &declaration_error)) { report_build_error(declaration_error,out_error); return false; }
    if (hashing) for (const auto& path : declared) snapshots[path] = std::to_string(filesystem::hash_path(root/path));
    std::set<std::string> legacy_dependencies;
    std::string legacy_error;
    if(!nift_hooks::run_file_hooks(root,info,"pre",current_hook_mode_,legacy_error,&legacy_dependencies,hashing ? &snapshots : nullptr,&declared)){report_build_error({info.name,{},0,legacy_error},out_error);return false;}

    // The previous pagination page count is historical state required for
    // stale-output cleanup: when pagination is removed or its page count
    // decreases, page-2..N outputs from the previous build must be removed.
    // This read is therefore required for every page (a page whose pagination
    // was removed no longer has info.paginate set, so gating the read on the
    // current paginate would silently leave stale pagination outputs).
    std::size_t previous_pagination_pages = 0;
    {
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

    ProjectInfoHost host(*this);
    host.observe_dependencies = hashing;
    std::set<std::string> hook_dependencies;
    auto item_script = [&](const std::string& phase, RenderResult* returned=nullptr) {
        auto it=info.build_hooks.find(phase); std::string path;
        if(it!=info.build_hooks.end())path=it->second;
        else {auto found=info.pipeline().discovered_hooks.find(phase);if(found!=info.pipeline().discovered_hooks.end())path=found->second;}
        if(path.empty())return true;
        const fs::path source=(root/path).lexically_normal();
        if(!filesystem::path_within(root,source)||source.extension()!=".f"||!filesystem::file_readable(source)) {report_build_error({info.name,source,0,"build script must be a readable project-local .f file"},out_error);return false;}
        host.build_environment={{"NIFT_HOOK_PHASE",phase},{"NIFT_HOOK_MODE",current_hook_mode_},{"NIFT_HOOK_TARGET",info.name},{"NIFT_HOOK_CONTENT",relative(content)},{"NIFT_HOOK_OUTPUT",relative(output_path(info))},{"NIFT_HOOK_TEMPLATE",info.template_path},{"NIFT_HOOK_ROOT",root.generic_string()}};
        Parser script(host,info);
        // Script writes are recovery-relevant even if it later fails.
        mark_mutation();
        const auto source_bytes = filesystem::read_file(source);
        host.observe_dependency(source, source_bytes);
        auto rr=script.run_script(source_bytes,source,true);
        if(!rr.ok){report_build_error(rr.error,out_error);return false;}
        hook_dependencies.insert(relative(source));hook_dependencies.insert(rr.dependencies.begin(),rr.dependencies.end());
        if(returned)*returned=std::move(rr);
        return true;
    };
    if(!item_script("pre-build"))return false;
    if (!filesystem::path_exists(content)) {
        report_build_error({info.name, content, 0, "content file does not exist"}, out_error);
        return false;
    }

    const bool custom=info.build_hooks.count("build")||info.pipeline().discovered_hooks.count("build");
    RenderResult result;
    if(custom){if(!item_script("build",&result))return false;result.dependencies.insert(relative(content));}
    else {Parser parser(host,info);result=parser.render();}
    result.dependencies.insert(hook_dependencies.begin(),hook_dependencies.end());
    result.dependencies.insert(legacy_dependencies.begin(),legacy_dependencies.end());
    if (!result.ok) { report_build_error(result.error, out_error); return false; }
    if (!load_user_dependencies(info, result.dependencies, &result.error)) { report_build_error(result.error, out_error); return false; }

    const fs::path output = output_path(info);
    const std::string extension = info.output_ext.empty() ? config.output_ext : info.output_ext;
    std::string normalized_extension = extension;
    std::transform(normalized_extension.begin(), normalized_extension.end(), normalized_extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool should_minify = info.minify.has_value()
        ? *info.minify
        : config.minify_exts.count(normalized_extension) != 0;
    if (should_minify && !custom) {
        minify::Format format;
        if (!minify::format_for_extension(extension, format)) {
            report_build_error({info.name, output, 0, "no minifier is available for output extension " + extension}, out_error);
            return false;
        }
        auto minify_output = [&](std::string& page_output) {
            std::string minified, minify_error;
            if (!minify::run(format, page_output, minified, minify_error)) {
                report_build_error({info.name, output, 0, "minification failed" +
                                    (minify_error.empty() ? std::string() : ": " + minify_error)}, out_error);
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

    // First recovery-relevant derived mutation of this page (outputs). The
    // flag is set BEFORE the write: an interruption during the write is
    // precisely the case the marker must protect.
    mark_mutation();
    if(custom) {
        std::error_code ec;
        if(!filesystem::path_within(root,output)||!fs::is_regular_file(output,ec)||ec){report_build_error({info.name,output,0,"custom build did not produce the tracked output file"},out_error);return false;}
    } else if (!result.pagination_outputs.empty()) {
        std::vector<std::pair<fs::path, std::string>> page_files;
        page_files.reserve(result.pagination_outputs.size());
        for (std::size_t page = 1; page <= result.pagination_outputs.size(); ++page)
            page_files.emplace_back(pagination_output_path(info, page), result.pagination_outputs[page - 1]);
        if (!filesystem::write_direct_files(page_files, output_mode)) {
            report_build_error({info.name, output, 0, "failed to commit generated pagination outputs"}, out_error);
            return false;
        }
    } else if (!filesystem::write_direct_file(output, result.output, output_mode)) {
        report_build_error({info.name, output, 0, "failed to write generated output"}, out_error);
        return false;
    }

    if(!item_script("post-build"))return false;
    if(!nift_hooks::run_file_hooks(root,info,"post",current_hook_mode_,legacy_error,&legacy_dependencies,hashing ? &snapshots : nullptr,&result.dependencies)){report_build_error({info.name,{},0,legacy_error},out_error);return false;}
    if(custom){std::error_code ec;if(!filesystem::path_within(root,output)||!fs::is_regular_file(output,ec)||ec){report_build_error({info.name,output,0,"custom post-build removed the tracked output"},out_error);return false;}}
    if (!custom) {
        for (std::size_t page = 1; page <= std::max<std::size_t>(1,result.pagination_outputs.size()); ++page) {
            const auto generated = pagination_output_path(info,page);
            std::error_code ec;
            if (!fs::is_regular_file(generated,ec) || ec) {
                report_build_error({info.name,generated,0,"post-build removed a tracked output; metadata was not certified"},out_error);
                return false;
            }
        }
    }
    for(const auto& hook:info.build_hooks)if(hook.first.find(' ')!=std::string::npos)hook_dependencies.insert(relative((root/hook.second).lexically_normal()));
    result.dependencies.insert(hook_dependencies.begin(),hook_dependencies.end());
    result.dependencies.insert(legacy_dependencies.begin(),legacy_dependencies.end());
    if (hashing) {
        std::string conflict;
        const auto observations = host.dependency_observations(conflict);
        for (const auto& path : host.dependency_conflicts()) if (result.dependencies.count(path)) { report_build_error({info.name,root/path,0,"dependency observed at multiple versions; retry build"},out_error); return false; }
        for (const auto& [path, hash] : observations) {
            if (!result.dependencies.count(path)) continue; // FileValue does not implicitly declare dependencies.
            auto it = snapshots.find(path);
            if (it != snapshots.end() && it->second != hash) { report_build_error({info.name,root/path,0,"dependency changed during build; retry build"},out_error); return false; }
            snapshots[path] = hash;
        }
        for (const auto& path : result.dependencies) {
            auto it = snapshots.find(path);
            if (it == snapshots.end()) snapshots[path] = std::to_string(current_hash_cached(root/path));
            else if (declared.count(path) && it->second != std::to_string(filesystem::hash_path(root/path))) { report_build_error({info.name,root/path,0,"declared dependency changed during build; retry build"},out_error); return false; }
        }
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
            report_build_error({info.name, stale, 0, "failed to remove stale pagination output"}, out_error);
            return false;
        }
    }

    if (!write_page_info(info, result.dependencies, result.reqs, new_pagination_pages, snapshots)) {
        report_build_error({info.name, info_path(info), 0, "failed to write page build metadata"}, out_error);
        return false;
    }

    if (has_build_dependencies)
        for (std::size_t page = 1; page <= std::max({std::size_t(1),previous_pagination_pages,new_pagination_pages}); ++page)
            invalidate_output_caches(pagination_output_path(info,page));
    return true;
}

int ProjectInfo::build_many(const std::vector<BuildJob>& initial_jobs, bool targeted, bool full_detail, std::size_t requested_count) {
 stage::Scope stage_scope("build_many");
    std::vector<BuildJob> expanded_jobs;
    if(has_build_dependencies) {
        expanded_jobs=initial_jobs;
        auto& jobs=expanded_jobs;
        std::unordered_set<std::string> included;included.reserve(jobs.size());
        for(const auto& job:jobs)included.insert(job.info->name);
        if(!targeted)for(auto& info:tracked)if(included.insert(info.name).second)jobs.push_back({&info,{},true});
        // Iterate closure; up-to-date prerequisites require no execution.
        for(std::size_t i=0;i<jobs.size();++i) for(const auto& name:jobs[i].info->pipeline().depends) {
            auto* prerequisite=find(name);
            if(!prerequisite){console::error("unknown build prerequisite '"+name+"'");return 1;}
            if(included.insert(name).second){auto reasons=build_reasons(*prerequisite);const bool clean=reasons.empty();jobs.push_back({prerequisite,std::move(reasons),clean});}
        }
    }
    const auto& jobs=has_build_dependencies?expanded_jobs:initial_jobs;
    last_build_affected.clear();
    if (jobs.empty()) {
        if (!console::build_quiet()) {
            std::lock_guard<std::mutex> lock(console::output_mutex);
            if (targeted) return 1;
            std::cout << console::good("✓") << ' ' << requested_count << " tracked "
                      << (requested_count == 1 ? "file is" : "files are") << " up to date\n";
        }
        return 0;
    }

    const unsigned hardware = std::max(1u, std::thread::hardware_concurrency());
    std::size_t thread_count = config.build_threads < 0 ? static_cast<std::size_t>(-static_cast<long long>(config.build_threads)) * hardware : (config.build_threads == 0 ? hardware : static_cast<std::size_t>(config.build_threads));
    thread_count = std::max<std::size_t>(1, std::min(thread_count, jobs.size()));
    // Per-file hooks run the native Parser (process-global env context), so
    // any affected file with hooks serializes the build to keep hooks safe
    // and their output readable.
    bool any_file_hooks = false;
    for (const auto& job : jobs) for(const auto& hook:job.info->build_hooks) if(hook.first.find(' ')!=std::string::npos) any_file_hooks=true;
    if (any_file_hooks) thread_count = 1;

    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> completed{0};
    std::vector<unsigned char> succeeded(jobs.size(), 0);
    std::vector<std::optional<BuildError>> errors(jobs.size());
    BuildProgress progress(jobs.size(), completed);

    std::vector<std::vector<std::size_t>> dependents;
    std::vector<std::size_t> pending;
    std::vector<unsigned char> blocked,executed,prerequisite_executed;
    std::vector<std::size_t> ready;
    std::size_t ready_cursor=0;
    std::mutex scheduler_mutex;
    std::condition_variable scheduler_cv;
    std::size_t remaining=jobs.size();
    const bool scheduler_stats=std::getenv("NIFT_TEST_BUILD_DAG_STATS")!=nullptr;
    const auto planning_started=scheduler_stats?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    std::size_t edge_count=0,active=0,peak_active=0,peak_ready=0,blocked_count=0,wakeups=0;
    std::int64_t lock_wait_ns=0;
    struct QueueSample {std::size_t completed, ready, active;std::int64_t elapsed_ns;};
    std::vector<QueueSample> queue_samples;
    const std::size_t sample_stride=std::max<std::size_t>(1, jobs.size()/32);

    if(has_build_dependencies) {
        dependents.resize(jobs.size());pending.resize(jobs.size());blocked.resize(jobs.size());ready.reserve(jobs.size());executed.resize(jobs.size());prerequisite_executed.resize(jobs.size());
        for(std::size_t i=0;i<jobs.size();++i)executed[i]=!jobs[i].validation_only;
        std::unordered_map<std::string,std::size_t> index;index.reserve(jobs.size());
        for(std::size_t i=0;i<jobs.size();++i)index.emplace(jobs[i].info->name,i);
        for(std::size_t i=0;i<jobs.size();++i)for(const auto& name:jobs[i].info->pipeline().depends){auto found=index.find(name);if(found!=index.end()){dependents[found->second].push_back(i);++pending[i];++edge_count;}}
        for(std::size_t i=0;i<jobs.size();++i)if(!pending[i])ready.push_back(i);
    }
    peak_ready=ready.size();
    if(scheduler_stats&&has_build_dependencies) {queue_samples.reserve(34);queue_samples.push_back({0,ready.size(),0,0});}
    const auto planning_ns=scheduler_stats?std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-planning_started).count():0;
    auto complete_job=[&](std::size_t index) {
        ++completed;
        if(!has_build_dependencies)return;
        std::lock_guard<std::mutex> lock(scheduler_mutex);
        --remaining;
        if(scheduler_stats)--active;
        for(auto dependent:dependents[index]){
            prerequisite_executed[dependent]=prerequisite_executed[dependent]||executed[index]||prerequisite_executed[index];
            if(!succeeded[index]){if(!blocked[dependent])++blocked_count;blocked[dependent]=1;errors[dependent]=BuildError{jobs[dependent].info->name,{},0,"blocked because prerequisite '"+jobs[index].info->name+"' failed"};}
            if(--pending[dependent]==0)ready.push_back(dependent);
        }
        if(scheduler_stats) {
            peak_ready=std::max(peak_ready,ready.size()-ready_cursor);
            const auto finished=jobs.size()-remaining;
            if(finished==jobs.size()||finished/sample_stride>queue_samples.back().completed/sample_stride)
                queue_samples.push_back({finished,ready.size()-ready_cursor,active,std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-planning_started).count()});
        }
        scheduler_cv.notify_all();
    };
    auto worker = [&] {
        while (true) {
            std::size_t index;
            if(has_build_dependencies){const auto lock_started=scheduler_stats?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};std::unique_lock<std::mutex> lock(scheduler_mutex);if(scheduler_stats){lock_wait_ns+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-lock_started).count();}while(ready_cursor==ready.size()&&remaining!=0){scheduler_cv.wait(lock);if(scheduler_stats)++wakeups;}if(!remaining)break;index=ready[ready_cursor++];if(scheduler_stats){++active;peak_active=std::max(peak_active,active);}}
            else {index=next.fetch_add(1);if(index>=jobs.size())break;}
            if(has_build_dependencies&&blocked[index]){complete_job(index);continue;}
            if(jobs[index].validation_only) {
                if(prerequisite_executed[index]) {
                    auto reasons=build_reasons(*jobs[index].info);
                    if(!reasons.empty()){executed[index]=1;expanded_jobs[index].reasons=std::move(reasons);}
                }
                if(!executed[index]){succeeded[index]=1;complete_job(index);continue;}
            }
            // Worker-generated build errors are buffered per job index (each
            // worker owns its slots, so no shared buffer needs locking). They
            // are emitted only after progress has stopped, so no diagnostic
            // can ever be written into an active progress line.
            succeeded[index] = build_one(*jobs[index].info, &errors[index]) ? 1 : 0;
            complete_job(index);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) workers.emplace_back(worker);
    for (auto& worker_thread : workers) worker_thread.join();

    // Deterministic shutdown: stop the renderer, join it, erase the transient
    // line and flush — only then may diagnostics and the summary be written.
    progress.finish();
    if(scheduler_stats)std::cerr<<"BUILD_DAG nodes="<<pending.size()<<" edges="<<edge_count<<" peak_ready="<<peak_ready<<" peak_active="<<peak_active<<" blocked="<<blocked_count<<" wakeups="<<wakeups<<" lock_wait_ns="<<lock_wait_ns<<" plan_ns="<<planning_ns<<'\n';

    if(scheduler_stats&&!queue_samples.empty()) {
        std::cerr<<"BUILD_DAG_QUEUE";
        for(const auto& sample:queue_samples)std::cerr<<' '<<sample.completed<<':'<<sample.ready<<':'<<sample.active<<':'<<sample.elapsed_ns;
        std::cerr<<'\n';
    }

    last_build_affected.clear();
    for (std::size_t i = 0; i < jobs.size(); ++i)
        if (succeeded[i] && (!has_build_dependencies||executed[i])) last_build_affected.push_back(jobs[i].info->name);

    for (std::size_t i = 0; i < jobs.size(); ++i)
        if (errors[i].has_value()) print_build_error(*errors[i]);

    std::size_t successful_count = 0;
    std::size_t validated_count=0;
    for(std::size_t i=0;i<jobs.size();++i)if(succeeded[i]){if(has_build_dependencies&&!executed[i])++validated_count;else ++successful_count;}
    const std::size_t missing_requested = targeted && requested_count > initial_jobs.size() ? requested_count - initial_jobs.size() : 0;
    const std::size_t failed_count = jobs.size() - successful_count - validated_count + missing_requested;

    std::lock_guard<std::mutex> lock(console::output_mutex);

    bool has_rebuild_reasons = false;
    for (const auto& job : jobs) {
        if (!job.reasons.empty()) { has_rebuild_reasons = true; break; }
    }

    const bool detailed = full_detail || (has_rebuild_reasons && jobs.size() <= detailed_build_output_limit);
    if (!console::build_quiet()) {
    if (detailed) {
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            if (!succeeded[i] || (has_build_dependencies&&!executed[i])) continue;
            std::cout << console::good("built") << ' ' << console::path(jobs[i].info->name) << '\n';
            for (const auto& reason : jobs[i].reasons)
                std::cout << console::dim("    ↳ " + reason) << '\n';
        }
    } else if (has_rebuild_reasons) {
        std::map<std::string, std::size_t> reason_counts;
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            if (!succeeded[i] || (has_build_dependencies&&!executed[i])) continue;
            for (const auto& reason : jobs[i].reasons) ++reason_counts[reason];
        }

        if (!reason_counts.empty()) {
            std::cout << console::dim("rebuild causes:") << '\n';
            for (const auto& [reason, count] : reason_counts)
                std::cout << "  " << console::dim("↳ " + reason + " → " + std::to_string(count) + (count == 1 ? " file" : " files")) << '\n';
        }

        std::cout << console::dim("affected files: ");
        std::size_t shown = 0;
        for (std::size_t i = 0; i < jobs.size() && shown < summary_path_sample_limit; ++i) {
            if (!succeeded[i] || (has_build_dependencies&&!executed[i])) continue;
            if (shown) std::cout << console::dim(", ");
            std::cout << console::path(jobs[i].info->name);
            ++shown;
        }
        if (successful_count > shown) std::cout << console::dim("  +" + std::to_string(successful_count - shown) + " more");
        std::cout << '\n';
    }

    if(!successful_count&&!failed_count&&!targeted) {
        std::cout<<console::good("✓")<<' '<<requested_count<<" tracked files are up to date\n";
    } else if(targeted&&has_build_dependencies) {
        std::cout<<successful_count<<" target/prerequisite items built; "<<validated_count<<" prerequisites up to date"<<(failed_count?"; build failed":"; build succeeded")<<'\n';
    } else if (targeted) {
        // Keep the historical wording because scripts and the regression suite rely on it.
        if (failed_count == 0) std::cout << console::good("📦") << " all " << successful_count << " specified files built successfully\n";
        else std::cout << successful_count << " of " << requested_count << " specified files built successfully\n";
    } else if (failed_count == 0) {
        const bool incremental = !jobs.empty() && !jobs.front().reasons.empty();
        std::cout << console::good("📦") << ' ' << successful_count << ' '
                  << (successful_count == 1 ? "file " : "files ")
                  << (incremental ? "rebuilt" : "built") << " successfully\n";
    } else {
        std::cout << successful_count << " of " << jobs.size() << " files built successfully\n";
    }
    }

    return failed_count == 0 ? 0 : 1;
}

int ProjectInfo::build_all(bool force, bool explain, bool repair) {
 stage::Scope stage_scope("build_all");
    filesystem::begin_recovery_epoch();

    // Acquire live-build ownership (OS lock + durable .unfinished marker)
    // BEFORE the first derived-state mutation. The first mutation in the build
    // path is reconcile_watch() (it removes outputs for disappeared watched
    // pages and rewrites watched state). Load/validation happened in open().
    //
    // Ordinary builds refuse on a stale marker (previous epoch unfinished) or
    // a live owner. build --repair is the only mode that may take over a stale
    // marker; it refuses on a live owner just like everything else. No derived
    // state is mutated before acquisition, and nothing is mutated after the
    // marker is removed on success.
    ProjectOwnership ownership(root / ".nift/.unfinished");
    const ProjectOwnership::State ownership_state = ownership.acquire();
    if (ownership_state == ProjectOwnership::State::Live) {
        std::lock_guard<std::mutex> lock(console::output_mutex);
        std::cerr << console::error_label() << " another build appears to be running;"
                  << " refusing to mutate build state\n";
        return 1;
    }
    if (ownership_state == ProjectOwnership::State::Failed) {
        std::lock_guard<std::mutex> lock(console::output_mutex);
        std::cerr << console::error_label() << " could not acquire the build lock ("
                  << relative(root / ".nift/.unfinished") << ")\n";
        return 1;
    }
    if (ownership_state == ProjectOwnership::State::Stale && !repair) {
        std::lock_guard<std::mutex> lock(console::output_mutex);
        std::cerr << console::error_label() << " unfinished build detected.\n";
        std::cerr << "  Generated build state may be incomplete.\n";
        std::cerr << "  Run " << console::good("nift build --repair") << " to reconstruct it.\n";
        return 1;
    }

    mutation_started_.store(false, std::memory_order_relaxed);

    // reconcile_watch failure is a controlled failure subject to the same
    // epoch-completion rule: if it failed BEFORE any recovery-relevant derived
    // removal (mutation_started still false), nothing needs repair and the
    // marker is cleared; if it removed outputs then failed, the marker stays.
    if (!reconcile_watch()) {
        return finish_if_epoch_complete(ownership, 1, repair);
    }
    reset_build_caches();

    const std::string mode = nift_hooks::effective_mode(force, repair, build_mode_hint_);
    // Project pre-hooks run before the affected-set calculation so generated
    // inputs can change dependencies for this invocation.
    { std::string hook_error;
      if (!nift_hooks::run_project_hooks(root, config, "pre", mode, hook_error)) {
          std::lock_guard<std::mutex> lock(console::output_mutex);
          std::cerr << console::error_label() << ' ' << hook_error << '\n';
          return finish_if_epoch_complete(ownership, 1, repair);
      }
    }
    current_hook_mode_ = mode;

    const auto dirty_started=std::chrono::steady_clock::now();
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

    stage::emit("dirty_scan",dirty_started);
    const int result = build_many(jobs, false, explain, tracked.size());
    if (result == 0) {
        std::string hook_error;
        if (!nift_hooks::run_project_hooks(root, config, "post", mode, hook_error)) {
            std::lock_guard<std::mutex> lock(console::output_mutex);
            std::cerr << console::error_label() << ' ' << hook_error << '\n';
            return finish_if_epoch_complete(ownership, 1, repair);
        }
    }
    return finish_if_epoch_complete(ownership, result, repair);
}

int ProjectInfo::finish_if_epoch_complete(ProjectOwnership& ownership, int result, bool repair) {
    if (result != 0) {
        // A failure occurred: clear the marker only for an ordinary build with
        // PROVEN zero recovery-relevant mutations; build --repair always
        // retains on failure because its pre-existing stale evidence is
        // unresolved. The command still reports failure.
        if (!repair && !mutation_started()) ownership.finish();
        return result;
    }
    // Success. A successful repair additionally runs the required sweep BEFORE
    // the marker is removed; if a required sweep operation fails, repair has
    // not converged, so the marker is retained and the command fails.
    if (repair && !repair_derived_state()) return 1;
    ownership.finish(); // remove marker strictly after the final mutation
    return 0;
}

namespace {
// Parses a decimal pagination-page suffix beginning at `begin` in `s`. Returns
// true and sets `page` (>= 2) only when the suffix matches Nift's canonical
// pagination grammar: entirely decimal digits, NO leading zero (Nift emits
// unpadded std::to_string(page), so "-02"/"-003" are paths Nift never
// generates), numerically >= 2, and representable without overflow. Everything
// else - non-digits, leading zeros ("-0", "-00", "-01", "-02", "-0009"),
// values below 2, oversized/overflowing values - returns false so callers
// PRESERVE the file.
bool parse_pagination_index(const std::string& s, std::size_t begin, std::size_t& page) {
    if (begin >= s.size()) return false;
    if (s[begin] == '0') return false; // non-canonical leading zero
    std::uint64_t value = 0;
    for (std::size_t i = begin; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < '0' || c > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    if (value < 2 || value > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return false;
    page = static_cast<std::size_t>(value);
    return true;
}
} // namespace

bool ProjectInfo::repair_derived_state() {
    // Ownership model (CP4-DESIGN.md): only files Nift can establish as its
    // own derived artifacts are removed. The output tree is never wiped, and
    // derived metadata is never used to authorize deleting a public file.
    // Current info paths identify the page (the info filename derives from the
    // OUTPUT path, not the page name - the "/" page's info is index.info.json).
    std::unordered_set<fs::path> current_info_paths;
    current_info_paths.reserve(tracked.size());
    std::unordered_set<fs::path> current_owned;
    struct PaginationPattern { std::string base; std::string ext; };
    std::unordered_map<std::string, std::vector<PaginationPattern>> patterns_by_dir;

    for (const auto& info : tracked) {
        current_info_paths.insert(info_path(info).lexically_normal());
        const fs::path out = output_path(info).lexically_normal();
        current_owned.insert(out);
        // Current pagination count from the just-written .info.json (the
        // rebuild succeeded, so every page's info is fresh and valid).
        std::size_t pages = 0;
        json::Document doc;
        std::string error;
        if (load_json_file(info_path(info), doc, error) && doc.is_object() &&
            doc.has("pagination-pages") && doc["pagination-pages"].is_number() &&
            std::isfinite(doc["pagination-pages"].num) && doc["pagination-pages"].num >= 1)
            pages = static_cast<std::size_t>(doc["pagination-pages"].num);
        if (pages >= 2) {
            for (std::size_t n = 2; n <= pages; ++n)
                current_owned.insert(pagination_output_path(info, n).lexically_normal());
            // Register the in-use pagination namespace so the output-tree
            // sweep can remove surplus pages N > count (ownership established
            // by the current render).
            const std::string ext = info.output_ext.empty() ? config.output_ext : info.output_ext;
            patterns_by_dir[out.parent_path().generic_string()].push_back(
                {out.stem().generic_string(), ext});
        }
    }

    // 1. Pagination surplus sweep (REQUIRED): files in a currently-paginated
    //    tracked page's <base>-<N> namespace beyond the current count. A
    //    traversal error or a failed removal means repair has not converged.
    {
        std::error_code ec;
        const fs::path output_root = root / config.output_dir;
        if (fs::is_directory(output_root, ec)) {
            for (const auto& entry : fs::recursive_directory_iterator(output_root, ec)) {
                if (ec) return false; // incomplete traversal: do not certify success
                std::error_code fec;
                if (!entry.is_regular_file(fec) || fec) continue;
                const fs::path path = entry.path().lexically_normal();
                if (current_owned.count(path)) continue;
                const auto it = patterns_by_dir.find(path.parent_path().generic_string());
                if (it == patterns_by_dir.end()) continue;
                const std::string fname = path.stem().generic_string();
                const std::string ext = path.extension().generic_string();
                for (const auto& pattern : it->second) {
                    if (ext != pattern.ext) continue;
                    const std::size_t marker = pattern.base.size();
                    if (fname.size() < marker + 2 || fname.compare(0, marker, pattern.base) != 0 ||
                        fname[marker] != '-')
                        continue;
                    // The suffix must be entirely decimal digits and parse to a
                    // numeric index N >= 2 (Nift pagination starts at -2, so
                    // -0 and -1 are NOT in the owned namespace). Leading zeros
                    // that numerically fall below 2, and oversized/overflowing
                    // suffixes, fail closed (preserve the file).
                    std::size_t page_index = 0;
                    if (parse_pagination_index(fname, marker + 1, page_index)) {
                        if (!filesystem::remove_owned_file(path)) return false;
                    }
                    break; // this file belongs to this page's namespace (kept or removed)
                }
            }
        }
    }

    // 2. Orphan .info.json sweep (REQUIRED): every .info.json under
    //    .nift/public/ is Nift-owned derived metadata, so an orphan's metadata
    //    is deleted. Its historical public output is PRESERVED: the only way
    //    to know that output's path/extension is the derived metadata itself,
    //    which repair must distrust (a corrupt-but-valid info could name any
    //    path, and a custom historical output extension makes a default-ext
    //    guess unsafe). This is the documented orphan-output limitation.
    {
        std::error_code ec;
        const fs::path info_root = root / ".nift" / config.output_dir;
        if (fs::is_directory(info_root, ec)) {
            for (const auto& entry : fs::recursive_directory_iterator(info_root, ec)) {
                if (ec) return false; // incomplete traversal
                std::error_code fec;
                if (!entry.is_regular_file(fec) || fec) continue;
                const fs::path path = entry.path();
                const std::string filename = path.filename().generic_string();
                constexpr const char* suffix = ".info.json";
                if (filename.size() <= std::char_traits<char>::length(suffix) ||
                    filename.compare(filename.size() - std::char_traits<char>::length(suffix),
                                     std::char_traits<char>::length(suffix), suffix) != 0)
                    continue;
                if (current_info_paths.count(path.lexically_normal())) continue; // current page
                // Nift writes .info.json read-only; Windows cannot unlink a
                // read-only file unless the attribute is cleared first, so use
                // the ownership-aware removal (POSIX unlink also works from a
                // writable directory). Without this, repair failed on Windows
                // with ERROR_ACCESS_DENIED for an orphaned page's metadata.
                if (!filesystem::remove_owned_file(path)) return false; // required metadata removal failed
            }
        }
    }

    // 3. Stale stored-hash sweep (BEST-EFFORT cache hygiene): stored hashes
    //    are regenerable and invalidated independently (stored_hash_changed
    //    treats a missing hash as changed), so a failure here does not fail
    //    repair. Removes hashes whose mirrored SOURCE path no longer exists
    //    (the .hash suffix is stripped before mirroring).
    {
        std::error_code ec;
        const fs::path nift_root = root / ".nift";
        for (const auto& entry : fs::recursive_directory_iterator(nift_root, ec)) {
            if (ec) break; // best-effort
            std::error_code fec;
            if (!entry.is_regular_file(fec) || fec) continue;
            const fs::path path = entry.path();
            if (path.extension() != ".hash") continue;
            const fs::path rel = path.lexically_relative(nift_root);
            if (rel.empty()) continue;
            const std::string first = rel.begin()->generic_string();
            if (first == "public" || first == ".watch" || first == "config.json" || first == "tracked.json")
                continue;
            fs::path mirrored = root / rel;
            mirrored.replace_extension(); // strip the trailing .hash
            std::error_code mec;
            if (!fs::is_regular_file(mirrored, mec))
                std::filesystem::remove(path, mec);
        }
    }

    return true;
}

int ProjectInfo::build_names(const std::vector<std::string>& names, bool, bool explain) {
    filesystem::begin_recovery_epoch();

    // Only validation that is genuinely independent of the CURRENT reconciled
    // tracking state runs before acquisition: duplicate-string rejection reads
    // nothing from `tracked`. Tracked-name resolution must NOT happen here:
    // reconcile_watch() can structurally modify `tracked` (add discovered
    // pages, erase disappeared ones), which would invalidate any TrackedInfo*
    // captured across it.
    std::unordered_set<std::string> seen_names;
    for (const auto& name : names) {
        if (!seen_names.insert(name).second) {
            console::error("duplicate tracked name requested for build: '" + name + "'");
            return 1;
        }
    }

    // Targeted builds are derived-state mutators and take the same ownership:
    // refuse on a live owner or a stale marker (repair is only reachable via
    // `build --repair`).
    ProjectOwnership ownership(root / ".nift/.unfinished");
    const ProjectOwnership::State ownership_state = ownership.acquire();
    if (ownership_state == ProjectOwnership::State::Live) {
        std::lock_guard<std::mutex> lock(console::output_mutex);
        std::cerr << console::error_label() << " another build appears to be running;"
                  << " refusing to mutate build state\n";
        return 1;
    }
    if (ownership_state == ProjectOwnership::State::Failed) {
        std::lock_guard<std::mutex> lock(console::output_mutex);
        std::cerr << console::error_label() << " could not acquire the build lock ("
                  << relative(root / ".nift/.unfinished") << ")\n";
        return 1;
    }
    if (ownership_state == ProjectOwnership::State::Stale) {
        std::lock_guard<std::mutex> lock(console::output_mutex);
        std::cerr << console::error_label() << " unfinished build detected.\n";
        std::cerr << "  Generated build state may be incomplete.\n";
        std::cerr << "  Run " << console::good("nift build --repair") << " to reconstruct it.\n";
        return 1;
    }

    mutation_started_.store(false, std::memory_order_relaxed);

    if (!reconcile_watch()) {
        return finish_if_epoch_complete(ownership, 1, /*repair=*/false);
    }
    reset_build_caches();

    // CP3.2 lifetime invariant: TrackedInfo* jobs are obtained ONLY after the
    // last operation that can structurally modify `tracked` for this pass
    // (reconcile_watch above). This is what makes the pointers valid for
    // build_many, and it also preserves the pre-CP3 semantic that watch
    // reconciliation runs before targeted-name resolution (so a newly
    // discovered watched page can be targeted, and a disappeared one is
    // reported rather than dereferenced).
    std::vector<BuildJob> jobs;
    for (const auto& name : names) {
        if (TrackedInfo* info = find(name)) jobs.push_back({info, {}});
        else {
            std::lock_guard<std::mutex> lock(console::output_mutex);
            std::cout << "not tracking:\n " << name << '\n';
        }
    }
    if (jobs.empty()) {
        // Zero-derived-mutation controlled failure - unless reconcile itself
        // performed a recovery-relevant deletion (mutation_started true), in
        // which case the completion rule correctly retains the marker.
        return finish_if_epoch_complete(ownership, 1, /*repair=*/false);
    }

    const std::string mode = "names";
    { std::string hook_error;
      if (!nift_hooks::run_project_hooks(root, config, "pre", mode, hook_error)) {
          std::lock_guard<std::mutex> lock(console::output_mutex);
          std::cerr << console::error_label() << ' ' << hook_error << '\n';
          return finish_if_epoch_complete(ownership, 1, /*repair=*/false);
      }
    }
    current_hook_mode_ = mode;

    const int result = build_many(jobs, true, explain, names.size());
    if (result == 0) {
        std::string hook_error;
        if (!nift_hooks::run_project_hooks(root, config, "post", mode, hook_error)) {
            std::lock_guard<std::mutex> lock(console::output_mutex);
            std::cerr << console::error_label() << ' ' << hook_error << '\n';
            return finish_if_epoch_complete(ownership, 1, /*repair=*/false);
        }
    }
    return finish_if_epoch_complete(ownership, result, /*repair=*/false);
}

bool ProjectInfo::reconcile_watch() { return watch_->reconcile(*this); }
