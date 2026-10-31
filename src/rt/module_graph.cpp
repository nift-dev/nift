#include "rt/module_graph.h"

#include "FileSystem.h"
#include "rt/literal.h"
#include "rt/module_loader.h"
#include "rt/module_scan.h"
#include "rt/package_resolve.h"
#include "rt/parser.h"
#include "rt/string.h"
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
    std::uint32_t item = 0;    // that statement's top-level index in the importer
};

class GraphBuilder {
public:
    GraphBuilder(ModuleGraph& graph, const ModuleGraphOptions& options) : graph_(graph), options_(options) {}

    // A feature refusal: the first is kept (at a recorded position); the
    // walk goes on (a later program error wins). The active chain is tainted:
    // its units' bound names may be incomplete, so they skip export-missing.
    void refuse(std::size_t where, std::uint32_t offset, std::string reason) {
        if (graph_.ok && !graph_.program_error) {
            graph_.ok = false;
            graph_.reason = std::move(reason);
            graph_.where = where;
            graph_.offset = offset;
            graph_.positioned = true;
        }
        tainted_.resize(graph_.modules.size(), false);
        tainted_[where] = true;
        for (const Frame& f : frames_) tainted_[f.unit] = true;
    }

    // A program error at a recorded position; `importing` adds the statement
    // being imported as the innermost frame. Ends the walk.
    bool error(std::size_t where, std::uint32_t offset, std::string id, const std::string& text,
               const Frame* importing = nullptr) {
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 11  // test-only (packages/mutants.sh)
        if (!graph_.ok && !graph_.program_error) { stopped_ = true; return false; }
#endif
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
        early_return(importing);
        graph_.ok = false;
        graph_.program_error = true;
        stopped_ = true;
        return false;
    }

    // One initialisation of unit `index` (legacy's order: its imports run at
    // their statements, depth first).
    bool visit(std::size_t index, const Ast& ast, NodeId root, const fs::path& base, std::vector<fs::path>& active) {
        check_placement(index, ast, root);
        // Names bound in this unit, with the module that originally declared
        // them: an import may rebind a name to the same declaration (the
        // shared instance, D-041), never to a different one.
        std::vector<Origin> bound;
        for (std::string_view name : importer_names(ast, root)) bound.push_back({name, index});
        Names provided;  // names earlier statements of a module bind (for `export`)
        const Node& program = ast.at(root);
        const NodeId* items = ast.list(program);
        for (std::uint32_t step = 0; step < program.count && !stopped_; ++step) {
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 12  // test-only (packages/mutants.sh)
            const std::uint32_t k = program.count - 1 - step;
#else
            const std::uint32_t k = step;
#endif
            const Node& st = ast.at(items[k]);
            current_item_ = k;
            if (index > 0) {
                const std::uint32_t ret = first_return(ast, items[k], true);
                if (ret != UINT32_MAX) return return_error(index, ret);
            }
            if (index > 0 && export_without_name(ast, st))
                return error(index, st.begin, "module-error:export-name", "export requires a binding name");
            if (st.kind == NodeKind::Export) {
                if (index > 0 && !check_export(index, ast, st, provided, bound)) return false;
                continue;
            }
            if (const std::string_view name = declared_name(ast, st); !name.empty()) provided.push_back(name);
            if (st.kind != NodeKind::Import) continue;
            std::size_t target = 0;
            const Frame frame{index, st.begin, (st.flags & kFlagTemplateImport) != 0, k};
            const int status = import(index, ast, st, items[k], base, active, frame, target);
            if (status < 0) return false;  // a program error (or the walk stopped)
            if (status == 0) continue;     // refused: no exports bound
            const Module& mod = *graph_.modules[target];
            for (std::size_t e = 0; e < mod.exports.size(); ++e) {
                const Origin incoming{mod.exports[e], mod.export_origins[e]};
                const auto same = std::find_if(bound.begin(), bound.end(),
                                               [&](const Origin& o) { return o.name == incoming.name; });
                if (same != bound.end()) {
                    if (same->module != incoming.module) refuse(index, st.begin, "import-collision");
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
    // -1: stop (program error or limit); 0: refused, go on; 1: imported.
    int import(std::size_t index, const Ast& ast, const Node& st, NodeId statement, const fs::path& base,
               std::vector<fs::path>& active, const Frame& frame, std::size_t& target) {
        std::string argument;
        if (!literal_argument(ast, st, argument)) return refuse(index, st.begin, "import-dynamic"), 0;
        fs::path root = graph_.modules[index]->package_root;  // inherited, as legacy's ModuleEnv
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
        } else {
            std::replace(argument.begin(), argument.end(), '\\', '/');
            path = argument;
            const bool relative = path.is_relative();
            if (relative) path = base / path;
            path = fs::absolute(path).lexically_normal();
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
            return refuse(index, st.begin, "import-outside-project"), 0;
        const fs::path identity = module_identity(path);
        if (std::find(active.begin(), active.end(), identity) != active.end())
            return error(index, st.begin, "import-error:cycle", "script import cycle through " + path.generic_string(),
                         &frame), -1;
        bool fresh = false, parsed = true;
        const int loaded = load(index, st, path, identity, root, frame, target, fresh, parsed);
        if (loaded <= 0) return loaded;
        Module& mod = *graph_.modules[target];
        ++mod.legacy_initialisations;
        graph_.modules[index]->imports.push_back({statement, target});
        if (!fresh) {
            // Legacy runs the module again here (a fresh environment); the
            // VM initialises it once. The single policy point (InitPolicy).
            if (options_.policy == InitPolicy::RefuseReinitialisation) refuse(index, st.begin, "import-reinitialised");
            return 1;
        }
        if (!parsed) return 0;  // refused (import-parse-error): nothing to visit
        // The import that would nest initialisations deeper than the limit is
        // refused at its own statement (Review 8: not at 1:1 of the module it
        // names). The same condition and order as a check on entering it.
        if (active.size() >= kMaxModuleDepth) return refuse(index, st.begin, "import-limit"), stopped_ = true, -1;
        active.push_back(identity);
        frames_.push_back(frame);
        const bool ok = visit(target, *mod.ast, mod.root, mod.path.parent_path(), active);
        frames_.pop_back();
        active.pop_back();
        return ok ? 1 : -1;
    }

    static bool literal_argument(const Ast& ast, const Node& st, std::string& out) {
        if (st.a == kNoNode) return false;
        const Node& arg = ast.at(st.a);
        Value value;
        if (arg.kind != NodeKind::String || arg.count != 0) return false;
        if (!decode_string_literal(ast.text(arg), value)) return false;
        out = std::string(string_view_of(value));
        return true;  // empty: a package name (legacy: invalid package name)
    }

    int load(std::size_t importer, const Node& st, const fs::path& path, const fs::path& identity,
             const fs::path& package_root, const Frame& frame, std::size_t& index, bool& fresh, bool& parsed_ok) {
        const auto known = by_identity_.find(identity.generic_string());
        if (known != by_identity_.end()) {
            index = known->second;
            return 1;
        }
        if (graph_.modules.size() >= kMaxModules) {
            refuse(importer, st.begin, "import-limit");
            stopped_ = true;
            return -1;
        }
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
        mod->source = std::move(source.text);
        mod->ast = std::make_unique<Ast>(mod->source);
        const ParseResult parsed = parse_script(*mod->ast);
        mod->root = parsed.root;
        index = graph_.modules.size();
        fresh = true;
        by_identity_.emplace(identity.generic_string(), index);
        graph_.modules.push_back(std::move(mod));
        if (!parsed.ok) {
            parsed_ok = false;
            frames_.push_back(frame);
            refuse(index, parsed.error.offset, "import-parse-error");
            frames_.pop_back();
            return 1;
        }
        const Module& m = *graph_.modules[index];
        const NodeId opaque = first_opaque_node(*m.ast, m.root);
        if (opaque != kNoNode) {  // refused; the walk still visits it (an `export(<expr>)` is a program error)
            frames_.push_back(frame);
            refuse(index, m.ast->at(opaque).begin,
                   std::string("import-opaque:") + opaque_reason_name(m.ast->at(opaque).reason));
            frames_.pop_back();
        }
        return 1;
    }

    // `return <value>` outside functions in a module: legacy reports it at the
    // module's import, after the module ran up to it.
    bool return_error(std::size_t index, std::uint32_t at) {
        const Frame importing = frames_.back();
        frames_.pop_back();
        graph_.error.return_note = true;
        graph_.error.return_where = index;
        graph_.error.return_offset = at;
        const std::string text = std::string("return with a value is not allowed in ") +
                                 (importing.at_import ? "@import" : "import");
        current_item_ = importing.item;
        error(importing.unit, importing.offset, "module-error:return-value", text, &importing);
        frames_.push_back(importing);
        return false;
    }

    // The first top-level `return` before the failing statement on the active
    // chain, outermost first: legacy would stop there and never reach it.
    void early_return(const Frame* importing) {
        std::vector<std::pair<std::size_t, std::uint32_t>> chain;  // (unit, top-level item)
        for (const Frame& f : frames_) chain.push_back({f.unit, f.item});
        const std::size_t innermost = importing ? importing->unit : graph_.error.where;
        chain.push_back({innermost, current_item_});
        for (std::size_t c = 0; c < chain.size(); ++c) {
            const auto [unit, item] = chain[c];
            const Ast& ast = unit == 0 ? *entry_ : *graph_.modules[unit]->ast;
            const NodeId root = unit == 0 ? entry_root_ : graph_.modules[unit]->root;
            const Node& program = ast.at(root);
            const NodeId* items = ast.list(program);
            for (std::uint32_t k = 0; k < item && k < program.count; ++k) {
                const std::uint32_t at = first_return(ast, items[k], false);
                if (at == UINT32_MAX) continue;
                graph_.error.early_return = true;
                graph_.error.early_where = unit;
                graph_.error.early_offset = at;
                return;
            }
        }
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
        tainted_.resize(graph_.modules.size(), false);
        if (!has(provided, name)) {
            if (tainted_[index]) return true;  // names may be missing after a refusal: no claim
            return error(index, st.begin, "module-error:export-missing",
                         "export names no existing binding: " + std::string(name));
        }
        const Node& program = ast.at(mod.root);
        const NodeId* items = ast.list(program);
        for (std::uint32_t k = 0; k < program.count; ++k) {
            const Node& decl = ast.at(items[k]);
            if (decl.kind == NodeKind::StructDecl && declared_name(ast, decl) == name) {
                refuse(index, st.begin, "export-struct");
                return true;
            }
        }
        std::size_t origin = index;
        for (const Origin& o : bound)
            if (o.name == name) origin = o.module;
        mod.exports.push_back(name);
        mod.export_origins.push_back(origin);
        return true;
    }

    // Imports and exports only as top-level statements (refused otherwise,
    // at the first in text order). `return <value>` is checked in order.
    void check_placement(std::size_t index, const Ast& ast, NodeId root) {
        const Node& program = ast.at(root);
        const NodeId* items = ast.list(program);
        const NodeId* last = items + program.count;
        const auto top = [&](NodeId id) { return std::find(items, last, id) != last; };
        std::uint32_t at = UINT32_MAX;
        const char* reason = nullptr;
        walk(ast, root, [&](NodeId id, bool) {
            const Node& n = ast.at(id);
            const char* r = n.kind == NodeKind::Import && !top(id)   ? "import-nested"
                            : n.kind == NodeKind::Export && !top(id) ? "export-nested"
                                                                     : nullptr;
            if (r && n.begin < at) { at = n.begin; reason = r; }
        });
        if (reason) refuse(index, at, reason);
    }

public:
    const Ast* entry_ = nullptr;
    NodeId entry_root_ = kNoNode;

private:
    ModuleGraph& graph_;
    const ModuleGraphOptions& options_;
    std::unordered_map<std::string, std::size_t> by_identity_;  // lookup only, never iterated
    std::unique_ptr<PackageReader> packages_;
    std::vector<Frame> frames_;   // the active imports, outermost first
    std::vector<bool> tainted_;   // per module: a refusal may have hidden bound names
    std::uint32_t current_item_ = 0;
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
    builder.entry_ = &entry;
    builder.entry_root_ = root;
    builder.visit(0, entry, root, base, active);
    builder.finish();
    return graph;
}

} // namespace nift::rt
