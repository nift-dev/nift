#pragma once
#include "ProjectOwnership.h"
#include "Types.h"
#include "HierarchyIndex.h"
#include <atomic>
#include <filesystem>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

class WatchList;
namespace niftid { struct Catalogue; }
namespace filesystem { struct DependencyStatus; struct DependencyTime; }
namespace json { class Document; }
namespace nift { class RuntimeValue; }
namespace content_model { struct Model; }

// feature-site-dx: site-check results recorded in a page's build metadata.
struct PageSiteRecord {
    std::size_t unknown_values = 0;          // literal unknown $[...] values
    std::vector<std::string> warnings;       // "file:line:col: message [kind]"
};

class ProjectInfo {
public:
    // Runtime 2.0 CP02: listing fingerprints computed at most once per build
    // or status run (cleared by reset_build_caches).
    std::string listing_fingerprint_cached(const std::string& key) const;
    std::filesystem::path root;
    Config config;
    std::vector<TrackedInfo> tracked;
    bool has_build_dependencies = false;
    std::string target_ = "native";

    ProjectInfo();
    ~ProjectInfo();

    bool open();
    bool load_config();
    bool load_tracking();
    bool save_tracking() const;

    // Build-scoped, thread-safe lazy copy of the read-only project model
    // (project.files / content / schemas / taxonomies). Built at most once per
    // build and shared by every page render instead of being reconstructed per
    // page (which was quadratic in the number of tracked files).
    std::shared_ptr<const nift::RuntimeValue> runtime_project_value() const;
    void refresh_project_fingerprint() const;
    std::shared_ptr<const content_model::Model> content_model_value() const;
    std::shared_ptr<const HierarchyIndex> hierarchy_index() const;
    void refresh_hierarchy_fingerprint() const;

    TrackedInfo* find(const std::string& name);
    const TrackedInfo* find(const std::string& name) const;
    bool conflicts_with_tracked_path(const TrackedInfo& candidate, const std::string& ignored_name = {}) const;
    void invalidate_tracked_index();

    std::filesystem::path content_path(const TrackedInfo& info) const;
    std::filesystem::path output_path(const TrackedInfo& info) const;
    std::filesystem::path pagination_output_path(const TrackedInfo& info, std::size_t page) const;
    std::filesystem::path info_path(const TrackedInfo& info) const;
    std::string relative(const std::filesystem::path& path) const;
    const std::string* read_shared_source(const std::filesystem::path& path) const;
    std::uint64_t observed_source_hash(const std::string* bytes) const;
    std::shared_ptr<const json::Document> read_shared_json(const std::filesystem::path& path, std::string& error) const;
    std::shared_ptr<const nift::RuntimeValue> read_shared_runtime_json(const std::filesystem::path& path, std::string& error) const;

    std::vector<std::string> build_reasons(const TrackedInfo& info) const;
    bool needs_build(const TrackedInfo& info, std::string* reason = nullptr) const;
    bool build_one(TrackedInfo& info, std::optional<BuildError>* out_error = nullptr, std::vector<std::string>* identities = nullptr);
    int build_all(bool force, bool explain = false, bool repair = false);
    int build_names(const std::vector<std::string>& names, bool force, bool explain = false);

    // Build-mode hint for v4.4 build hooks. The CLI sets "auto" for the watch
    // loop; other modes are derived from the build entry point itself.
    std::string build_mode_hint_ = "updated";
    // Effective hook mode for the current build invocation (file hooks read it
    // inside build_many).
    std::string current_hook_mode_ = "updated";

    // Page names built by the most recent build_all/build_names/build_many
    // call, exposed to the structured automation API.
    std::vector<std::string> last_build_affected;

    // Central epoch-completion rule (CP3.1, D-020): every controlled exit
    // after ownership acquisition routes through this and returns the command
    // exit code. The marker records uncertainty, not failure: it is cleared on
    // success AND on an ordinary failure (failed pages carry a failure record,
    // succeeded pages committed normally), and kept only when a mutation could
    // not be brought back to a consistent state (unresolved_mutation()). A
    // repair additionally needs every page settled (rebuilt or carrying a
    // failure record) and its required sweep to converge.
    int finish_if_epoch_complete(ProjectOwnership& ownership, int result, bool repair);
    // Body of build_all once ownership is held (also the automatic recovery
    // path: a stale marker runs it with force+repair).
    int build_all_owned(ProjectOwnership& ownership, bool force, bool explain, bool repair);

    // CP4 repair sweep: called after a successful repair rebuild. Removes only
    // derived artifacts Nift can establish as its own: pagination surplus of
    // currently-paginated tracked pages (REQUIRED, failures propagate) and
    // orphan .info.json metadata (REQUIRED); stale stored hashes are
    // best-effort cache hygiene. Historical public outputs of removed pages are
    // PRESERVED (their paths are only knowable from distrustable derived
    // metadata - documented limitation). Returns false if a required sweep
    // operation fails (repair must not certify convergence).
    bool repair_derived_state();

    // CP2.2 monotonic flag set BEFORE any recovery-relevant derived mutation
    // (output write, script run, reconcile deletion). Since D-020 it no longer
    // decides the marker by itself: a failed page is settled per page
    // (settle_failed_page), and only a reconcile failure after deletions turns
    // it into an unresolved mutation.
    void mark_mutation() { mutation_started_.store(true, std::memory_order_relaxed); }
    bool mutation_started() const { return mutation_started_.load(std::memory_order_relaxed); }
    // D-020: set when a derived-state mutation could not be brought back to a
    // consistent state (a Nift-owned write failed, a failure record or output
    // restore could not be written, reconcile failed after removing files).
    // Only this keeps .nift/.unfinished after a controlled exit.
    void mark_unresolved() { unresolved_mutation_.store(true, std::memory_order_relaxed); }
    bool unresolved_mutation() const { return unresolved_mutation_.load(std::memory_order_relaxed); }

    bool reconcile_watch();
    WatchList& watch_list();

private:
    static std::string project_fingerprint_of(const json::Document& model);
    void write_project_fingerprint(const std::string& fingerprint) const;
    void invalidate_project_value() const;
    void invalidate_content_model() const;
    void invalidate_hierarchy() const;

    std::unique_ptr<niftid::Catalogue> identity_catalogue_;
    bool identity_reload_pending_ = false;
    void reload_identity_catalogue();
    bool metadata_path_is_safe_prepared(unsigned id) const;
    WatchList* watch_ = nullptr;
    mutable std::mutex project_value_mutex_;
    mutable std::shared_ptr<const nift::RuntimeValue> runtime_project_value_;
    mutable std::string project_fingerprint_;
    mutable std::mutex content_model_mutex_;
    mutable std::shared_ptr<const content_model::Model> content_model_value_;
    mutable std::mutex hierarchy_mutex_;
    mutable std::shared_ptr<const HierarchyIndex> hierarchy_;
    std::atomic<bool> mutation_started_{false};
    std::atomic<bool> unresolved_mutation_{false};
    // True once build_many processed every job (each one succeeded, failed
    // with a failure record, or was blocked with one); repair needs it.
    bool last_build_settled_ = false;
    struct PageAttempt;
    bool build_one_attempt(TrackedInfo& info, std::optional<BuildError>* out_error, std::vector<std::string>* identities, PageAttempt& attempt);
    // Leaves a failed page consistent and dirty: restores last-good outputs
    // of scripted pages, writes the failure record, marks unresolved on error.
    void settle_failed_page(const TrackedInfo& info, const PageAttempt& attempt);
    // D-020 failed-page list (.nift/failed.json): names of tracked pages whose
    // last build failed (or was blocked by a failed prerequisite). Their
    // page-info and last-good outputs are left untouched; the list only makes
    // them dirty
    // ("previous build of this page failed") whatever selected them. Absent
    // when empty, so a healthy project pays one stat per command.
    std::unordered_set<std::string> failed_pages_;
    bool failed_pages_dirty_ = false;
    std::mutex failed_mutex_;
    std::unordered_set<std::string> failed_now_;
    std::filesystem::path failed_pages_path() const;
    void load_failed_pages();
    // Merges this build's outcome into the list on disk and rewrites it.
    bool save_failed_pages(const std::unordered_set<std::string>& succeeded,
                           const std::unordered_set<std::string>& failed);
public:
    // feature-host-deps (D-006 decision): the tracked page that owns `path` as
    // a build output (its output file or one of its pagination outputs), or
    // nullopt. `path` must already be canonical (std::filesystem::
    // weakly_canonical); owner outputs are canonicalised the same way, so a
    // symlinked output directory or an alternative spelling still matches.
    std::optional<std::string> output_owner(const std::filesystem::path& canonical_path) const;
    // Drops untracked names (and removes the list when it becomes empty);
    // run on every build and by untrack/rm/mv. False on a write failure.
    bool prune_failed_pages();
private:
    mutable std::unordered_map<std::string, std::size_t> tracked_index_;
    mutable std::unordered_set<std::string> tracked_output_index_;
    mutable bool tracked_output_index_valid_ = false;
    mutable std::mutex tracked_output_index_mutex_;
    struct PaginatedOwner { std::string directory, prefix, extension, name; };
    mutable std::unordered_map<std::string, std::string> output_owner_index_;
    mutable std::vector<PaginatedOwner> paginated_owner_index_;
    mutable bool output_owner_index_valid_ = false;
    mutable std::size_t tracked_index_size_ = static_cast<std::size_t>(-1);
    mutable std::mutex hash_mutex_;
    mutable std::unordered_map<std::string, std::uint64_t> current_hash_cache_;
    std::unordered_set<std::string> refreshed_hashes_;
    mutable std::mutex source_cache_mutex_;
    mutable std::unordered_map<const std::string*,std::uint64_t> observed_source_hashes_;
    mutable std::unordered_map<std::string, std::unique_ptr<const std::string>> shared_source_cache_;
    std::vector<std::unique_ptr<const std::string>> retired_sources_;
    mutable std::mutex json_cache_mutex_;
    mutable std::unordered_map<std::string, std::shared_ptr<const json::Document>> shared_json_cache_;
    mutable std::unordered_map<std::string, std::shared_ptr<const nift::RuntimeValue>> shared_runtime_json_cache_;
    mutable std::mutex metadata_path_mutex_;
    mutable std::unordered_map<std::string, bool> metadata_parent_safety_cache_;
    std::mutex build_output_mutex_;

    struct BuildJob {
        TrackedInfo* info = nullptr;
        std::vector<std::string> reasons;
        bool validation_only = false;
    };

    void rebuild_tracked_index() const;
    bool is_tracked_output(const std::filesystem::path& path) const;
    struct SidecarObservation;
    bool cached_user_dependencies_match(const TrackedInfo& info, const json::Document& document) const;
    bool load_user_dependencies(const TrackedInfo& info, std::set<std::string>& dependencies, BuildError* error = nullptr, SidecarObservation* observation = nullptr) const;
    bool dependency_changed(const std::filesystem::path& dependency, const filesystem::DependencyStatus& status, const filesystem::DependencyStatus& info_status, const json::Document& snapshots, const std::string& name) const;
    // Inputs are lexically normalized within one consumer dirty-check.
    bool metadata_path_is_safe(const std::filesystem::path& normalized, const std::filesystem::path& normalized_root) const;
    std::uint64_t current_hash_cached(const std::filesystem::path& dependency) const;
    void reset_build_caches();
    void invalidate_output_caches(const std::filesystem::path& path);
    void refresh_hash_once(const std::filesystem::path& dependency);
    bool write_page_info(const TrackedInfo& info, const std::set<std::string>& dependencies, const std::set<std::string>& reqs, std::size_t pagination_pages, const std::map<std::string,std::string>& snapshots, const SidecarObservation* observation = nullptr,
                         const ReadObservations* observed = nullptr,
                         const struct PageSiteRecord* site = nullptr) const;
    // feature-site-dx (review R1): warnings recorded in page metadata for
    // pages that were not rebuilt, collected by build_reasons().
    mutable std::mutex outstanding_warning_mutex_;
    mutable std::map<std::string, std::vector<std::string>> outstanding_warnings_;
public:
    // Outstanding warnings of pages as recorded at their last build,
    // filtered by the current no-warn settings; filled by build_reasons().
    std::map<std::string, std::vector<std::string>> outstanding_warnings() const;
    void print_outstanding_warning_summary(const std::set<std::string>& rebuilt) const;
private:
    void append_read_observations(std::string& output, const ReadObservations& observed) const;
    void read_observation_reasons(const json::Document& document, std::vector<std::string>& reasons) const;
    mutable std::mutex listing_cache_mutex_;
    mutable std::unordered_map<std::string, std::string> listing_cache_;
    void print_build_error(const BuildError& error) const;
    // feature-site-dx: template build warnings, buffered per page and printed
    // after the progress line (like build errors), in job order.
    mutable std::mutex site_warning_mutex_;
    std::map<std::string, std::vector<BuildWarning>> site_warnings_by_page_;
    void print_build_warning(const BuildWarning& warning) const;
    void report_build_error(const BuildError& error, std::optional<BuildError>* out_error) const;
    int build_many(const std::vector<BuildJob>& initial_jobs, bool targeted, bool full_detail, std::size_t requested_count);
};
