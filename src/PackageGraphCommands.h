#pragma once

// Batch 5 CP12: real package acquisition provider + atomic graph acquisition.
// Resolves the complete graph (CP11), stages every required store change into
// the PackageTransaction staging root, and produces the desired manifest + v2
// graph lock + deterministic operation list. Nothing is published here;
// PackageTransaction::commit performs the single atomic publication.
#include "FileSystem.h"
#include "JsonFile.h"
#include "PackageGraphResolver.h"
#include "PackageTransaction.h"
#include "Proc.h"

#include <map>
#include <set>
#include <string>
#include <vector>

namespace package_graph_commands {

namespace fs = std::filesystem;

// ---- git acquisition primitives (single shared implementation) ----
inline bool git_run(const std::vector<std::string>& args, const fs::path& cwd, ProcessResult& r) {
    ProcessSpec s; s.program = "git"; s.args = args; s.cwd = cwd;
    r = nift_run_process(s, true, false);
    return r.launched && r.exit_code == 0;
}

inline bool git_checkout(const std::string& source, const std::string& ref,
                         const fs::path& dest, std::string& commit, std::string& error) {
    if (!package_metadata::safe_external_text(source) || !package_metadata::safe_external_text(ref)) {
        error = "package source and ref must be non-empty safe strings"; return false;
    }
    std::error_code ec; fs::remove_all(dest, ec); fs::create_directories(dest.parent_path(), ec);
    ProcessResult r;
    if (!git_run({"clone", "--quiet", "--", package_metadata::git_source(source), dest.string()}, fs::current_path(), r)) {
        error = r.error.empty() ? r.err : r.error; return false;
    }
    if (ref != "latest" && ref != "latest-tag") {
        if (!git_run({"checkout", "--quiet", "--detach", ref}, dest, r)) { error = r.err; fs::remove_all(dest, ec); return false; }
    }
    if (ref == "latest-tag") {
        if (!git_run({"tag", "--list", "v*"}, dest, r)) { error = r.err; fs::remove_all(dest, ec); return false; }
        std::istringstream tags(r.out); std::string tag, best, best_version;
        while (std::getline(tags, tag)) {
            if (tag.size() < 2 || tag.front() != 'v' || !package_metadata::valid_semver(tag.substr(1))) continue;
            const std::string version = tag.substr(1);
            if (best.empty() || package_metadata::compare_semver(version, best_version) > 0) { best = tag; best_version = version; }
        }
        if (best.empty()) { error = "no semantic-version tag found"; fs::remove_all(dest, ec); return false; }
        if (!git_run({"checkout", "--quiet", "--detach", best}, dest, r)) { error = r.err; fs::remove_all(dest, ec); return false; }
    }
    if (!git_run({"rev-parse", "HEAD"}, dest, r)) { error = r.err; return false; }
    commit = r.out;
    while (!commit.empty() && (commit.back() == '\n' || commit.back() == '\r')) commit.pop_back();
    if (!git_run({"checkout", "--quiet", "--detach", commit}, dest, r)) { error = r.err; return false; }
    return true;
}

inline bool stage_local_package(const fs::path& source, const fs::path& staged, std::string& error) {
    std::error_code ec; fs::create_directory_symlink(source, staged, ec);
    if (!ec) return true;
#ifdef _WIN32
    const std::string symlink_error = ec.message(); ec.clear();
    fs::copy(source, staged, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
    if (!ec) return true;
    error = "cannot stage local package: " + symlink_error + " / " + ec.message();
#else
    error = "cannot stage local package: " + ec.message();
#endif
    return false;
}

inline bool detached_git_head(const fs::path& package, const std::string& expected) {
    const auto head = filesystem::read_file_checked(package / ".git" / "HEAD");
    if (!head) return false;
    std::string value = *head;
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value == expected;
}

inline bool exists_no_follow(const fs::path& path) {
    std::error_code error;
    return fs::symlink_status(path, error).type() != fs::file_type::not_found && !error;
}

// ---- real ResolutionProvider ----
// Stages git packages into <staging_root>/new/<name> the first time their
// ref is resolved (memoized by the resolver); local packages are read from
// their canonical absolute source. Read-oriented only; never publishes.
class RealProvider : public package_graph::ResolutionProvider {
public:
    explicit RealProvider(const fs::path& staging_root) : staging_root_(staging_root) {}

    bool resolve_ref(const package_graph::Requirement& req, std::string& commit, std::string& error) override {
        const fs::path dest = staging_root_ / "new" / req.name;
        // Reuse an already-staged clone only when it was produced for the same
        // requested ref (the add command pre-stages the new root to discover
        // its name; the resolver memoizes per (canonical, requested)). A
        // different ref for the same name must re-clone so the correct commit
        // is observed (otherwise a conflict between two refs would be missed).
        bool reuse = exists_no_follow(dest);
        if (reuse) {
            const auto previous = staged_refs_.find(req.name);
            if (previous != staged_refs_.end() && previous->second != req.requested) reuse = false;
        }
        if (reuse) {
            package_metadata::Manifest existing; fs::path entry;
            std::string load_error;
            if (!package_metadata::load_package(dest, existing, entry, load_error) || existing.name != req.name) reuse = false;
        }
        if (reuse) {
            const auto head = filesystem::read_file_checked(dest / ".git" / "HEAD");
            if (!head) { error = "staged git package is missing HEAD: " + req.name; return false; }
            commit = *head;
            while (!commit.empty() && (commit.back() == '\n' || commit.back() == '\r')) commit.pop_back();
            staged_refs_[req.name] = req.requested;
            return true;
        }
        std::string clone_error;
        if (!git_checkout(req.source, req.requested, dest, commit, clone_error)) {
            error = "package clone failed: " + clone_error; return false;
        }
        staged_refs_[req.name] = req.requested;
        return true;
    }

    bool load_package_manifest(const package_graph::Requirement& req, const std::string& canonical,
                               const std::string&, fs::path& manifest_dir,
                               package_metadata::Manifest& manifest, std::string& error) override {
        if (package_metadata::source_is_local_path(canonical)) {
            manifest_dir = fs::path(canonical);
        } else {
            manifest_dir = staging_root_ / "new" / req.name;
        }
        if (!package_metadata::load_manifest(manifest_dir / "manifest.json", true, manifest, error)) {
            if (error.empty()) error = "package manifest could not be loaded: " + req.name;
            return false;
        }
        return true;
    }

private:
    fs::path staging_root_;
    std::map<std::string, std::string> staged_refs_;
};

// ---- existing lock -> GraphLock view ----
inline bool existing_lock_graph(const json::Document* v2_doc, const package_metadata::Lock* v1_lock,
                                package_graph::GraphLock& out, std::string& error) {
    if (v2_doc) return package_graph::parse_graph_lock(*v2_doc, out, error);
    if (v1_lock) {
        for (const auto& item : *v1_lock) {
            package_graph::GraphNode node;
            node.source = item.second.source;
            node.commit = item.second.commit;
            out.packages[item.first] = std::move(node);
        }
    }
    return true;
}

// The reachable subgraph of an existing v2 graph lock under the given root
// manifest, preserving identities and edges. Used by `remove` so retained
// nodes keep their locked identities and orphans drop out deterministically.
inline bool prune_locked_graph(const package_metadata::Manifest& manifest,
                               const package_graph::GraphLock& locked,
                               package_graph::GraphLock& out, std::string& error) {
    std::set<std::string> reachable;
    std::vector<std::string> stack;
    for (const auto& dep : manifest.dependencies) stack.push_back(dep.first);
    while (!stack.empty()) {
        const std::string name = stack.back(); stack.pop_back();
        if (!reachable.insert(name).second) continue;
        const auto it = locked.packages.find(name);
        if (it == locked.packages.end()) { error = "package graph lock is missing root dependency: " + name; return false; }
        for (const auto& edge : it->second.requirements) stack.push_back(edge.first);
    }
    for (const auto& name : reachable) {
        const auto it = locked.packages.find(name);
        if (it == locked.packages.end()) { error = "package graph lock is missing reachable node: " + name; return false; }
        out.packages[name] = it->second;
    }
    if (!package_graph::validate_graph_lock(manifest, out, error)) return false;
    return true;
}

struct AcquireResult {
    json::Document manifest_document;
    json::Document lock_document;
    std::vector<PackageTransaction::Operation> operations;
    std::vector<std::string> reuse_names;
    package_graph::GraphLock graph;
    bool used_locked_identity = false;
};

// Resolve the desired graph, stage every required store change into the
// transaction staging root, and compute the deterministic operation list.
// mode: LockedInstall (authoritative v2 lock, offline identity decisions),
// ResolveUpdate (full re-resolution), or TargetedUpdate (target root only).
// root_locked_commits: optional map of root name -> exact commit to preserve
// (used when installing against a v1 lock). Never publishes anything.
inline bool acquire_graph(const fs::path& project,
                          const fs::path& staging_root,
                          const package_metadata::Manifest& manifest,
                          const package_metadata::Lock* v1_lock,
                          const json::Document* v2_lock_doc,
                          package_graph::ResolveMode mode,
                          const std::string* target_root,
                          const std::map<std::string, std::string>* root_locked_commits,
                          bool prune,
                          AcquireResult& out, std::string& error) {
    package_graph::GraphLock old_graph;
    if (!existing_lock_graph(v2_lock_doc, v1_lock, old_graph, error)) return false;

    package_graph::GraphLock graph;
    out.used_locked_identity = false;
    if (prune) {
        if (v2_lock_doc) {
            if (!prune_locked_graph(manifest, old_graph, graph, error)) return false;
            out.used_locked_identity = true;
        } else {
            // v1 lock: re-resolve the remaining roots, preserving their locked
            // direct commits via root_locked_commits.
            package_graph::ResolveOutcome resolve_out;
            RealProvider provider(staging_root);
            if (!package_graph::resolve_graph(manifest, project, package_graph::ResolveMode::ResolveUpdate,
                                              nullptr, nullptr, root_locked_commits, provider, resolve_out, error)) return false;
            graph = resolve_out.graph;
            out.used_locked_identity = resolve_out.used_locked_identity;
        }
    } else {
        package_graph::ResolveOutcome resolve_out;
        RealProvider provider(staging_root);
        const package_graph::GraphLock* locked_ptr = nullptr;
        if (mode == package_graph::ResolveMode::LockedInstall || mode == package_graph::ResolveMode::TargetedUpdate) {
            locked_ptr = &old_graph;
        }
        if (!package_graph::resolve_graph(manifest, project, mode, locked_ptr, target_root,
                                          root_locked_commits, provider, resolve_out, error)) return false;
        graph = resolve_out.graph;
        out.used_locked_identity = resolve_out.used_locked_identity;
    }
    out.graph = graph;

    // Operation plan: old vs desired vs actual store.
    std::set<std::string> replace_names;
    for (const auto& node : graph.packages) {
        const auto old = old_graph.packages.find(node.first);
        bool reuse = false;
        if (old != old_graph.packages.end() && old->second.source == node.second.source &&
            old->second.commit == node.second.commit) {
            const fs::path live = project / ".nift" / "packages" / node.first;
            if (exists_no_follow(live)) {
                package_metadata::Manifest m; fs::path entry;
                std::string load_error;
                if (package_metadata::load_package(live, m, entry, load_error) && m.name == node.first) {
                    if (node.second.commit == "local" || detached_git_head(live, node.second.commit)) reuse = true;
                }
            }
        }
        if (reuse) {
            out.reuse_names.push_back(node.first);
        } else {
            replace_names.insert(node.first);
            // stage
            if (node.second.commit == "local") {
                if (!stage_local_package(fs::path(node.second.source), staging_root / "new" / node.first, error)) return false;
            } else {
                const fs::path staged = staging_root / "new" / node.first;
                if (!exists_no_follow(staged)) {
                    std::string commit;
                    if (!git_checkout(node.second.source, node.second.commit, staged, commit, error)) return false;
                }
            }
        }
    }
    for (const auto& name : replace_names) out.operations.push_back({name, PackageTransaction::Kind::Replace});
    for (const auto& node : old_graph.packages) {
        if (!graph.packages.count(node.first)) out.operations.push_back({node.first, PackageTransaction::Kind::Remove});
    }

    out.manifest_document = package_metadata::manifest_document(manifest);
    out.lock_document = package_graph::graph_lock_document(graph);
    return true;
}

inline bool acquire_graph(const fs::path& project,
                          const fs::path& staging_root,
                          const package_metadata::Manifest& manifest,
                          const package_metadata::Lock* v1_lock,
                          const json::Document* v2_lock_doc,
                          package_graph::ResolveMode mode,
                          const std::string* target_root,
                          const std::map<std::string, std::string>* root_locked_commits,
                          AcquireResult& out, std::string& error) {
    return acquire_graph(project, staging_root, manifest, v1_lock, v2_lock_doc, mode,
                         target_root, root_locked_commits, false, out, error);
}

}  // namespace package_graph_commands