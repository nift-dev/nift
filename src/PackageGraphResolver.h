#pragma once

// Deterministic transitive package graph resolver (Batch 5 CP11).
// Compute-only: produces a resolved GraphLock and deterministic diagnostics
// with NO mutation of the package store, manifest, lock, or transaction
// journal. All acquisition/ref resolution is mediated through the read-only
// ResolutionProvider abstraction so unit tests can drive the entire
// algorithm with a fake provider. Contract source:
// docs/handover/V4.6-BATCH-5-GRAPH-CONTRACTS.md and
// docs/handover/V4.6-BATCH-5-DEPENDENCY-GRAPH-DESIGN.md.

#include "PackageGraphLock.h"
#include "PackageMetadata.h"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace package_graph {

enum class ResolveMode { LockedInstall, ResolveUpdate, TargetedUpdate };

// One package requirement edge: the target package name, the requirement
// source spelling and requested ref as declared by the owner manifest, and
// the owner manifest directory (absolute) that relative local sources are
// resolved against.
struct Requirement {
    std::string name;
    std::string source;
    std::string requested;
    std::filesystem::path owner_dir;
};

// Read-only acquisition abstraction. The resolver never mutates the store;
// it only asks the provider to resolve a ref to an exact commit and to load a
// resolved package's manifest. In LockedInstall mode neither is called for
// ref resolution (offline contract); the provider is expected to fail loudly
// if resolve_ref is invoked in that mode (tests assert this).
class ResolutionProvider {
public:
    virtual ~ResolutionProvider() = default;

    // Resolve a requirement's ref to an exact commit (or "local" for a local
    // source, which never requires this call in practice).
    virtual bool resolve_ref(const Requirement& requirement, std::string& commit,
                             std::string& error) = 0;

    // Load the manifest of a resolved package (read-only). Fills manifest and
    // the absolute directory containing it (the owner for its own relative
    // local dependencies). Must not be called in LockedInstall mode.
    virtual bool load_package_manifest(const Requirement& requirement,
                                       const std::string& canonical_source,
                                       const std::string& commit,
                                       std::filesystem::path& manifest_dir,
                                       package_metadata::Manifest& manifest,
                                       std::string& error) = 0;
};

struct ResolveOutcome {
    GraphLock graph;
    bool used_locked_identity = false;  // some node came from the existing lock
};

namespace detail {

inline std::string canonical_source(const Requirement& requirement, std::string& error) {
    if (package_metadata::source_is_local_path(requirement.source)) {
        std::filesystem::path local;
        if (!local_source_absolute(requirement.owner_dir, requirement.source, local, error)) {
            if (error.empty()) error = "cannot resolve local source: " + requirement.source;
            return {};
        }
        return local.generic_string();
    }
    return package_metadata::git_source(requirement.source);
}

}  // namespace detail

// Resolve the root manifest requirements into a deterministic GraphLock.
//
// Modes:
//   LockedInstall    existing_lock is required; the locked identities and
//                    edges are authoritative; no ref resolution and no
//                    manifest loading occurs (offline).
//   ResolveUpdate    refs are resolved through the provider (memoized);
//                    existing_lock may be null.
//   TargetedUpdate   target_root names a root/direct dependency to
//                    re-resolve; unrelated root nodes and their reachable
//                    closure are preserved at their locked identities; shared
//                    nodes are reconciled globally (conflict => error).
// Overload with root_locked_commits: for a root/direct dependency whose name
// appears in root_locked_commits, its exact commit is taken from the map
// (preserving an existing lock's direct identity) instead of re-resolving the
// ref; the original requested ref is still recorded on the edge. Used by
// `install` migration from a v1 lock.
inline bool resolve_graph(const package_metadata::Manifest& root_manifest,
                          const std::filesystem::path& root_dir,
                          ResolveMode mode,
                          const GraphLock* existing_lock,
                          const std::string* target_root,
                          const std::map<std::string, std::string>* root_locked_commits,
                          ResolutionProvider& provider,
                          ResolveOutcome& outcome,
                          std::string& error) {
    GraphLock graph;
    std::map<std::string, std::vector<std::string>> node_paths;
    std::vector<std::string> visiting;
    std::set<std::string> visiting_set;
    std::map<std::pair<std::string, std::string>, std::string> memo;
    std::set<std::string> locked_nodes;
    outcome.used_locked_identity = false;

    if (mode == ResolveMode::TargetedUpdate && (!target_root || target_root->empty())) {
        error = "targeted update requires a root dependency name";
        return false;
    }
    if ((mode == ResolveMode::LockedInstall || mode == ResolveMode::TargetedUpdate) && !existing_lock) {
        error = "this resolver mode requires an existing graph lock";
        return false;
    }

    std::function<bool(const Requirement&, const std::vector<std::string>&, bool, std::string&)>
        dfs = [&](const Requirement& req, const std::vector<std::string>& ancestors,
                  bool resolve_this, std::string& err) -> bool {
        // Determine canonical source and exact commit for this requirement.
        std::string canonical;
        std::string commit;
        if (!resolve_this && existing_lock) {
            const auto locked = existing_lock->packages.find(req.name);
            if (locked == existing_lock->packages.end()) {
                err = "missing locked node for required package: " + req.name;
                return false;
            }
            canonical = locked->second.source;
            commit = locked->second.commit;
            locked_nodes.insert(req.name);
            outcome.used_locked_identity = true;
        } else {
            canonical = detail::canonical_source(req, err);
            if (canonical.empty() && !err.empty()) return false;
            if (package_metadata::source_is_local_path(req.source)) {
                commit = "local";
            } else {
                const auto key = std::make_pair(canonical, req.requested);
                const auto found = memo.find(key);
                if (found != memo.end()) {
                    commit = found->second;
                } else {
                    const bool root_locked =
                        root_locked_commits && ancestors.empty() &&
                        root_locked_commits->count(req.name) != 0;
                    if (root_locked) {
                        commit = root_locked_commits->at(req.name);
                    } else if (!provider.resolve_ref(req, commit, err)) {
                        return false;
                    }
                    memo.emplace(key, commit);
                }
            }
        }

        const std::vector<std::string> incoming = ancestors;
        std::vector<std::string> incoming_path = ancestors;
        incoming_path.push_back(req.name);

        if (visiting_set.count(req.name)) {
            std::string cycle;
            bool in_cycle = false;
            for (const auto& name : visiting) {
                if (name == req.name) in_cycle = true;
                if (in_cycle) { if (!cycle.empty()) cycle += " -> "; cycle += name; }
            }
            err = "dependency cycle: " + cycle + " -> " + req.name;
            return false;
        }

        const auto existing = graph.packages.find(req.name);
        if (existing != graph.packages.end()) {
            if (existing->second.source != canonical || existing->second.commit != commit) {
                std::string existing_path;
                for (std::size_t i = 0; i < node_paths[req.name].size(); ++i) {
                    if (i) existing_path += " -> ";
                    existing_path += node_paths[req.name][i];
                }
                std::string incoming_path_text;
                for (std::size_t i = 0; i < incoming_path.size(); ++i) {
                    if (i) incoming_path_text += " -> ";
                    incoming_path_text += incoming_path[i];
                }
                err = "dependency conflict for package: " + req.name +
                      "; existing path: " + existing_path +
                      "; incoming path: " + incoming_path_text +
                      "; existing source: " + existing->second.source +
                      "; existing commit: " + existing->second.commit +
                      "; incoming source: " + canonical +
                      "; incoming commit: " + commit;
                return false;
            }
            return true;
        }

        GraphNode node;
        node.source = canonical;
        node.commit = commit;
        node_paths[req.name] = incoming_path;
        visiting.push_back(req.name);
        visiting_set.insert(req.name);

        if (!resolve_this && existing_lock) {
            // Preserved/locked node: edges come from the locked graph.
            const auto locked = existing_lock->packages.find(req.name);
            if (locked == existing_lock->packages.end()) {
                err = "missing locked node for required package: " + req.name;
                visiting.pop_back(); visiting_set.erase(req.name);
                return false;
            }
            for (const auto& edge : locked->second.requirements) {
                node.requirements[edge.first] = edge.second;
                Requirement child{edge.first, edge.second.source, edge.second.requested, std::filesystem::path()};
                // For locked edges the owner is not needed (identity comes from
                // the lock), so the child inherits the parent's owner-less
                // context; locked resolution never canonicalizes a local source.
                if (!dfs(child, incoming_path, false, err)) {
                    visiting.pop_back(); visiting_set.erase(req.name);
                    return false;
                }
            }
        } else {
            std::filesystem::path manifest_dir;
            package_metadata::Manifest manifest;
            if (!provider.load_package_manifest(req, canonical, commit, manifest_dir, manifest, err)) {
                visiting.pop_back(); visiting_set.erase(req.name);
                return false;
            }
            if (manifest.name != req.name) {
                err = "package manifest name mismatch: requirement '" + req.name +
                      "' but package manifest declares '" + manifest.name + "'";
                visiting.pop_back(); visiting_set.erase(req.name);
                return false;
            }
            for (const auto& dep : manifest.dependencies) {
                Requirement child{dep.first, dep.second.source, dep.second.ref, manifest_dir};
                if (dep.first == req.name) {
                    // A package declaring a dependency on itself at the same
                    // canonical source is self-provided: it adds no external
                    // requirement, so it is a no-op rather than a cycle. A
                    // genuine multi-node cycle (a -> b -> a) is still rejected.
                    std::string self_error;
                    const std::string self_canonical = detail::canonical_source(child, self_error);
                    if (self_error.empty() && self_canonical == canonical) continue;
                }
                node.requirements[dep.first] =
                    GraphRequirement{dep.second.source, dep.second.ref};
                if (!dfs(child, incoming_path, true, err)) {
                    visiting.pop_back(); visiting_set.erase(req.name);
                    return false;
                }
            }
        }

        visiting.pop_back();
        visiting_set.erase(req.name);
        graph.packages.emplace(req.name, std::move(node));
        return true;
    };

    if (mode == ResolveMode::LockedInstall) {
        // Structural validation of the locked graph against the root manifest;
        // no ref resolution, no manifest loading.
        if (!validate_graph_lock(root_manifest, *existing_lock, error)) return false;
        outcome.graph = *existing_lock;
        outcome.used_locked_identity = true;
        return true;
    }

    std::vector<std::pair<std::string, package_metadata::Dependency>> roots(
        root_manifest.dependencies.begin(), root_manifest.dependencies.end());
    std::sort(roots.begin(), roots.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& root : roots) {
        const Requirement req{root.first, root.second.source, root.second.ref, root_dir};
        const bool resolve_this =
            (mode == ResolveMode::ResolveUpdate) ||
            (mode == ResolveMode::TargetedUpdate && target_root && root.first == *target_root);
        if (!dfs(req, {}, resolve_this, error)) return false;
    }
    outcome.graph = std::move(graph);
    return true;
}

// Convenience overload without root_locked_commits.
inline bool resolve_graph(const package_metadata::Manifest& root_manifest,
                          const std::filesystem::path& root_dir,
                          ResolveMode mode,
                          const GraphLock* existing_lock,
                          const std::string* target_root,
                          ResolutionProvider& provider,
                          ResolveOutcome& outcome,
                          std::string& error) {
    return resolve_graph(root_manifest, root_dir, mode, existing_lock, target_root,
                         static_cast<const std::map<std::string, std::string>*>(nullptr),
                         provider, outcome, error);
}

}  // namespace package_graph