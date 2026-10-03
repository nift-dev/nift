#pragma once

// Batch 5 CP13: deterministic package graph inspection. A graph explanation is
// derived from the root manifest + the v2 lock graph alone; installed package
// manifests are never opened. Deterministic output: roots lexicographically,
// child edges lexicographically, paths sorted lexicographically by their node
// sequence. Same manifest+lock bytes always produce identical output.
#include "PackageGraphLock.h"
#include "PackageMetadata.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace package_graph {

struct RequirementHop {
    std::string parent;    // "root" or a package name
    std::string child;     // target package name
    std::string source;    // requirement source spelling
    std::string requested; // requested ref
};

struct GraphPath {
    std::vector<std::string> nodes;  // root -> ... -> target (target last)
    RequirementHop requirement;      // the edge into the target (or root req)
};

// Enumerate every root-to-node path deterministically from the root manifest +
// v2 lock graph. Validates the graph with validate_graph_lock first.
inline bool enumerate_graph_paths(const package_metadata::Manifest& root,
                                  const GraphLock& graph,
                                  std::map<std::string, std::vector<GraphPath>>& by_target,
                                  std::string& error) {
    if (!validate_graph_lock(root, graph, error)) return false;
    std::vector<std::pair<std::string, package_metadata::Dependency>> roots(
        root.dependencies.begin(), root.dependencies.end());
    std::sort(roots.begin(), roots.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::function<void(const std::string&, std::vector<std::string>&,
                       const RequirementHop&, std::map<std::string, std::vector<GraphPath>>&)>
        dfs = [&](const std::string& name, std::vector<std::string>& nodes,
                  const RequirementHop& hop, std::map<std::string, std::vector<GraphPath>>& out) {
        std::vector<std::string> path = nodes;
        path.push_back(name);
        out[name].push_back(GraphPath{path, hop});
        const auto it = graph.packages.find(name);
        if (it == graph.packages.end()) return;
        for (const auto& edge : it->second.requirements) {
            std::vector<std::string> child_nodes = path;
            const RequirementHop child_hop{name, edge.first, edge.second.source, edge.second.requested};
            dfs(edge.first, child_nodes, child_hop, out);
        }
    };

    for (const auto& root_dep : roots) {
        std::vector<std::string> nodes;
        const RequirementHop root_hop{"root", root_dep.first, root_dep.second.source, root_dep.second.ref};
        dfs(root_dep.first, nodes, root_hop, by_target);
    }

    // Deterministic path ordering: lexicographic by node-name sequence.
    for (auto& item : by_target) {
        std::sort(item.second.begin(), item.second.end(),
                  [](const GraphPath& a, const GraphPath& b) { return a.nodes < b.nodes; });
    }
    return true;
}

// Human-readable explanation for one package (or the whole graph when the
// query is empty).
inline std::string explain_graph(const package_metadata::Manifest& root,
                                 const GraphLock& graph, const std::string& query,
                                 std::string& error) {
    std::map<std::string, std::vector<GraphPath>> by_target;
    if (!enumerate_graph_paths(root, graph, by_target, error)) return {};

    std::ostringstream out;
    if (query.empty()) {
        // whole-graph summary: one block per package, sorted by name
        for (const auto& node : graph.packages) {
            const std::string& name = node.first;
            out << name << "\n";
            out << "  resolved source: " << node.second.source << "\n";
            out << "  commit: " << node.second.commit << "\n";
            const auto found = by_target.find(name);
            if (found != by_target.end() && !found->second.empty()) {
                out << "  required by: " << found->second.size() << " path(s)\n";
                for (const auto& path : found->second) {
                    out << "    ";
                    for (std::size_t i = 0; i < path.nodes.size(); ++i) {
                        if (i) out << " -> ";
                        out << path.nodes[i];
                    }
                    out << "\n        source: " << path.requirement.source
                        << "\n        requested: " << path.requirement.requested << "\n";
                }
            }
        }
    } else {
        const auto node = graph.packages.find(query);
        if (node == graph.packages.end()) {
            error = "package is not in the dependency graph: " + query;
            return {};
        }
        out << query << "\n";
        out << "  resolved source: " << node->second.source << "\n";
        out << "  commit: " << node->second.commit << "\n";
        const auto found = by_target.find(query);
        if (found == by_target.end() || found->second.empty()) {
            out << "  (not reachable from any root requirement)\n";
        } else {
            out << "  required by:\n";
            for (const auto& path : found->second) {
                out << "    ";
                for (std::size_t i = 0; i < path.nodes.size(); ++i) {
                    if (i) out << " -> ";
                    out << path.nodes[i];
                }
                out << "\n      source spelling: " << path.requirement.source
                    << "\n      requested: " << path.requirement.requested << "\n";
            }
        }
    }
    return out.str();
}

}  // namespace package_graph