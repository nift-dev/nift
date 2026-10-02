// CP11: deterministic transitive package graph resolver (compute-only). The
// entire algorithm is exercised with an in-memory fake ResolutionProvider; no
// git, no network, no store mutation.
#include "PackageGraphResolver.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "package-graph-resolver FAIL: %s (line %d)\n", \
                         #cond, __LINE__);                                   \
            ++failures;                                                      \
        }                                                                    \
    } while (0)

namespace fs = std::filesystem;

using package_metadata::Dependency;
using package_metadata::Manifest;

static Manifest make_manifest(const std::string& name,
                              const std::map<std::string, Dependency>& deps = {}) {
    Manifest m;
    m.has_identity = true;
    m.name = name;
    m.version = "0.1.0";
    m.entry = "src/main.f";
    m.has_dependencies = true;
    m.dependencies = deps;
    return m;
}

static Manifest root_manifest(const std::map<std::string, Dependency>& deps) {
    Manifest m;
    m.has_dependencies = true;
    m.dependencies = deps;
    return m;
}

class FakeProvider : public package_graph::ResolutionProvider {
public:
    std::map<std::string, Manifest> manifests;       // by package name
    std::map<std::string, fs::path> dirs;            // manifest owner dir by name
    std::map<std::pair<std::string, std::string>, std::string> ref_commits;  // (canonical, ref) -> commit
    std::map<std::pair<std::string, std::string>, int> resolve_counts;       // per-key resolution count
    std::set<std::string> failing_loads;             // names whose load fails
    int resolve_calls_total = 0;

    bool resolve_ref(const package_graph::Requirement& r, std::string& commit,
                     std::string& error) override {
        ++resolve_calls_total;
        const std::string canonical = package_metadata::git_source(r.source);
        const auto key = std::make_pair(canonical, r.requested);
        ++resolve_counts[key];
        const auto it = ref_commits.find(key);
        if (it == ref_commits.end()) { error = "no ref resolution for " + canonical + "/" + r.requested; return false; }
        commit = it->second;
        return true;
    }

    bool load_package_manifest(const package_graph::Requirement& r, const std::string&,
                               const std::string&, fs::path& dir,
                               Manifest& manifest, std::string& error) override {
        if (failing_loads.count(r.name)) { error = "malformed transitive manifest: " + r.name; return false; }
        const auto it = manifests.find(r.name);
        if (it == manifests.end()) { error = "no manifest for " + r.name; return false; }
        manifest = it->second;
        const auto d = dirs.find(r.name);
        dir = (d != dirs.end()) ? d->second : (fs::path("/fake") / r.name);
        return true;
    }
};

static bool resolve(const Manifest& root, const fs::path& root_dir,
                    package_graph::ResolveMode mode, const package_graph::GraphLock* locked,
                    const std::string* target, FakeProvider& p,
                    package_graph::ResolveOutcome& out, std::string& error) {
    return package_graph::resolve_graph(root, root_dir, mode, locked, target, p, out, error);
}

int main() {
    const fs::path root_dir = fs::path("/repo/root");

    // ---- zero dependencies ----
    {
        FakeProvider p;
        package_graph::ResolveOutcome out; std::string error;
        CHECK(resolve(root_manifest({}), root_dir, package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(out.graph.packages.empty());
    }

    // ---- one dependency ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a");
        p.ref_commits[{"https://github.com/org/a.git", "latest"}] = "X";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(resolve(root_manifest({{"a", {"github:org/a", "latest"}}}), root_dir,
                      package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(out.graph.packages.size() == 1);
        CHECK(out.graph.packages.at("a").source == "https://github.com/org/a.git");
        CHECK(out.graph.packages.at("a").commit == "X");
    }

    // ---- multi-level chain a -> b -> c ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"b", {"github:org/b", "latest"}}});
        p.manifests["b"] = make_manifest("b", {{"c", {"github:org/c", "latest"}}});
        p.manifests["c"] = make_manifest("c");
        for (const char* n : {"a", "b", "c"}) p.ref_commits[{std::string("https://github.com/org/") + n + ".git", "latest"}] = std::string(n) + "C";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(resolve(root_manifest({{"a", {"github:org/a", "latest"}}}), root_dir,
                      package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(out.graph.packages.size() == 3);
        CHECK(out.graph.packages.at("a").requirements.at("b").requested == "latest");
        CHECK(out.graph.packages.at("b").requirements.at("c").requested == "latest");
    }

    // ---- branching + shared diamond (not a cycle): root -> {a, c}; a->b, c->b ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"b", {"github:org/b", "latest"}}});
        p.manifests["c"] = make_manifest("c", {{"b", {"github:org/b", "v1.2.3"}}});
        p.manifests["b"] = make_manifest("b");
        p.ref_commits[{"https://github.com/org/b.git", "latest"}] = "X";
        p.ref_commits[{"https://github.com/org/b.git", "v1.2.3"}] = "X";  // both refs -> same commit
        for (const char* n : {"a", "c"}) p.ref_commits[{std::string("https://github.com/org/") + n + ".git", "latest"}] = std::string(n) + "C";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(resolve(root_manifest({{"a", {"github:org/a", "latest"}}, {"c", {"github:org/c", "latest"}}}), root_dir,
                      package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(out.graph.packages.size() == 3);
        // one b node at X, both edges preserved with their own requested refs
        CHECK(out.graph.packages.at("b").commit == "X");
        CHECK(out.graph.packages.at("a").requirements.at("b").requested == "latest");
        CHECK(out.graph.packages.at("c").requirements.at("b").requested == "v1.2.3");
    }

    // ---- different parent refs -> different commit => conflict ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"b", {"github:org/b", "latest"}}});
        p.manifests["c"] = make_manifest("c", {{"b", {"github:org/b", "v1.2.3"}}});
        p.manifests["b"] = make_manifest("b");
        p.ref_commits[{"https://github.com/org/b.git", "latest"}] = "X";
        p.ref_commits[{"https://github.com/org/b.git", "v1.2.3"}] = "Y";  // different
        for (const char* n : {"a", "c"}) p.ref_commits[{std::string("https://github.com/org/") + n + ".git", "latest"}] = std::string(n) + "C";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(!resolve(root_manifest({{"a", {"github:org/a", "latest"}}, {"c", {"github:org/c", "latest"}}}), root_dir,
                       package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(error.find("dependency conflict for package: b") != std::string::npos);
        CHECK(error.find("existing path") != std::string::npos);
        CHECK(error.find("incoming path") != std::string::npos);
    }

    // ---- same name -> different source => conflict ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"b", {"github:org/b", "latest"}}});
        p.manifests["c"] = make_manifest("c", {{"b", {"github:other/b", "latest"}}});
        p.manifests["b"] = make_manifest("b");
        p.ref_commits[{"https://github.com/org/b.git", "latest"}] = "X";
        p.ref_commits[{"https://github.com/other/b.git", "latest"}] = "X";
        for (const char* n : {"a", "c"}) p.ref_commits[{std::string("https://github.com/org/") + n + ".git", "latest"}] = std::string(n) + "C";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(!resolve(root_manifest({{"a", {"github:org/a", "latest"}}, {"c", {"github:org/c", "latest"}}}), root_dir,
                       package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(error.find("dependency conflict for package: b") != std::string::npos);
    }

    // ---- different names -> same source remain two nodes ----
    {
        FakeProvider p;
        p.manifests["foo"] = make_manifest("foo");
        p.manifests["bar"] = make_manifest("bar");
        p.ref_commits[{"https://github.com/org/s.git", "latest"}] = "X";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(resolve(root_manifest({{"foo", {"github:org/s", "latest"}}, {"bar", {"github:org/s", "latest"}}}), root_dir,
                      package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(out.graph.packages.size() == 2);
        CHECK(out.graph.packages.count("foo") && out.graph.packages.count("bar"));
    }

    // ---- self-cycle a -> a ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"a", {"github:org/a", "latest"}}});
        p.ref_commits[{"https://github.com/org/a.git", "latest"}] = "X";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(!resolve(root_manifest({{"a", {"github:org/a", "latest"}}}), root_dir,
                       package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(error.find("cycle") != std::string::npos);
    }

    // ---- multi-node cycle a -> b -> a with deterministic path ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"b", {"github:org/b", "latest"}}});
        p.manifests["b"] = make_manifest("b", {{"a", {"github:org/a", "latest"}}});
        p.ref_commits[{"https://github.com/org/a.git", "latest"}] = "X";
        p.ref_commits[{"https://github.com/org/b.git", "latest"}] = "X";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(!resolve(root_manifest({{"a", {"github:org/a", "latest"}}}), root_dir,
                       package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(error.find("dependency cycle: a -> b -> a") != std::string::npos);
    }

    // ---- package manifest name mismatch ----
    {
        FakeProvider p;
        p.manifests["b"] = make_manifest("c");  // edge says b, manifest says c
        p.ref_commits[{"https://github.com/org/b.git", "latest"}] = "X";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(!resolve(root_manifest({{"b", {"github:org/b", "latest"}}}), root_dir,
                       package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(error.find("manifest name mismatch") != std::string::npos);
    }

    // ---- malformed transitive manifest (provider load failure) ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"b", {"github:org/b", "latest"}}});
        p.manifests["b"] = make_manifest("b");
        p.failing_loads.insert("b");
        p.ref_commits[{"https://github.com/org/a.git", "latest"}] = "X";
        p.ref_commits[{"https://github.com/org/b.git", "latest"}] = "X";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(!resolve(root_manifest({{"a", {"github:org/a", "latest"}}}), root_dir,
                       package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK(error.find("malformed transitive manifest") != std::string::npos);
    }

    // ---- owner-relative local sources + ../sibling + CWD independence ----
    {
        FakeProvider p;
        const fs::path a_dir = root_dir / "packages" / "a";
        const fs::path b_dir = root_dir / "packages" / "b";
        p.manifests["a"] = make_manifest("a", {{"b", {"../b", "local"}}});
        p.manifests["b"] = make_manifest("b");
        p.dirs["a"] = a_dir;
        p.dirs["b"] = b_dir;
        package_graph::ResolveOutcome out1, out2; std::string error;
        const Manifest root = root_manifest({{"a", {"./packages/a", "local"}}});
        CHECK(resolve(root, root_dir, package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out1, error));
        CHECK(out1.graph.packages.at("a").source == (root_dir / "packages" / "a").generic_string());
        CHECK(out1.graph.packages.at("b").source == b_dir.generic_string());
        CHECK(out1.graph.packages.at("b").commit == "local");
        // CWD change must not affect resolution
        const fs::path cwd_before = fs::current_path();
        std::error_code ec;
        fs::current_path(fs::temp_directory_path(), ec);
        CHECK(resolve(root, root_dir, package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out2, error));
        fs::current_path(cwd_before, ec);
        CHECK(package_graph::graph_lock_text(out1.graph) == package_graph::graph_lock_text(out2.graph));
    }

    // ---- floating ref resolved once (memoized) + changing provider answer never observed ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"b", {"github:org/b", "latest"}}});
        p.manifests["c"] = make_manifest("c", {{"b", {"github:org/b", "latest"}}});
        p.manifests["b"] = make_manifest("b");
        p.ref_commits[{"https://github.com/org/b.git", "latest"}] = "X";
        for (const char* n : {"a", "c"}) p.ref_commits[{std::string("https://github.com/org/") + n + ".git", "latest"}] = std::string(n) + "C";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(resolve(root_manifest({{"a", {"github:org/a", "latest"}}, {"c", {"github:org/c", "latest"}}}), root_dir,
                      package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        // b/latest resolved exactly once even though two parents require it
        CHECK((p.resolve_counts[{std::string("https://github.com/org/b.git"), "latest"}] == 1));
        CHECK((p.resolve_counts[{std::string("https://github.com/org/a.git"), "latest"}] == 1));
        CHECK((p.resolve_counts[{std::string("https://github.com/org/c.git"), "latest"}] == 1));
    }

    // ---- separate refs resolve separately ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"b", {"github:org/b", "latest"}}});
        p.manifests["c"] = make_manifest("c", {{"b", {"github:org/b", "v1"}}});
        p.manifests["b"] = make_manifest("b");
        p.ref_commits[{"https://github.com/org/b.git", "latest"}] = "X";
        p.ref_commits[{"https://github.com/org/b.git", "v1"}] = "X";  // same commit -> shared, distinct memo keys
        for (const char* n : {"a", "c"}) p.ref_commits[{std::string("https://github.com/org/") + n + ".git", "latest"}] = std::string(n) + "C";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(resolve(root_manifest({{"a", {"github:org/a", "latest"}}, {"c", {"github:org/c", "latest"}}}), root_dir,
                      package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, out, error));
        CHECK((p.resolve_counts[{std::string("https://github.com/org/b.git"), "latest"}] == 1));
        CHECK((p.resolve_counts[{std::string("https://github.com/org/b.git"), "v1"}] == 1));
    }

    // ---- insertion/discovery order independence + byte-identical serialization ----
    {
        // roots processed lexicographically: bar before foo regardless of
        // insertion order in the manifest map.
        FakeProvider p;
        p.manifests["foo"] = make_manifest("foo");
        p.manifests["bar"] = make_manifest("bar");
        p.ref_commits[{"https://github.com/org/s.git", "latest"}] = "X";
        const Manifest root1 = root_manifest({{"foo", {"github:org/s", "latest"}}, {"bar", {"github:org/s", "latest"}}});
        const Manifest root2 = root_manifest({{"bar", {"github:org/s", "latest"}}, {"foo", {"github:org/s", "latest"}}});
        package_graph::ResolveOutcome o1, o2; std::string error;
        CHECK(resolve(root1, root_dir, package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, o1, error));
        CHECK(resolve(root2, root_dir, package_graph::ResolveMode::ResolveUpdate, nullptr, nullptr, p, o2, error));
        CHECK(package_graph::graph_lock_text(o1.graph) == package_graph::graph_lock_text(o2.graph));
    }

    // ---- LockedInstall mode: zero remote ref resolutions, offline ----
    {
        FakeProvider p;
        package_graph::GraphLock locked;
        locked.packages["a"] = {"https://github.com/org/a.git", "X", {}};
        package_graph::ResolveOutcome out; std::string error;
        CHECK(resolve(root_manifest({{"a", {"github:org/a", "latest"}}}), root_dir,
                      package_graph::ResolveMode::LockedInstall, &locked, nullptr, p, out, error));
        CHECK(p.resolve_calls_total == 0);
        CHECK(out.graph.packages.at("a").commit == "X");
    }

    // ---- LockedInstall: missing locked node rejected ----
    {
        FakeProvider p;
        package_graph::GraphLock locked;  // empty
        package_graph::ResolveOutcome out; std::string error;
        CHECK(!resolve(root_manifest({{"a", {"github:org/a", "latest"}}}), root_dir,
                       package_graph::ResolveMode::LockedInstall, &locked, nullptr, p, out, error));
    }

    // ---- LockedInstall: lock/manifest edge mismatch rejected (missing edge target) ----
    {
        FakeProvider p;
        package_graph::GraphLock locked;
        locked.packages["a"] = {"https://github.com/org/a.git", "X",
                                {{"z", {"github:org/z", "latest"}}}};  // z not a node
        package_graph::ResolveOutcome out; std::string error;
        CHECK(!resolve(root_manifest({{"a", {"github:org/a", "latest"}}}), root_dir,
                       package_graph::ResolveMode::LockedInstall, &locked, nullptr, p, out, error));
    }

    // ---- Targeted update: compatible shared node (a refreshed keeps b at X, c preserved also X) ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"b", {"github:org/b", "latest"}}});
        p.manifests["c"] = make_manifest("c", {{"b", {"github:org/b", "v1.2.3"}}});
        p.manifests["b"] = make_manifest("b");
        p.ref_commits[{"https://github.com/org/a.git", "latest"}] = "X";
        p.ref_commits[{"https://github.com/org/b.git", "latest"}] = "Y";
        p.ref_commits[{"https://github.com/org/b.git", "v1.2.3"}] = "Y";  // refreshed a's b and locked c's b both -> Y
        package_graph::GraphLock locked;
        locked.packages["a"] = {"https://github.com/org/a.git", "X", {{"b", {"github:org/b", "latest"}}}};
        locked.packages["c"] = {"https://github.com/org/c.git", "C", {{"b", {"github:org/b", "v1.2.3"}}}};
        locked.packages["b"] = {"https://github.com/org/b.git", "Y", {}};
        const std::string target = "a";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(resolve(root_manifest({{"a", {"github:org/a", "latest"}}, {"c", {"github:org/c", "latest"}}}), root_dir,
                      package_graph::ResolveMode::TargetedUpdate, &locked, &target, p, out, error));
        // b refreshed to Y globally; c preserved (source C); both compatible with b@Y
        CHECK(out.graph.packages.at("b").commit == "Y");
        CHECK(out.graph.packages.at("c").commit == "C");
    }

    // ---- Targeted update: conflicting shared node (refreshed a wants b@Y, locked c needs b@X) ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a", {{"b", {"github:org/b", "latest"}}});
        p.manifests["b"] = make_manifest("b");
        p.ref_commits[{"https://github.com/org/a.git", "latest"}] = "X";
        p.ref_commits[{"https://github.com/org/b.git", "latest"}] = "Y";  // a now resolves b to Y
        package_graph::GraphLock locked;
        locked.packages["a"] = {"https://github.com/org/a.git", "X", {{"b", {"github:org/b", "latest"}}}};
        locked.packages["c"] = {"https://github.com/org/c.git", "C", {{"b", {"github:org/b", "v1.2.3"}}}};
        locked.packages["b"] = {"https://github.com/org/b.git", "X", {}};  // locked b at X
        const std::string target = "a";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(!resolve(root_manifest({{"a", {"github:org/a", "latest"}}, {"c", {"github:org/c", "latest"}}}), root_dir,
                       package_graph::ResolveMode::TargetedUpdate, &locked, &target, p, out, error));
        CHECK(error.find("dependency conflict for package: b") != std::string::npos);
    }

    // ---- Targeted update: unrelated root preserved (c stays locked, never re-resolved) ----
    {
        FakeProvider p;
        p.manifests["a"] = make_manifest("a");
        p.manifests["c"] = make_manifest("c");
        p.ref_commits[{"https://github.com/org/a.git", "latest"}] = "X";
        // NOTE: no ref entry for c at all; if the resolver tried to resolve c it would fail.
        package_graph::GraphLock locked;
        locked.packages["a"] = {"https://github.com/org/a.git", "X", {}};
        locked.packages["c"] = {"https://github.com/org/c.git", "C", {}};
        const std::string target = "a";
        package_graph::ResolveOutcome out; std::string error;
        CHECK(resolve(root_manifest({{"a", {"github:org/a", "latest"}}, {"c", {"github:org/c", "latest"}}}), root_dir,
                      package_graph::ResolveMode::TargetedUpdate, &locked, &target, p, out, error));
        CHECK(out.graph.packages.at("c").commit == "C");
        CHECK(p.resolve_calls_total == 1);  // only a's ref resolved
    }

    if (failures) {
        std::fprintf(stderr, "package-graph-resolver: %d failure(s)\n", failures);
        return 1;
    }
    std::fprintf(stderr, "package-graph-resolver: PASS\n");
    return 0;
}