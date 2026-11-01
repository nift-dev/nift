#include "rt/module_graph.h"

#include "FileSystem.h"
#include "rt/module_loader.h"
#include "rt/module_scan.h"
#include "rt/package_resolve.h"
#include "rt/parser.h"
#include "rt/unit_check.h"

#include <algorithm>
#include <unordered_map>

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt {

namespace {

namespace fs = std::filesystem;

using Names = std::vector<std::string_view>;

struct Origin {
    std::string_view name;
    std::size_t module = 0;  // the module whose top level declares it
};

bool has(const Names& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

struct Frame {
    std::size_t unit = 0;      // the importing unit
    std::uint32_t offset = 0;  // its import statement
    bool at_import = false;    // spelled `@import`
};

class GraphBuilder {
public:
    GraphBuilder(ModuleGraph& graph, const ModuleGraphOptions& options) : graph_(graph), options_(options) {}

    // The walk stops at the first problem it finds (owner policy: one plain,
    // deterministic order; checkpoints/weekend-packages.md §4.6): a feature
    // refusal (the VM lacks a capability) or a program error (below).
    bool refuse(std::size_t where, std::uint32_t offset, std::string reason) {
        graph_.ok = false;
        graph_.reason = std::move(reason);
        graph_.where = where;
        graph_.offset = offset;
        graph_.positioned = true;
        stopped_ = true;
        return false;
    }

    // A program error at a recorded position; `importing` adds the statement
    // being imported as the innermost frame.
    bool error(std::size_t where, std::uint32_t offset, std::string id, const std::string& text,
               const Frame* importing = nullptr) {
        GraphError& e = graph_.error;
        e.id = std::move(id);
        e.where = where;
        e.offset = offset;
        e.positioned = true;
        std::string prefix;
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 6  // test-only (packages/mutants.sh)
        for (std::size_t f = 0; f < frames_.size() + (importing ? 1 : 0); ++f) prefix += "import: ";
#else
        for (const Frame& f : frames_) prefix += f.at_import ? "@import: " : "import: ";
        if (importing) prefix += importing->at_import ? "@import: " : "import: ";
#endif
        e.message = prefix + text;
        graph_.ok = false;
        graph_.program_error = true;
        stopped_ = true;
        return false;
    }

    // One initialisation of unit `index` (legacy's order: its imports run at
    // their statements, depth first).
    bool visit(std::size_t index, const Ast& ast, NodeId root, const fs::path& base, std::vector<fs::path>& active) {
        std::uint32_t misplaced_at = 0;
        if (const char* misplaced = first_misplaced(ast, root, misplaced_at))
            return refuse(index, misplaced_at, misplaced);
        // Names bound in this unit, with the module that originally declared
        // them: an import may rebind a name to the same declaration (the
        // shared instance, D-041), never to a different one.
        std::vector<Origin> bound;
        for (std::string_view name : importer_names(ast, root)) bound.push_back({name, index});
        Names provided;  // names earlier statements of a module bind (for `export`)
        const Node& program = ast.at(root);
        const NodeId* items = ast.list(program);
        for (std::uint32_t k = 0; k < program.count && !stopped_; ++k) {
            const Node& st = ast.at(items[k]);
            if (index > 0) {  // `return <value>` outside functions in a module, at the `return`
                const std::uint32_t ret = first_return(ast, items[k], true);
                if (ret != UINT32_MAX)
                    return error(index, ret, "module-error:return-value",
                                 std::string("return with a value is not allowed in ") +
                                     (!frames_.empty() && frames_.back().at_import ? "@import" : "import"));
            }
            if (st.kind == NodeKind::Export) {
                if (index > 0 && !check_export(index, ast, st, provided, bound)) return false;
                continue;
            }
            if (const std::string_view name = declared_name(ast, st); !name.empty()) provided.push_back(name);
            if (st.kind != NodeKind::Import) continue;
            std::size_t target = 0;
            const Frame frame{index, st.begin, (st.flags & kFlagTemplateImport) != 0};
            const int status = import(index, ast, st, items[k], base, active, frame, target);
            if (status <= 0) return false;  // the walk stopped
            const Module& mod = *graph_.modules[target];
            // In the module's export order: the first export that collides is
            // the one reported (it names the binding that collides).
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 25  // test-only (packages/mutants.sh)
            for (std::string_view name : options_.invocation_names)  // by invocation name, not export order
                if (index == 0 && has(mod.exports, name))
                    return error(index, st.begin, "import-scope:invocation-export",
                                 "export collides with existing binding: " + std::string(name), &frame);
#endif
            for (std::size_t e = 0; e < mod.exports.size(); ++e) {
                const Origin incoming{mod.exports[e], mod.export_origins[e]};
                if (invocation_export(index, incoming.name, options_.invocation_names))  // the entry's args/cmd
                    return error(index, st.begin, "import-scope:invocation-export",
                                 "export collides with existing binding: " + std::string(incoming.name), &frame);
                const auto same = std::find_if(bound.begin(), bound.end(),
                                               [&](const Origin& o) { return o.name == incoming.name; });
                if (same != bound.end()) {
                    if (same->module != incoming.module) return refuse(index, st.begin, "import-collision");
                    continue;  // the same declaration again: rebound
                }
                bound.push_back(incoming);
                provided.push_back(incoming.name);
            }
        }
        return !stopped_;
    }

    void finish() { packages_.reset(); }  // ends the package critical section

private:
    // -1: the walk stopped (a refusal or a program error); 1: imported.
    int import(std::size_t index, const Ast& ast, const Node& st, NodeId statement, const fs::path& base,
               std::vector<fs::path>& active, const Frame& frame, std::size_t& target) {
        std::string argument;
        if (!literal_argument(ast, st, argument)) return refuse(index, st.begin, "import-dynamic"), -1;
        fs::path root = graph_.modules[index]->package_root;  // inherited, as legacy's ModuleEnv
        bool provenance = false;  // legacy: frozen only for a package entry and relative imports below it
        fs::path path;
        const bool package = package_shaped(argument);
        if (package) {
            if (!packages_) packages_ = std::make_unique<PackageReader>(base);  // `base`: mutant 1 only
            const PackageResolution r = packages_->resolve(argument);
            if (!r.ok)
                return error(index, st.begin, r.lock_error ? "import-error:package-lock" : "import-error:package",
                             r.error, &frame), -1;
            path = r.entry;
            root = r.root;
            provenance = true;
        } else {
            std::replace(argument.begin(), argument.end(), '\\', '/');
            path = argument;
            const bool relative = path.is_relative();
            if (relative) path = base / path;
            path = fs::absolute(path).lexically_normal();
            provenance = relative && graph_.modules[index]->package_provenance;
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 16  // test-only (packages/mutants.sh)
            if (false)
#else
            if (relative && !root.empty() && !graph_.modules[index]->package_provenance)
#endif
                return error(index, st.begin, "import-error:package-escape",
                             "package-relative import has no frozen package provenance", &frame), -1;
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 10  // test-only (packages/mutants.sh)
            if (false)
#else
            if (relative && !root.empty() && !filesystem::path_within(root, path))
#endif
                return error(index, st.begin, "import-error:package-escape",
                             "package-relative import escapes package root: " + path.generic_string(), &frame), -1;
        }
        path = fs::absolute(path).lexically_normal();
        if (!options_.project_root.empty() &&
            !path_inside(fs::absolute(options_.project_root).lexically_normal(), path))
            return refuse(index, st.begin, "import-outside-project"), -1;
        const fs::path identity = module_identity(path);
        if (std::find(active.begin(), active.end(), identity) != active.end())
            return error(index, st.begin, "import-error:cycle", "script import cycle through " + path.generic_string(),
                         &frame), -1;
        bool fresh = false;
        const int loaded = load(index, st, path, identity, root, provenance, frame, target, fresh);
        if (loaded <= 0) return loaded;
        Module& mod = *graph_.modules[target];
        ++mod.legacy_initialisations;
        graph_.modules[index]->imports.push_back({statement, target});
        if (!fresh) {
            // Legacy runs the module again here (a fresh environment); the
            // VM initialises it once. The single policy point (InitPolicy).
            if (options_.policy == InitPolicy::RefuseReinitialisation)
                return refuse(index, st.begin, "import-reinitialised"), -1;
            return 1;
        }
        // Refused at the import itself (Review 8), as a check on entering it.
        if (active.size() >= kMaxModuleDepth) return refuse(index, st.begin, "import-limit"), -1;
        active.push_back(identity);
        frames_.push_back(frame);
        const bool ok = visit(target, *mod.ast, mod.root, mod.path.parent_path(), active);
        frames_.pop_back();
        active.pop_back();
        return ok ? 1 : -1;
    }

    int load(std::size_t importer, const Node& st, const fs::path& path, const fs::path& identity,
             const fs::path& package_root, bool provenance, const Frame& frame, std::size_t& index, bool& fresh) {
        const auto known = by_identity_.find(identity.generic_string());
        if (known != by_identity_.end()) {
            index = known->second;
            return 1;
        }
        if (graph_.modules.size() >= kMaxModules) return refuse(importer, st.begin, "import-limit"), -1;
        ModuleSource source = load_module_source(path);
        if (!source.found) {
            graph_.absent.push_back(path);
            return error(importer, st.begin, "import-error:missing", source.message, &frame), -1;
        }
        graph_.dependencies.push_back(path);
        auto mod = std::make_unique<Module>();
        mod->path = path;
        mod->identity = identity;
        mod->package_root = package_root;
        mod->package_provenance = provenance;
        mod->source = std::move(source.text);
        mod->ast = std::make_unique<Ast>(mod->source);
        const ParseResult parsed = parse_script(*mod->ast);
        mod->root = parsed.root;
        index = graph_.modules.size();
        fresh = true;
        by_identity_.emplace(identity.generic_string(), index);
        graph_.modules.push_back(std::move(mod));
        // A module is validated when it loads (as legacy, D-022 #3): a syntax
        // error is a program error; so is `export(<not a name>)`, which the
        // parser keeps as an Opaque statement; other Opaque text is refused.
        frames_.push_back(frame);
        std::uint32_t bad = 0;
        std::string message;
        bool ok = true;
        const Module& m = *graph_.modules[index];
        const NodeId opaque = parsed.ok ? first_opaque_node(*m.ast, m.root) : kNoNode;
        if (!module_prevalidates(m.source, bad, message)) ok = error(index, bad, "module-error:syntax", message);
        else if (!parsed.ok) ok = refuse(index, parsed.error.offset, "import-parse-error");
        else if (opaque != kNoNode && export_without_name(*m.ast, m.ast->at(opaque)))
            ok = error(index, m.ast->at(opaque).begin, "module-error:export-name", "export requires a binding name");
        else if (opaque != kNoNode)
            ok = refuse(index, m.ast->at(opaque).begin,
                        std::string("import-opaque:") + opaque_reason_name(m.ast->at(opaque).reason));
        frames_.pop_back();
        return ok ? 1 : -1;
    }

    // `export(x)` (modules only): x is bound by an earlier top-level statement
    // of the module (legacy checks at the export; exported values are taken at
    // the module's end).
    bool check_export(std::size_t index, const Ast& ast, const Node& st, const Names& provided,
                      const std::vector<Origin>& bound) {
        Module& mod = *graph_.modules[index];
        if (st.a == kNoNode || ast.at(st.a).kind != NodeKind::Ident)
            return error(index, st.begin, "module-error:export-name", "export requires a binding name");
        const std::string_view name = ast.text(ast.at(st.a));
        if (has(mod.exports, name))
            return error(index, st.begin, "module-error:export-duplicate", "duplicate export: " + std::string(name));
        if (!has(provided, name))
            return error(index, st.begin, "module-error:export-missing",
                         "export names no existing binding: " + std::string(name));
        const Node& program = ast.at(mod.root);
        const NodeId* items = ast.list(program);
        for (std::uint32_t k = 0; k < program.count; ++k) {
            const Node& decl = ast.at(items[k]);
            if (decl.kind == NodeKind::StructDecl && declared_name(ast, decl) == name)
                return refuse(index, st.begin, "export-struct");
        }
        std::size_t origin = index;
        for (const Origin& o : bound)
            if (o.name == name) origin = o.module;
        mod.exports.push_back(name);
        mod.export_origins.push_back(origin);
        return true;
    }

    ModuleGraph& graph_;
    const ModuleGraphOptions& options_;
    std::unordered_map<std::string, std::size_t> by_identity_;  // lookup only, never iterated
    std::unique_ptr<PackageReader> packages_;
    std::vector<Frame> frames_;   // the active imports, outermost first
    bool stopped_ = false;
};

} // namespace

ModuleGraph build_module_graph(const Ast& entry, NodeId root, const fs::path& entry_path,
                               const ModuleGraphOptions& options) {
    ModuleGraph graph;
    auto unit = std::make_unique<Module>();
    std::vector<fs::path> active;
    fs::path base = fs::current_path();
    if (!entry_path.empty()) {
        unit->path = fs::absolute(entry_path).lexically_normal();
        unit->identity = module_identity(unit->path);
        active.push_back(unit->identity);
        base = unit->path.parent_path();
    }
    unit->legacy_initialisations = 1;
    graph.modules.push_back(std::move(unit));
    GraphBuilder builder(graph, options);
    builder.visit(0, entry, root, base, active);
    builder.finish();
    return graph;
}

} // namespace nift::rt
