#pragma once
#include "Types.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace json { class Document; }

// Read-only snapshot of a Nift project's rendering facts: .nift/config.json
// and .nift/tracked.json plus the shared path geometry and read caches.
//
// This is the SSR (project-aware Engine) view of a project. Unlike ProjectInfo
// it owns no build machinery by construction: there is no save_tracking, no
// .info.json writer, no hash tracking, no watch list, no build decision. It
// consumes project knowledge for rendering and can never mutate the project.
//
// Validation mirrors ProjectInfo's read semantics exactly (same accept/reject
// rules), but errors are returned rather than printed: an embedded consumer
// must be able to turn a missing/malformed project into a controlled error.
//
// open() is transactional: on success a complete validated snapshot is
// installed; on failure the object is left empty/unopened (no partial config
// or tracked registry from the failed candidate is observable) and is valid
// for another open().
class ProjectState {
public:
    ProjectState() = default;
    ProjectState(const ProjectState&) = delete;
    ProjectState& operator=(const ProjectState&) = delete;
    ~ProjectState();

    // Opens and validates the snapshot rooted at `root`. On any failure
    // (missing/malformed config or tracking, invalid entries, duplicate names,
    // conflicting paths) returns false with a non-empty `error` and leaves the
    // object empty/unopened. Never writes to disk.
    bool open(const std::filesystem::path& root, std::string& error);

    // Registry access. `tracked` preserves tracked.json order; `find` is the
    // same tracked-name lookup ProjectInfo performs.
    const std::filesystem::path& root() const { return root_; }
    const Config& config() const { return config_; }
    const std::vector<TrackedInfo>& tracked() const { return tracked_; }
    const TrackedInfo* find(const std::string& name) const;

    // Shared path geometry, identical to ProjectInfo.
    std::filesystem::path content_path(const TrackedInfo& info) const;
    std::filesystem::path output_path(const TrackedInfo& info) const;
    std::filesystem::path pagination_output_path(const TrackedInfo& info, std::size_t page) const;
    std::string relative(const std::filesystem::path& path) const;

    // Thread-safe shared source/JSON reads with the same caching ProjectInfo
    // uses for rendering. Never write to the paths read here.
    const std::string* read_shared_source(const std::filesystem::path& path) const;
    std::shared_ptr<const json::Document> read_shared_json(const std::filesystem::path& path, std::string& error) const;

private:
    void reset();

    std::filesystem::path root_;
    Config config_;
    std::vector<TrackedInfo> tracked_;
    mutable std::unordered_map<std::string, std::size_t> tracked_index_;

    mutable std::mutex source_cache_mutex_;
    mutable std::unordered_map<std::string, std::unique_ptr<const std::string>> shared_source_cache_;
    mutable std::mutex json_cache_mutex_;
    mutable std::unordered_map<std::string, std::shared_ptr<const json::Document>> shared_json_cache_;
};
