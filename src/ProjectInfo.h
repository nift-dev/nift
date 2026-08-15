#pragma once
#include "Types.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

class WatchList;
namespace json { class Document; }

class ProjectInfo {
public:
    std::filesystem::path root;
    Config config;
    std::vector<TrackedInfo> tracked;

    ProjectInfo();
    ~ProjectInfo();

    bool open();
    bool load_config();
    bool load_tracking();
    bool save_tracking() const;

    TrackedInfo* find(const std::string& name);
    const TrackedInfo* find(const std::string& name) const;
    bool conflicts_with_tracked_path(const TrackedInfo& candidate, const std::string& ignored_name = {}) const;
    void invalidate_tracked_index();

    std::filesystem::path content_path(const TrackedInfo& info) const;
    std::filesystem::path output_path(const TrackedInfo& info) const;
    std::filesystem::path info_path(const TrackedInfo& info) const;
    std::string relative(const std::filesystem::path& path) const;
    const std::string* read_shared_source(const std::filesystem::path& path) const;
    std::shared_ptr<const json::Document> read_shared_json(const std::filesystem::path& path, std::string& error) const;

    std::vector<std::string> build_reasons(const TrackedInfo& info) const;
    bool needs_build(const TrackedInfo& info, std::string* reason = nullptr) const;
    bool build_one(TrackedInfo& info);
    int build_all(bool force, bool explain = false);
    int build_names(const std::vector<std::string>& names, bool force, bool explain = false);

    bool reconcile_watch();
    WatchList& watch_list();

private:
    WatchList* watch_ = nullptr;
    mutable std::unordered_map<std::string, std::size_t> tracked_index_;
    mutable std::size_t tracked_index_size_ = static_cast<std::size_t>(-1);
    mutable std::mutex hash_mutex_;
    mutable std::unordered_map<std::string, bool> hash_change_cache_;
    std::unordered_set<std::string> refreshed_hashes_;
    mutable std::mutex source_cache_mutex_;
    mutable std::unordered_map<std::string, std::unique_ptr<const std::string>> shared_source_cache_;
    mutable std::mutex json_cache_mutex_;
    mutable std::unordered_map<std::string, std::shared_ptr<const json::Document>> shared_json_cache_;
    std::mutex build_output_mutex_;

    struct BuildJob {
        TrackedInfo* info = nullptr;
        std::vector<std::string> reasons;
    };

    void rebuild_tracked_index() const;
    bool load_user_dependencies(const TrackedInfo& info, std::set<std::string>& dependencies, BuildError* error = nullptr) const;
    bool dependency_changed(const std::filesystem::path& dependency, const std::filesystem::path& info_path) const;
    bool hash_changed_cached(const std::filesystem::path& dependency) const;
    void reset_build_caches();
    void refresh_hash_once(const std::filesystem::path& dependency);
    bool write_page_info(const TrackedInfo& info, const std::set<std::string>& dependencies, const std::set<std::string>& reqs) const;
    void print_build_error(const BuildError& error) const;
    int build_many(const std::vector<BuildJob>& jobs, bool targeted, bool full_detail, std::size_t requested_count);
};
