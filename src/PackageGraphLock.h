#pragma once

// Deterministic v2 package graph lock representation (Batch 5 CP10).
// Representation/parse/validate/serialize ONLY. No resolver, no acquisition,
// no command wiring. Existing v1 direct-only lock behavior is unchanged.
// Contract source: docs/handover/V4.6-BATCH-5-GRAPH-CONTRACTS.md.

#include "Json.h"
#include "PackageMetadata.h"

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace package_graph {

// One resolved package node, keyed by package name. A node holds the
// canonical/resolved source identity and exact commit (or "local"); the
// outgoing requirement edges preserve each parent's declared source spelling
// and requested ref. Edge requirements are never collapsed into the node.
struct GraphRequirement {
    std::string source;      // requirement source spelling
    std::string requested;   // requirement ref
};

struct GraphNode {
    std::string source;      // canonical source identity
    std::string commit;      // exact git commit or "local"
    std::map<std::string, GraphRequirement> requirements;
};

struct GraphLock {
    std::map<std::string, GraphNode> packages;  // one node per package name
};

enum class LockFormat { Invalid, V1, V2 };

inline LockFormat detect_lock_format(const json::Document& document) {
    if (!document.is_object()) return LockFormat::Invalid;
    bool has_version = false, has_packages = false, has_flat = false, has_other = false;
    for (const auto& item : document.object) {
        if (item.first == "lockfileVersion") has_version = true;
        else if (item.first == "packages") has_packages = true;
        else if (filesystem::valid_package_name(item.first)) has_flat = true;
        else has_other = true;
    }
    if (has_other) return LockFormat::Invalid;
    if (has_version || has_packages) {
        // v2 requires exactly lockfileVersion + packages and no flat entries.
        if (!(has_version && has_packages) || has_flat) return LockFormat::Invalid;
        return LockFormat::V2;
    }
    return LockFormat::V1;
}

inline bool parse_graph_lock(const json::Document& document, GraphLock& out, std::string& error) {
    if (detect_lock_format(document) != LockFormat::V2) {
        error = "package graph lock must use lockfileVersion 2 with a packages object";
        return false;
    }
    if (!document["lockfileVersion"].is_number() || document["lockfileVersion"].num != 2) {
        error = "unsupported package lockfile version";
        return false;
    }
    if (!document["packages"].is_object()) {
        error = "package graph lock packages must be an object";
        return false;
    }
    GraphLock result;
    for (const auto& item : document["packages"].object) {
        const std::string& name = item.first;
        if (!filesystem::valid_package_name(name)) { error = "invalid locked package name: " + name; return false; }
        const json::Document& node = item.second;
        if (!package_metadata::exact_fields(node, {"source", "commit", "requirements"},
                                            "package graph node '" + name + "'", error)) return false;
        if (!node.has("source") || !node["source"].is_string() ||
            !package_metadata::safe_external_text(node["source"].string)) {
            error = "package graph node '" + name + "' requires a non-empty safe string source"; return false;
        }
        if (!node.has("commit") || !node["commit"].is_string() ||
            !package_metadata::safe_external_text(node["commit"].string)) {
            error = "package graph node '" + name + "' requires a non-empty safe string commit"; return false;
        }
        const std::string commit = node["commit"].string;
        if (commit != "local" && !package_metadata::exact_commit(commit)) {
            error = "package graph node '" + name + "' has an invalid exact commit"; return false;
        }
        if (!node.has("requirements") || !node["requirements"].is_object()) {
            error = "package graph node '" + name + "' requirements must be an object"; return false;
        }
        GraphNode parsed;
        parsed.source = node["source"].string;
        parsed.commit = commit;
        for (const auto& edge : node["requirements"].object) {
            const std::string& child = edge.first;
            if (!filesystem::valid_package_name(child)) { error = "invalid graph requirement package name: " + child; return false; }
            if (!package_metadata::exact_fields(edge.second, {"source", "requested"},
                                                "graph requirement '" + child + "'", error)) return false;
            if (!edge.second.has("source") || !edge.second["source"].is_string() ||
                !package_metadata::safe_external_text(edge.second["source"].string)) {
                error = "graph requirement '" + child + "' requires a non-empty safe string source"; return false;
            }
            if (!edge.second.has("requested") || !edge.second["requested"].is_string() ||
                !package_metadata::safe_external_text(edge.second["requested"].string)) {
                error = "graph requirement '" + child + "' requires a non-empty safe string requested"; return false;
            }
            parsed.requirements.emplace(child, GraphRequirement{edge.second["source"].string,
                                                                edge.second["requested"].string});
        }
        result.packages.emplace(name, std::move(parsed));
    }
    out = std::move(result);
    return true;
}

// Structural graph validation, self-contained (never opens installed
// packages). Requires: every root manifest dependency names a node; the
// closure is closed under edges; every node is reachable from a root
// dependency (no orphans); no self or multi-node cycle. Cycle diagnostics
// carry the deterministic path (a -> b -> c -> a).
inline bool validate_graph_lock(const package_metadata::Manifest& manifest,
                                const GraphLock& graph, std::string& error) {
    if (graph.packages.empty()) { error = "package graph lock has no packages"; return false; }
    for (const auto& dep : manifest.dependencies) {
        if (!graph.packages.count(dep.first)) {
            error = "package graph lock is missing root dependency: " + dep.first; return false;
        }
    }
    for (const auto& node : graph.packages) {
        for (const auto& edge : node.second.requirements) {
            if (!graph.packages.count(edge.first)) {
                error = "package graph node '" + node.first + "' requires missing package: " + edge.first; return false;
            }
        }
    }
    std::set<std::string> reachable;
    std::vector<std::string> stack;
    for (const auto& dep : manifest.dependencies) stack.push_back(dep.first);
    while (!stack.empty()) {
        const std::string name = stack.back(); stack.pop_back();
        if (!reachable.insert(name).second) continue;
        const auto it = graph.packages.find(name);
        if (it == graph.packages.end()) {
            error = "package graph lock is missing root dependency: " + name; return false;
        }
        for (const auto& edge : it->second.requirements) stack.push_back(edge.first);
    }
    for (const auto& node : graph.packages) {
        if (!reachable.count(node.first)) {
            error = "orphan package graph node (not reachable from any root dependency): " + node.first; return false;
        }
    }
    std::set<std::string> visiting, visited;
    std::vector<std::string> path;
    std::function<bool(const std::string&)> dfs = [&](const std::string& name) -> bool {
        if (visiting.count(name)) {
            std::string cycle;
            bool in_cycle = false;
            for (const auto& p : path) {
                if (p == name) in_cycle = true;
                if (in_cycle) { if (!cycle.empty()) cycle += " -> "; cycle += p; }
            }
            error = "package dependency cycle: " + cycle + " -> " + name;
            return false;
        }
        if (visited.count(name)) return true;
        visiting.insert(name); path.push_back(name);
        const auto it = graph.packages.find(name);
        if (it != graph.packages.end()) {
            for (const auto& edge : it->second.requirements) {
                if (!dfs(edge.first)) return false;
            }
        }
        path.pop_back(); visiting.erase(name); visited.insert(name);
        return true;
    };
    for (const auto& dep : manifest.dependencies) {
        if (!dfs(dep.first)) return false;
    }
    return true;
}

// Deterministic serialization: stable top-level field order, packages sorted
// lexicographically by package name, requirements sorted by dependency name,
// stable field order inside nodes and edges.
inline json::Document graph_lock_document(const GraphLock& graph) {
    json::Document result = json::Document::make_object();
    result["lockfileVersion"] = json::Document(2);
    json::Document packages = json::Document::make_object();
    for (const auto& item : graph.packages) {
        json::Document node = json::Document::make_object();
        node["source"] = json::Document(item.second.source);
        node["commit"] = json::Document(item.second.commit);
        json::Document requirements = json::Document::make_object();
        for (const auto& edge : item.second.requirements) {
            json::Document requirement = json::Document::make_object();
            requirement["source"] = json::Document(edge.second.source);
            requirement["requested"] = json::Document(edge.second.requested);
            requirements[edge.first] = requirement;
        }
        node["requirements"] = requirements;
        packages[item.first] = node;
    }
    result["packages"] = packages;
    return result;
}

inline std::string graph_lock_text(const GraphLock& graph) {
    return graph_lock_document(graph).dump(2) + "\n";
}

// Owner-relative local source resolution. A relative local source in a
// package manifest resolves against the directory containing that manifest
// (the owner), never the process CWD. The owner directory must be absolute.
inline bool local_source_absolute(const std::filesystem::path& owner_dir,
                                  const std::string& source,
                                  std::filesystem::path& out, std::string& error) {
    if (!package_metadata::source_is_local_path(source)) {
        error = "source is not a local path: " + source; return false;
    }
    if (owner_dir.empty() || !owner_dir.is_absolute()) {
        error = "local source owner directory must be absolute"; return false;
    }
    out = (owner_dir / source).lexically_normal();
    return true;
}

}  // namespace package_graph