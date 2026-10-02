// CP10: deterministic v2 package graph lock representation. Representation /
// parse / structural validation / deterministic serialization only. No
// resolver, no acquisition, no command wiring. v1 direct-only behavior and
// runtime package provenance are preserved.
#include "PackageGraphLock.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "package-graph-lock FAIL: %s (line %d)\n",  \
                         #cond, __LINE__);                                   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

static bool parse(const std::string& text, json::Document& out, std::string& error) {
    return nift_json::parse(text, out, error);
}

static package_metadata::Manifest project_manifest(const std::map<std::string, package_metadata::Dependency>& deps) {
    package_metadata::Manifest m;
    m.has_dependencies = true;
    m.dependencies = deps;
    return m;
}

static std::string v2_text() {
    // Worked graph: root -> {a, c}; a -> {b(latest), d(latest)}; c -> {b(v1.2.3)}.
    return R"({
  "lockfileVersion": 2,
  "packages": {
    "a": {"source": "https://github.com/org/a.git", "commit": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
          "requirements": {"b": {"source": "github:org/b", "requested": "latest"},
                           "d": {"source": "github:org/d", "requested": "latest"}}},
    "b": {"source": "https://github.com/org/b.git", "commit": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
          "requirements": {}},
    "c": {"source": "https://github.com/org/c.git", "commit": "cccccccccccccccccccccccccccccccccccccccc",
          "requirements": {"b": {"source": "github:org/b", "requested": "v1.2.3"}}},
    "d": {"source": "https://github.com/org/d.git", "commit": "dddddddddddddddddddddddddddddddddddddddd",
          "requirements": {}}
  }
})";
}

int main() {
    namespace fs = std::filesystem;
    std::string error;

    // ---- format detection: v1, v2, mixed, unsupported, malformed ----
    {
        json::Document doc;
        CHECK(parse("{\"pkg\": {\"source\": \"x\", \"requested\": \"latest\", \"commit\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}", doc, error));
        CHECK(package_graph::detect_lock_format(doc) == package_graph::LockFormat::V1);
        CHECK(parse(v2_text(), doc, error));
        CHECK(package_graph::detect_lock_format(doc) == package_graph::LockFormat::V2);
        // mixed: v2 packages plus a stray flat entry
        json::Document mixed;
        CHECK(parse("{\"lockfileVersion\": 2, \"packages\": {}, \"pkg\": {\"source\": \"x\", \"requested\": \"latest\", \"commit\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}", mixed, error));
        CHECK(package_graph::detect_lock_format(mixed) == package_graph::LockFormat::Invalid);
        // v1 map plus lockfileVersion
        json::Document mixed2;
        CHECK(parse("{\"lockfileVersion\": 1, \"pkg\": {\"source\": \"x\", \"requested\": \"latest\", \"commit\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}", mixed2, error));
        CHECK(package_graph::detect_lock_format(mixed2) == package_graph::LockFormat::Invalid);
        // unknown top-level field (uppercase is not a valid package name)
        json::Document other;
        CHECK(parse("{\"UnknownField\": 1}", other, error));
        CHECK(package_graph::detect_lock_format(other) == package_graph::LockFormat::Invalid);
        // non-object
        json::Document arr;
        CHECK(parse("[1,2]", arr, error));
        CHECK(package_graph::detect_lock_format(arr) == package_graph::LockFormat::Invalid);
    }

    // ---- valid v2 worked graph parses + validates structurally ----
    package_graph::GraphLock graph;
    {
        json::Document doc;
        CHECK(parse(v2_text(), doc, error));
        CHECK(package_graph::parse_graph_lock(doc, graph, error));
        CHECK(graph.packages.size() == 4);
        // node identity: canonical source + commit
        CHECK(graph.packages.at("b").source == "https://github.com/org/b.git");
        CHECK(graph.packages.at("b").commit == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        // edge fidelity preserved for both parents
        CHECK(graph.packages.at("a").requirements.at("b").source == "github:org/b");
        CHECK(graph.packages.at("a").requirements.at("b").requested == "latest");
        CHECK(graph.packages.at("c").requirements.at("b").requested == "v1.2.3");
        auto manifest = project_manifest({{"a", {"github:org/a", "latest"}},
                                          {"c", {"github:org/c", "latest"}}});
        CHECK(package_graph::validate_graph_lock(manifest, graph, error));
    }

    // ---- strict v2 field validation ----
    {
        auto fail = [&](const std::string& text) {
            json::Document doc;
            if (!parse(text, doc, error)) return false;
            package_graph::GraphLock g;
            return !package_graph::parse_graph_lock(doc, g, error);
        };
        // unsupported lockfile version
        CHECK(fail("{\"lockfileVersion\": 3, \"packages\": {}}"));
        // non-numeric lockfile version
        CHECK(fail("{\"lockfileVersion\": \"2\", \"packages\": {}}"));
        // missing packages
        CHECK(fail("{\"lockfileVersion\": 2}"));
        // missing lockfileVersion
        CHECK(fail("{\"packages\": {}}"));
        // invalid package name
        CHECK(fail("{\"lockfileVersion\": 2, \"packages\": {\"Bad-Name\": {\"source\": \"x\", \"commit\": \"local\", \"requirements\": {}}}}"));
        // unknown node field
        CHECK(fail("{\"lockfileVersion\": 2, \"packages\": {\"b\": {\"source\": \"x\", \"commit\": \"local\", \"requirements\": {}, \"mystery\": 1}}}"));
        // missing node field
        CHECK(fail("{\"lockfileVersion\": 2, \"packages\": {\"b\": {\"source\": \"x\", \"commit\": \"local\"}}}"));
        // invalid commit
        CHECK(fail("{\"lockfileVersion\": 2, \"packages\": {\"b\": {\"source\": \"x\", \"commit\": \"not-a-commit\", \"requirements\": {}}}}"));
        // requirements not an object
        CHECK(fail("{\"lockfileVersion\": 2, \"packages\": {\"b\": {\"source\": \"x\", \"commit\": \"local\", \"requirements\": [1]}}}"));
        // malformed edge: extra field
        CHECK(fail("{\"lockfileVersion\": 2, \"packages\": {\"b\": {\"source\": \"x\", \"commit\": \"local\", \"requirements\": {\"c\": {\"source\": \"y\", \"requested\": \"latest\", \"mystery\": 1}}}}}"));
        // malformed edge: missing requested
        CHECK(fail("{\"lockfileVersion\": 2, \"packages\": {\"b\": {\"source\": \"x\", \"commit\": \"local\", \"requirements\": {\"c\": {\"source\": \"y\"}}}}}"));
    }

    // ---- structural validation: root reachability, orphans, missing edges, cycles ----
    {
        auto manifest = project_manifest({{"a", {"github:org/a", "latest"}}});
        // missing root dependency node
        {
            json::Document doc;
            CHECK(parse("{\"lockfileVersion\": 2, \"packages\": {\"b\": {\"source\": \"x\", \"commit\": \"local\", \"requirements\": {}}}}", doc, error));
            package_graph::GraphLock g;
            CHECK(package_graph::parse_graph_lock(doc, g, error));
            CHECK(!package_graph::validate_graph_lock(manifest, g, error));
        }
        // missing edge target
        {
            json::Document doc;
            CHECK(parse("{\"lockfileVersion\": 2, \"packages\": {\"a\": {\"source\": \"x\", \"commit\": \"local\", \"requirements\": {\"z\": {\"source\": \"y\", \"requested\": \"latest\"}}}}}", doc, error));
            package_graph::GraphLock g;
            CHECK(package_graph::parse_graph_lock(doc, g, error));
            CHECK(!package_graph::validate_graph_lock(manifest, g, error));
        }
        // orphan node (not reachable from any root dep)
        {
            json::Document doc;
            CHECK(parse("{\"lockfileVersion\": 2, \"packages\": {\"a\": {\"source\": \"x\", \"commit\": \"local\", \"requirements\": {}}, \"orphan\": {\"source\": \"y\", \"commit\": \"local\", \"requirements\": {}}}}", doc, error));
            package_graph::GraphLock g;
            CHECK(package_graph::parse_graph_lock(doc, g, error));
            CHECK(!package_graph::validate_graph_lock(manifest, g, error));
        }
        // self-cycle
        {
            json::Document doc;
            CHECK(parse("{\"lockfileVersion\": 2, \"packages\": {\"a\": {\"source\": \"x\", \"commit\": \"local\", \"requirements\": {\"a\": {\"source\": \"x\", \"requested\": \"latest\"}}}}}", doc, error));
            package_graph::GraphLock g;
            CHECK(package_graph::parse_graph_lock(doc, g, error));
            CHECK(!package_graph::validate_graph_lock(manifest, g, error));
            CHECK(error.find("cycle") != std::string::npos);
        }
        // multi-node cycle a -> b -> a
        {
            json::Document doc;
            CHECK(parse("{\"lockfileVersion\": 2, \"packages\": {\"a\": {\"source\": \"x\", \"commit\": \"local\", \"requirements\": {\"b\": {\"source\": \"y\", \"requested\": \"latest\"}}}, \"b\": {\"source\": \"y\", \"commit\": \"local\", \"requirements\": {\"a\": {\"source\": \"x\", \"requested\": \"latest\"}}}}}", doc, error));
            package_graph::GraphLock g;
            CHECK(package_graph::parse_graph_lock(doc, g, error));
            CHECK(!package_graph::validate_graph_lock(manifest, g, error));
            CHECK(error.find("cycle") != std::string::npos);
        }
    }

    // ---- deterministic serialization + insertion-order independence ----
    {
        auto make_graph = [](bool reverse) {
            package_graph::GraphLock g;
            auto add = [&](const std::string& name, const std::string& source,
                           const std::string& commit,
                           const std::map<std::string, package_graph::GraphRequirement>& reqs) {
                package_graph::GraphNode node;
                node.source = source;
                node.commit = commit;
                node.requirements = reqs;
                g.packages[name] = std::move(node);
            };
            if (reverse) {
                add("d", "s4", "local", {});
                add("c", "s3", "local", {});
                add("b", "s2", "local", {});
                add("a", "s1", "local", {{"b", {"s2", "latest"}}});
            } else {
                add("a", "s1", "local", {{"b", {"s2", "latest"}}});
                add("b", "s2", "local", {});
                add("c", "s3", "local", {});
                add("d", "s4", "local", {});
            }
            return g;
        };
        const std::string fwd = package_graph::graph_lock_text(make_graph(false));
        const std::string rev = package_graph::graph_lock_text(make_graph(true));
        CHECK(fwd == rev);
        // round-trip: parse -> serialize -> parse is byte-identical
        json::Document doc;
        CHECK(parse(fwd, doc, error));
        package_graph::GraphLock g1, g2;
        CHECK(package_graph::parse_graph_lock(doc, g1, error));
        CHECK(parse(package_graph::graph_lock_text(g1), doc, error));
        CHECK(package_graph::parse_graph_lock(doc, g2, error));
        CHECK(package_graph::graph_lock_text(g1) == package_graph::graph_lock_text(g2));
    }

    // ---- edge fidelity + name identity ----
    {
        // Two differently named nodes may share the same source + commit and
        // remain two distinct nodes (name, not source tuple, is identity).
        json::Document doc;
        CHECK(parse("{\"lockfileVersion\": 2, \"packages\": {\"x\": {\"source\": \"https://github.com/org/s.git\", \"commit\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\", \"requirements\": {}}, \"y\": {\"source\": \"https://github.com/org/s.git\", \"commit\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\", \"requirements\": {}}}}", doc, error));
        package_graph::GraphLock g;
        CHECK(package_graph::parse_graph_lock(doc, g, error));
        CHECK(g.packages.size() == 2);
        CHECK(g.packages.count("x") && g.packages.count("y"));
    }

    // ---- owner-relative local source normalization (CWD-independent) ----
    {
        const fs::path owner = (fs::absolute(".") / "packages" / "a").lexically_normal();
        fs::path out;
        CHECK(package_graph::local_source_absolute(owner, "./b", out, error));
        CHECK(out == (owner / "b").lexically_normal());
        CHECK(package_graph::local_source_absolute(owner, "../sibling", out, error));
        CHECK(out == (owner.parent_path() / "sibling").lexically_normal());
        // CWD change has no effect because the owner is absolute
        fs::path cwd_before = fs::current_path();
        std::error_code ec;
        fs::current_path(fs::temp_directory_path(), ec);
        fs::path out_under_other_cwd;
        CHECK(package_graph::local_source_absolute(owner, "./b", out_under_other_cwd, error));
        fs::current_path(cwd_before, ec);
        CHECK(out_under_other_cwd == (owner / "b").lexically_normal());
        // relative owner rejected (pins CWD independence)
        CHECK(!package_graph::local_source_absolute(fs::path("relative/owner"), "./b", out, error));
        // git source rejected by the local primitive
        CHECK(!package_graph::local_source_absolute(owner, "https://github.com/x/y.git", out, error));
    }

    // ---- canonical git source reuse (single normalizer) ----
    {
        CHECK(package_metadata::git_source("b") == "https://github.com/nift-packages/b.git");
        CHECK(package_metadata::git_source("github:org/x") == "https://github.com/org/x.git");
        CHECK(package_metadata::git_source("https://example.com/x.git") == "https://example.com/x.git");
        CHECK(package_metadata::git_source("git@host:path/repo") == "git@host:path/repo");
        CHECK(package_metadata::source_is_local_path("./b"));
        CHECK(package_metadata::source_is_local_path("../b"));
        CHECK(package_metadata::source_is_local_path("/abs/b"));
        CHECK(!package_metadata::source_is_local_path("https://example.com/x.git"));
    }

    // ---- v1 compatibility: existing parse_lock + validate_lock unchanged ----
    {
        json::Document doc;
        CHECK(parse("{\"pkg\": {\"source\": \"github:org/a\", \"requested\": \"latest\", \"commit\": \"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}", doc, error));
        package_metadata::Lock lock;
        CHECK(package_metadata::parse_lock(doc, lock, error));
        CHECK(lock.size() == 1);
        CHECK(lock.at("pkg").source == "github:org/a");
        CHECK(lock.at("pkg").requested == "latest");
        auto manifest = project_manifest({{"pkg", {"github:org/a", "latest"}}});
        CHECK(package_metadata::validate_lock(manifest, lock, error));
    }

    if (failures) {
        std::fprintf(stderr, "package-graph-lock: %d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "package-graph-lock: PASS\n");
    return 0;
}