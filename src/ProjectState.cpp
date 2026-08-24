#include "ProjectState.h"
#include "FileSystem.h"
#include "Json.h"
#include "ProjectRead.h"

#include <mutex>

namespace fs = std::filesystem;

ProjectState::~ProjectState() = default;

bool ProjectState::open(const std::filesystem::path& root, std::string& error) {
    // Build the candidate snapshot entirely in locals. Commit only if the whole
    // snapshot validates, so a failure can never expose partial state.
    const fs::path candidate_root = fs::absolute(root).lexically_normal();
    Config candidate_config;
    std::vector<TrackedInfo> candidate_tracked;

    if (!project_read::load_config(candidate_root, candidate_config, error) ||
        !project_read::load_tracking(candidate_root, candidate_config, candidate_tracked, error)) {
        reset();
        return false;
    }

    root_ = candidate_root;
    config_ = std::move(candidate_config);
    tracked_ = std::move(candidate_tracked);
    tracked_index_.clear();
    tracked_index_.reserve(tracked_.size());
    for (std::size_t i = 0; i < tracked_.size(); ++i) tracked_index_[tracked_[i].name] = i;
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        shared_source_cache_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(json_cache_mutex_);
        shared_json_cache_.clear();
    }
    return true;
}

void ProjectState::reset() {
    root_.clear();
    config_ = Config{};
    tracked_.clear();
    tracked_index_.clear();
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        shared_source_cache_.clear();
    }
    {
        std::lock_guard<std::mutex> lock(json_cache_mutex_);
        shared_json_cache_.clear();
    }
}

const TrackedInfo* ProjectState::find(const std::string& name) const {
    const auto it = tracked_index_.find(name);
    return it == tracked_index_.end() ? nullptr : &tracked_[it->second];
}

fs::path ProjectState::content_path(const TrackedInfo& info) const {
    return project_read::content_path_of(root_, config_, info);
}

fs::path ProjectState::output_path(const TrackedInfo& info) const {
    return project_read::output_path_of(root_, config_, info);
}

fs::path ProjectState::pagination_output_path(const TrackedInfo& info, std::size_t page) const {
    return project_read::pagination_output_path_of(root_, config_, info, page);
}

std::string ProjectState::relative(const fs::path& path) const {
    return project_read::relative_of(root_, path);
}

const std::string* ProjectState::read_shared_source(const fs::path& path) const {
    const std::string key = path.lexically_normal().generic_string();
    {
        std::lock_guard<std::mutex> lock(source_cache_mutex_);
        const auto it = shared_source_cache_.find(key);
        if (it != shared_source_cache_.end()) return it->second.get();
    }

    // The read is the authority: nullptr for missing/unreadable/non-regular
    // (only successful reads are cached; the error path re-probes per render).
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

std::shared_ptr<const json::Document> ProjectState::read_shared_json(const fs::path& path, std::string& error) const {
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
