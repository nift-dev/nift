// Runtime 2.0 CP12 phase B: unit tests for the module graph (D-037,
// rt/module_graph.h). Every legacy semantics probe
// (tests/runtime2/semantics-modules) is planned under both initialisation
// policies (D-041's once per context, and the temporary refusal of units
// where today's legacy initialises again): accepted units get their modules, dependencies and legacy
// initialisation counts; the others the refusal reason, before execution.
// Run from the repository root. Hermetic: every graph is planned from a copy
// in a fresh temporary root (TempRoot) whose absent targets are checked
// absent first, so results never depend on what else is in the checkout.
#include "rt/module_graph.h"
#include "rt/module_loader.h"
#include "rt/parser.h"
#include "rt_test.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace nift::rt;
namespace fs = std::filesystem;

namespace {

const fs::path kProbes = "tests/runtime2/semantics-modules";
const fs::path kD041 = "tests/runtime2/modules-d041";

// A fresh, uniquely named directory under the system temporary directory,
// canonical (so resolved paths are deterministic), removed on destruction.
class TempRoot {
public:
    TempRoot() {
        std::random_device seed;
        std::mt19937_64 rng((static_cast<std::uint64_t>(seed()) << 32) ^ seed());
        const fs::path base = fs::canonical(fs::temp_directory_path());
        for (int attempt = 0; attempt < 100 && path_.empty(); ++attempt) {
            const fs::path candidate = base / ("nift-vm-modules-unit-" + std::to_string(rng()));
            std::error_code ec;
            if (fs::create_directory(candidate, ec) && !ec) path_ = candidate;
        }
        CHECK(!path_.empty());
    }
    ~TempRoot() {
        std::error_code ec;
#ifndef _WIN32
        for (const auto& e : fs::recursive_directory_iterator(path_, ec))
            fs::permissions(e.path(), fs::perms::owner_all, fs::perm_options::add, ec);
#endif
        fs::remove_all(path_, ec);
    }
    TempRoot(const TempRoot&) = delete;
    TempRoot& operator=(const TempRoot&) = delete;
    const fs::path& path() const { return path_; }

    // A copy of `from` (a probe's p/ directory) at path()/name; its main.f.
    fs::path copy(const fs::path& from, const std::string& name) const {
        const fs::path to = path_ / name;
        fs::create_directories(to);
        fs::copy(from, to, fs::copy_options::recursive);
        return to / "main.f";
    }
    void write(const fs::path& rel, const std::string& text) const {
        fs::create_directories((path_ / rel).parent_path());
        std::FILE* f = std::fopen((path_ / rel).string().c_str(), "wb");
        CHECK(f != nullptr);
        if (f == nullptr) return;
        std::fputs(text.c_str(), f);
        std::fclose(f);
    }

private:
    fs::path path_;
};

bool absent(const fs::path& p) {
    std::error_code ec;
    return !fs::exists(fs::symlink_status(p, ec));
}

struct Planned {
    std::string source;
    std::unique_ptr<Ast> ast;
    ModuleGraph graph;
};

Planned plan(const fs::path& entry, InitPolicy policy, const fs::path& project_root = {}) {
    Planned p;
    p.source = load_module_source(entry).text;
    p.ast = std::make_unique<Ast>(p.source);
    const ParseResult parsed = parse_script(*p.ast);
    CHECK(parsed.ok);
    ModuleGraphOptions options;
    options.policy = policy;
    options.project_root = project_root;
    p.graph = build_module_graph(*p.ast, parsed.root, entry, options);
    return p;
}

std::string verdict(const ModuleGraph& g) { return g.ok ? "ok" : g.reason; }

struct Expect {
    const char* probe;
    const char* once_per_context;  // InitPolicy::OncePerContext (D-041)
    const char* refuse;            // InitPolicy::RefuseReinitialisation (temporary)
};

// Graph-level verdicts; "ok" units may still be refused later by the
// compiler (free names, struct construction, ...).
const Expect kExpect[] = {
    {"cycle", "import-cycle", "import-cycle"},
    {"diamond", "ok", "import-reinitialised"},
    {"dynamic_path", "import-dynamic", "import-dynamic"},
    {"export_collision", "import-collision", "import-collision"},
    {"export_copy", "ok", "ok"},
    {"export_missing", "export-missing", "export-missing"},
    {"in_block", "import-nested", "import-nested"},
    {"in_function", "import-nested", "import-nested"},
    {"missing", "import-missing", "import-missing"},
    {"module_bare_return", "ok", "ok"},
    {"module_error", "ok", "ok"},  // a run-time error inside the module
    {"module_free_unbound", "ok", "ok"},
    {"module_private", "ok", "ok"},
    {"module_private_late", "ok", "ok"},
    {"module_return", "module-return", "module-return"},
    {"module_vs_caller_local", "ok", "ok"},
    {"nested_relative", "ok", "ok"},
    {"not_reexported", "ok", "ok"},
    {"package_missing", "import-package", "import-package"},
    {"repeated", "ok", "import-reinitialised"}  /* a rebind */,
    {"same_file_two_spellings", "ok", "import-reinitialised"},
    {"self_cycle", "import-cycle", "import-cycle"},
    {"side_effect_order", "ok", "ok"},
    {"struct_export", "export-struct", "export-struct"},
    {"template_import", "ok", "ok"},
};

void check_probes() {
    std::size_t seen = 0;
    for (const auto& entry : fs::directory_iterator(kProbes)) seen += fs::exists(entry.path() / "p" / "main.f");
    CHECK(seen == sizeof(kExpect) / sizeof(kExpect[0]));  // a new probe needs an expectation here
    const TempRoot root;
    for (const Expect& e : kExpect) {
        const fs::path main = root.copy(kProbes / e.probe / "p", e.probe);
        if (std::string(e.probe) == "missing") CHECK(absent(main.parent_path() / "nope.nft"));
        const Planned a = plan(main, InitPolicy::OncePerContext);
        const Planned c = plan(main, InitPolicy::RefuseReinitialisation);
        const bool ok = verdict(a.graph) == e.once_per_context && verdict(c.graph) == e.refuse;
        CHECK(ok);
        if (!ok)
            std::fprintf(stderr, "  %s: A %s (want %s), C %s (want %s)\n", e.probe, verdict(a.graph).c_str(),
                         e.once_per_context, verdict(c.graph).c_str(), e.refuse);
    }
}

// The D-041 fixtures (tests/runtime2/modules-d041) that are scripts.
void check_d041_fixtures() {
    const TempRoot root;
    const struct {
        const char* fixture;
        const char* verdict;  // under OncePerContext
    } expect[] = {
        {"repeated", "ok"}, {"repeated_two_spellings", "ok"}, {"export_clash", "import-collision"},
        {"diamond", "ok"}, {"diamond_stateful", "ok"}, {"nested_chain", "ok"}, {"nested_shared", "ok"},
        {"circular", "import-cycle"}, {"self_cycle", "import-cycle"}, {"stateful_export_binding", "ok"},
        {"stateful_array_binding", "ok"}, {"failed_init", "import-nested"},  // imports inside `try`
        {"repeated_binding", "ok"}, {"reexport_binding", "ok"}, {"module_fn_lexical", "ok"},
        {"doc_math", "ok"}, {"doc_try_import", "import-nested"},
    };
    for (const auto& e : expect) {
        const Planned p = plan(root.copy(kD041 / e.fixture / "p", e.fixture), InitPolicy::OncePerContext);
        CHECK(verdict(p.graph) == e.verdict);
        if (verdict(p.graph) != e.verdict)
            std::fprintf(stderr, "  %s: %s (want %s)\n", e.fixture, verdict(p.graph).c_str(), e.verdict);
    }
}

// Rebinding: the same declaration through two paths is one binding; a
// different declaration of the name, even declared later, is a collision.
void check_rebinding() {
    const TempRoot root;
    const fs::path dir = root.path();
    const auto write = [&](const char* name, const char* text) { root.write(name, text); };
    write("c.nft", "fn(cf()) { return 1 }\nexport(cf)\n");
    write("b.nft", "import(\"c.nft\")\nexport(cf)\n");
    write("d.nft", "fn(cf()) { return 2 }\nexport(cf)\n");
    write("ok.f", "import(\"c.nft\")\nimport(\"b.nft\")\nprint(cf())\n");
    write("clash.f", "import(\"c.nft\")\nimport(\"d.nft\")\n");
    write("late.f", "import(\"c.nft\")\nfn(cf()) { return 3 }\n");
    const Planned ok = plan(dir / "ok.f", InitPolicy::OncePerContext);
    CHECK(verdict(ok.graph) == "ok");
    CHECK(ok.graph.modules.size() == 3 && ok.graph.modules[2]->export_origins.size() == 1 &&
          ok.graph.modules[2]->export_origins[0] == 1);  // b re-exports c's cf
    CHECK(verdict(plan(dir / "clash.f", InitPolicy::OncePerContext).graph) == "import-collision");
    CHECK(verdict(plan(dir / "late.f", InitPolicy::OncePerContext).graph) == "import-collision");
}

std::string name_of(const fs::path& p) { return p.filename().string(); }

void check_details() {
    const TempRoot root;
    const auto probe = [&](const char* name) { return root.copy(kProbes / name / "p", name); };
    {
        // Resolution relative to each importer. Dependencies are compared by
        // full path (the copy lives under a random root) in first-read order,
        // which is a contract (module_graph.h: legacy's read order).
        const fs::path main = probe("nested_relative");
        const fs::path dir = main.parent_path();
        const Planned p = plan(main, InitPolicy::RefuseReinitialisation);
        CHECK(p.graph.ok && p.graph.modules.size() == 4);
        const std::vector<fs::path> want = {dir / "sub" / "s.nft", dir / "m2.nft" /* ../ from sub/ */,
                                            dir / "sub" / "t.nft"};
        CHECK(p.graph.dependencies == want);
        if (p.graph.dependencies != want)
            for (const fs::path& d : p.graph.dependencies)
                std::fprintf(stderr, "  dependency %s\n", d.string().c_str());
        CHECK(p.graph.modules[0]->imports.size() == 1 && p.graph.modules[1]->imports.size() == 2);
        CHECK(p.graph.modules[1]->exports.size() == 1 && p.graph.modules[1]->exports[0] == "sf");
    }
    {
        // Diamond under A: c read and parsed once, legacy runs it twice.
        const Planned p = plan(probe("diamond"), InitPolicy::OncePerContext);
        CHECK(p.graph.ok && p.graph.modules.size() == 4 && p.graph.dependencies.size() == 3);
        std::size_t twice = 0;
        for (const auto& m : p.graph.modules) twice += m->legacy_initialisations == 2 && name_of(m->path) == "c.nft";
        CHECK(twice == 1);
    }
    {
        // Two spellings, one identity.
        const Planned p = plan(probe("same_file_two_spellings"), InitPolicy::OncePerContext);
        CHECK(p.graph.modules.size() == 2 && p.graph.modules[1]->legacy_initialisations == 2);
    }
    {
        // Refusals inside a module name that module.
        const Planned p = plan(probe("module_return"), InitPolicy::RefuseReinitialisation);
        CHECK(p.graph.where == 1 && name_of(p.graph.modules[1]->path) == "ret.nft");
    }
    {
        // The project root (builds): a probe's own directory admits it, a
        // sibling directory does not.
        const fs::path main = root.copy(kProbes / "nested_relative" / "p", "nested_relative_root");
        CHECK(plan(main, InitPolicy::RefuseReinitialisation, main.parent_path()).graph.ok);
        CHECK(verdict(plan(main, InitPolicy::RefuseReinitialisation, main.parent_path() / "sub").graph) ==
              "import-outside-project");
    }
    CHECK(module_identity(root.path() / "x" / ".." / "y") == module_identity(root.path() / "y"));
}

// Absent modules, hermetically: each import target is checked absent (or of
// the intended kind) before planning; the graph records it as absent, never
// as a dependency, refused at the import with `import-missing`.
void check_absent_modules() {
    const TempRoot root;
    const std::string main = "print(\"before\")\nimport(\"nope.nft\")\nprint(\"after\")\n";
    root.write("plain/main.f", main);
    CHECK(absent(root.path() / "plain" / "nope.nft"));
    const Planned p = plan(root.path() / "plain" / "main.f", InitPolicy::OncePerContext);
    CHECK(verdict(p.graph) == "import-missing");
    CHECK(p.graph.dependencies.empty() && p.graph.absent.size() == 1);
    CHECK(!p.graph.absent.empty() && p.graph.absent[0] == root.path() / "plain" / "nope.nft");
    CHECK(p.graph.where == 0 && p.graph.offset == p.source.find("import(\"nope"));
    // Nested: a module's missing import is resolved against that module's
    // directory, recorded absent, and the reading module is a dependency.
    root.write("nested/main.f", "import(\"sub/a.nft\")\n");
    root.write("nested/sub/a.nft", "import(\"gone.nft\")\n");
    CHECK(absent(root.path() / "nested" / "sub" / "gone.nft"));
    const Planned n = plan(root.path() / "nested" / "main.f", InitPolicy::OncePerContext);
    CHECK(verdict(n.graph) == "import-missing" && n.graph.where == 1);
    CHECK(n.graph.dependencies.size() == 1 && n.graph.absent.size() == 1);
    CHECK(!n.graph.absent.empty() && n.graph.absent[0] == root.path() / "nested" / "sub" / "gone.nft");
}

// Regression (cp12b-gates5): a never-opened std::ifstream is not in a failed
// state; the loader must report every unreadable target as not found, with
// legacy's message — a missing file, a directory, a dangling symlink, a FIFO
// and a file without read permission (when the process cannot read it; a
// process that can, e.g. root, reads it, which is also a defined result).
void check_loader_unreadable() {
    const TempRoot root;
    const auto not_found = [](const fs::path& path) {
        const ModuleSource s = load_module_source(path);
        CHECK(!s.found && s.text.empty() && s.message == "script is not readable: " + path.generic_string());
        if (s.found) std::fprintf(stderr, "  found: %s\n", path.string().c_str());
    };
    const fs::path missing = root.path() / "missing.nft";
    CHECK(absent(missing));
    not_found(missing);
    fs::create_directory(root.path() / "dir.nft");
    not_found(root.path() / "dir.nft");
    root.write("real.nft", "x := 1\n");
    const ModuleSource real = load_module_source(root.path() / "real.nft");
    CHECK(real.found && real.text == "x := 1\n" && real.message.empty());
#ifndef _WIN32
    std::error_code ec;
    fs::create_symlink(root.path() / "nowhere.nft", root.path() / "dangling.nft", ec);
    CHECK(!ec && absent(root.path() / "nowhere.nft"));
    not_found(root.path() / "dangling.nft");
    CHECK(::mkfifo((root.path() / "fifo.nft").c_str(), 0600) == 0);
    not_found(root.path() / "fifo.nft");  // not a regular file: never opened, never blocks
    root.write("locked.nft", "y := 2\n");
    fs::permissions(root.path() / "locked.nft", fs::perms::none, ec);
    CHECK(!ec);
    std::FILE* probe = std::fopen((root.path() / "locked.nft").c_str(), "rb");
    const bool readable = probe != nullptr;
    if (probe != nullptr) std::fclose(probe);
    const ModuleSource locked = load_module_source(root.path() / "locked.nft");
    if (readable) {
        CHECK(locked.found && locked.text == "y := 2\n");  // the process may read it (root)
    } else {
        not_found(root.path() / "locked.nft");
    }
#endif
}

} // namespace

int main() {
    check_probes();
    check_details();
    check_d041_fixtures();
    check_rebinding();
    check_absent_modules();
    check_loader_unreadable();
    return rt_test::finish("rt vm modules unit");
}
