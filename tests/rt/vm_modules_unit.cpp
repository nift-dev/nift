// Runtime 2.0 CP12 phase B: unit tests for the module graph (D-037,
// rt/module_graph.h). Every legacy semantics probe
// (tests/runtime2/semantics-modules) is planned under both initialisation
// policies (D-041's once per context, and the temporary refusal of units
// where today's legacy initialises again): accepted units get their modules, dependencies and legacy
// initialisation counts; the others the refusal reason, before execution.
// Run from the repository root.
#include "rt/module_graph.h"
#include "rt/module_loader.h"
#include "rt/parser.h"
#include "rt_test.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace nift::rt;
namespace fs = std::filesystem;

namespace {

const fs::path kProbes = "tests/runtime2/semantics-modules";

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
    for (const Expect& e : kExpect) {
        const fs::path main = kProbes / e.probe / "p" / "main.f";
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
    const fs::path dir = "tests/runtime2/modules-d041";
    const struct {
        const char* fixture;
        const char* verdict;  // under OncePerContext
    } expect[] = {
        {"repeated", "ok"}, {"repeated_two_spellings", "ok"}, {"export_clash", "import-collision"},
        {"diamond", "ok"}, {"diamond_stateful", "ok"}, {"nested_chain", "ok"}, {"nested_shared", "ok"},
        {"circular", "import-cycle"}, {"self_cycle", "import-cycle"}, {"stateful_export_copy", "ok"},
        {"stateful_array_copy", "ok"}, {"failed_init", "import-nested"},  // imports inside `try`
    };
    for (const auto& e : expect) {
        const Planned p = plan(dir / e.fixture / "p" / "main.f", InitPolicy::OncePerContext);
        CHECK(verdict(p.graph) == e.verdict);
        if (verdict(p.graph) != e.verdict)
            std::fprintf(stderr, "  %s: %s (want %s)\n", e.fixture, verdict(p.graph).c_str(), e.verdict);
    }
}

// Rebinding: the same declaration through two paths is one binding; a
// different declaration of the name, even declared later, is a collision.
void check_rebinding() {
    const fs::path dir = fs::temp_directory_path() / "nift-vm-modules-unit";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const auto write = [&](const char* name, const char* text) {
        std::FILE* f = std::fopen((dir / name).string().c_str(), "wb");
        if (f == nullptr) return;
        std::fputs(text, f);
        std::fclose(f);
    };
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
    fs::remove_all(dir);
}

std::string name_of(const fs::path& p) { return p.filename().string(); }

void check_details() {
    {
        // Resolution relative to each importer, dependencies in first-read order.
        const Planned p = plan(kProbes / "nested_relative" / "p" / "main.f", InitPolicy::RefuseReinitialisation);
        CHECK(p.graph.ok && p.graph.modules.size() == 4);
        CHECK(p.graph.dependencies.size() == 3);
        if (p.graph.dependencies.size() == 3) {
            CHECK(name_of(p.graph.dependencies[0]) == "s.nft");
            CHECK(name_of(p.graph.dependencies[1]) == "m2.nft");
            CHECK(p.graph.dependencies[1].parent_path().filename() == "p");  // ../ from sub/
            CHECK(name_of(p.graph.dependencies[2]) == "t.nft");
            CHECK(p.graph.dependencies[2].parent_path().filename() == "sub");
        }
        CHECK(p.graph.modules[0]->imports.size() == 1 && p.graph.modules[1]->imports.size() == 2);
        CHECK(p.graph.modules[1]->exports.size() == 1 && p.graph.modules[1]->exports[0] == "sf");
    }
    {
        // Diamond under A: c read and parsed once, legacy runs it twice.
        const Planned p = plan(kProbes / "diamond" / "p" / "main.f", InitPolicy::OncePerContext);
        CHECK(p.graph.ok && p.graph.modules.size() == 4 && p.graph.dependencies.size() == 3);
        std::size_t twice = 0;
        for (const auto& m : p.graph.modules) twice += m->legacy_initialisations == 2 && name_of(m->path) == "c.nft";
        CHECK(twice == 1);
    }
    {
        // Two spellings, one identity.
        const Planned p = plan(kProbes / "same_file_two_spellings" / "p" / "main.f", InitPolicy::OncePerContext);
        CHECK(p.graph.modules.size() == 2 && p.graph.modules[1]->legacy_initialisations == 2);
    }
    {
        // A missing module is recorded as absent, not as a dependency.
        const Planned p = plan(kProbes / "missing" / "p" / "main.f", InitPolicy::RefuseReinitialisation);
        CHECK(p.graph.absent.size() == 1 && p.graph.dependencies.empty());
        CHECK(!p.graph.absent.empty() && name_of(p.graph.absent[0]) == "nope.nft");
        CHECK(p.graph.where == 0 && p.graph.offset == p.source.find("import(\"nope"));
    }
    {
        // Refusals inside a module name that module.
        const Planned p = plan(kProbes / "module_return" / "p" / "main.f", InitPolicy::RefuseReinitialisation);
        CHECK(p.graph.where == 1 && name_of(p.graph.modules[1]->path) == "ret.nft");
    }
    {
        // The project root (builds): a probe's own directory admits it, a
        // sibling directory does not.
        const fs::path main = kProbes / "nested_relative" / "p" / "main.f";
        CHECK(plan(main, InitPolicy::RefuseReinitialisation, kProbes / "nested_relative" / "p").graph.ok);
        CHECK(verdict(plan(main, InitPolicy::RefuseReinitialisation, kProbes / "nested_relative" / "p" / "sub").graph) ==
              "import-outside-project");
    }
    CHECK(module_identity("tests/runtime2/../runtime2") == module_identity("tests/runtime2"));
    CHECK(!load_module_source(kProbes / "missing" / "p" / "nope.nft").found);
}

} // namespace

int main() {
    check_probes();
    check_details();
    check_d041_fixtures();
    check_rebinding();
    return rt_test::finish("rt vm modules unit");
}
