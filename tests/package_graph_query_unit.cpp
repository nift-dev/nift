// CP13: deterministic package graph inspection. Path enumeration derives
// everything from the root manifest + v2 lock graph (installed packages are
// never opened) and is byte-deterministic.
#include "PackageGraphQuery.h"

#include <cstdio>
#include <map>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "package-graph-query FAIL: %s (line %d)\n", \
                         #cond, __LINE__);                                   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

using package_metadata::Dependency;
using package_metadata::Manifest;

static Manifest root(const std::map<std::string, Dependency>& deps) {
    Manifest m;
    m.has_dependencies = true;
    m.dependencies = deps;
    return m;
}

static package_graph::GraphLock lock_with(
    const std::map<std::string, std::pair<std::string, std::string>>& nodes,
    const std::map<std::string, std::map<std::string, package_graph::GraphRequirement>>& edges) {
    package_graph::GraphLock g;
    for (const auto& node : nodes) {
        package_graph::GraphNode n;
        n.source = node.second.first;
        n.commit = node.second.second;
        const auto e = edges.find(node.first);
        if (e != edges.end()) n.requirements = e->second;
        g.packages[node.first] = std::move(n);
    }
    return g;
}

int main() {
    std::string error;
    const std::string B = "https://github.com/org/b.git";
    const std::string A = "https://github.com/org/a.git";
    const std::string C = "https://github.com/org/c.git";

    // ---- single root a -> b: both paths enumerated ----
    {
        package_graph::GraphLock g = lock_with(
            {{"a", {A, "X"}}, {"b", {B, "Y"}}},
            {{"a", {{"b", {"github:org/b", "latest"}}}}});
        std::map<std::string, std::vector<package_graph::GraphPath>> by;
        CHECK(package_graph::enumerate_graph_paths(root({{"a", {"github:org/a", "latest"}}}), g, by, error));
        CHECK(by.count("a") && by.at("a").size() == 1);
        CHECK((by.at("a")[0].nodes == std::vector<std::string>{"a"}));
        CHECK(by.at("a")[0].requirement.source == "github:org/a");
        CHECK(by.at("a")[0].requirement.requested == "latest");
        CHECK(by.count("b") && by.at("b").size() == 1);
        CHECK((by.at("b")[0].nodes == std::vector<std::string>{"a", "b"}));
        CHECK(by.at("b")[0].requirement.source == "github:org/b");
        CHECK(by.at("b")[0].requirement.requested == "latest");
    }

    // ---- diamond a -> b, c -> b: both paths, stable order, distinct edges ----
    {
        package_graph::GraphLock g = lock_with(
            {{"a", {A, "X"}}, {"c", {C, "W"}}, {"b", {B, "Y"}}},
            {{"a", {{"b", {"github:org/b", "latest"}}}},
             {"c", {{"b", {"github:org/b", "v1.2.3"}}}}});
        const Manifest m = root({{"a", {"github:org/a", "latest"}}, {"c", {"github:org/c", "latest"}}});
        std::map<std::string, std::vector<package_graph::GraphPath>> by;
        CHECK(package_graph::enumerate_graph_paths(m, g, by, error));
        CHECK(by.at("b").size() == 2);
        // lexicographic order: a -> b before c -> b
        CHECK((by.at("b")[0].nodes == std::vector<std::string>{"a", "b"}));
        CHECK((by.at("b")[1].nodes == std::vector<std::string>{"c", "b"}));
        // distinct edge requirements preserved (not collapsed)
        CHECK(by.at("b")[0].requirement.requested == "latest");
        CHECK(by.at("b")[1].requirement.requested == "v1.2.3");
        // source spelling vs canonical node source both available
        CHECK(by.at("b")[0].requirement.source == "github:org/b");
        CHECK(g.packages.at("b").source == B);
        // explanation is deterministic and shows both paths
        const std::string text = package_graph::explain_graph(m, g, "b", error);
        CHECK(text.find("a -> b") != std::string::npos);
        CHECK(text.find("c -> b") != std::string::npos);
        CHECK(text.find("latest") != std::string::npos);
        CHECK(text.find("v1.2.3") != std::string::npos);
    }

    // ---- query works without any installed packages (manifest + lock only) ----
    {
        package_graph::GraphLock g = lock_with({{"a", {A, "X"}}}, {});
        const std::string text = package_graph::explain_graph(root({{"a", {"github:org/a", "latest"}}}), g, "a", error);
        CHECK(text.find("resolved source: " + A) != std::string::npos);
        CHECK(text.find("required by:") != std::string::npos);
    }

    // ---- missing target / invalid query ----
    {
        package_graph::GraphLock g = lock_with({{"a", {A, "X"}}}, {});
        const std::string text = package_graph::explain_graph(root({{"a", {"github:org/a", "latest"}}}), g, "zz", error);
        CHECK(text.empty());
        CHECK(error.find("not in the dependency graph") != std::string::npos);
    }

    // ---- validation rejects malformed graphs (missing target, orphan, cycle) ----
    {
        package_graph::GraphLock orphan = lock_with(
            {{"a", {A, "X"}}, {"orphan", {B, "Y"}}}, {});
        std::map<std::string, std::vector<package_graph::GraphPath>> by;
        CHECK(!package_graph::enumerate_graph_paths(root({{"a", {"github:org/a", "latest"}}}), orphan, by, error));
        CHECK(error.find("orphan") != std::string::npos);

        package_graph::GraphLock cycle = lock_with(
            {{"a", {A, "X"}}, {"b", {B, "Y"}}},
            {{"a", {{"b", {"github:org/b", "latest"}}}},
             {"b", {{"a", {"github:org/a", "latest"}}}}});
        CHECK(!package_graph::enumerate_graph_paths(root({{"a", {"github:org/a", "latest"}}}), cycle, by, error));
        CHECK(error.find("cycle") != std::string::npos);

        package_graph::GraphLock missing = lock_with(
            {{"a", {A, "X"}}},
            {{"a", {{"zz", {"github:org/zz", "latest"}}}}});
        CHECK(!package_graph::enumerate_graph_paths(root({{"a", {"github:org/a", "latest"}}}), missing, by, error));
        CHECK(error.find("missing") != std::string::npos);
    }

    // ---- same manifest+lock always produce identical explanation bytes ----
    {
        package_graph::GraphLock g = lock_with(
            {{"a", {A, "X"}}, {"c", {C, "W"}}, {"b", {B, "Y"}}},
            {{"a", {{"b", {"github:org/b", "latest"}}}},
             {"c", {{"b", {"github:org/b", "v1.2.3"}}}}});
        const Manifest m = root({{"a", {"github:org/a", "latest"}}, {"c", {"github:org/c", "latest"}}});
        CHECK(package_graph::explain_graph(m, g, "b", error) == package_graph::explain_graph(m, g, "b", error));
        CHECK(package_graph::explain_graph(m, g, "", error) == package_graph::explain_graph(m, g, "", error));
    }

    if (failures) {
        std::fprintf(stderr, "package-graph-query: %d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "package-graph-query: PASS\n");
    return 0;
}