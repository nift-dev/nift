#include "rt/module_graph.h"

#include "rt/literal.h"
#include "rt/module_loader.h"
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

std::string_view declared_name(const Ast& ast, const Node& st) {
    const bool decl = st.kind == NodeKind::Declare || st.kind == NodeKind::FnDecl ||
                      st.kind == NodeKind::StructDecl || st.kind == NodeKind::EnumDecl;
    if (!decl || st.a == kNoNode || ast.at(st.a).kind != NodeKind::Ident) return {};
    return ast.text(ast.at(st.a));
}

bool inside(const fs::path& root, const fs::path& path) {
    const fs::path rel = path.lexically_relative(root);
    return !rel.empty() && *rel.begin() != "..";
}

// Calls `visit(id, in_function)` for every node below `root`, in no
// particular order; `in_function` is true inside FnDecl and Lambda bodies.
template <typename Visit>
void walk(const Ast& ast, NodeId root, Visit visit) {
    std::vector<std::pair<NodeId, bool>> stack{{root, false}};
    while (!stack.empty()) {
        const auto [id, in_fn] = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast.at(id);
        visit(id, in_fn);
        const bool below = in_fn || n.kind == NodeKind::FnDecl || n.kind == NodeKind::Lambda;
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back({child, below});
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back({list[k], below});
    }
}

class GraphBuilder {
public:
    GraphBuilder(ModuleGraph& graph, const ModuleGraphOptions& options) : graph_(graph), options_(options) {}

    bool refuse(std::size_t where, std::uint32_t offset, std::string reason) {
        if (graph_.ok) {
            graph_.ok = false;
            graph_.reason = std::move(reason);
            graph_.where = where;
            graph_.offset = offset;
        }
        return false;
    }

    // One initialisation of unit `index` (legacy's order: its imports run at
    // their statements, depth first).
    bool visit(std::size_t index, const Ast& ast, NodeId root, const fs::path& base, std::vector<fs::path>& active) {
        if (active.size() > kMaxModuleDepth) return refuse(index, 0, "import-limit");
        if (!check_placement(index, ast, root)) return false;
        // Names bound in this unit, with the module that originally declared
        // them: an import may rebind a name to the same declaration (the
        // shared instance, D-041), never to a different one.
        std::vector<Origin> bound;
        for (std::string_view name : importer_names(ast, root)) bound.push_back({name, index});
        Names provided;  // names earlier statements of a module bind (for `export`)
        const Node& program = ast.at(root);
        const NodeId* items = ast.list(program);
        for (std::uint32_t k = 0; k < program.count; ++k) {
            const Node& st = ast.at(items[k]);
            if (st.kind == NodeKind::Export) {
                if (index > 0 && !check_export(index, ast, st, provided, bound)) return false;
                continue;
            }
            if (const std::string_view name = declared_name(ast, st); !name.empty()) provided.push_back(name);
            if (st.kind != NodeKind::Import) continue;
            std::size_t target = 0;
            if (!import(index, ast, st, items[k], base, active, target)) return false;
            const Module& mod = *graph_.modules[target];
            for (std::size_t e = 0; e < mod.exports.size(); ++e) {
                const Origin incoming{mod.exports[e], mod.export_origins[e]};
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
        return true;
    }

private:
    bool import(std::size_t index, const Ast& ast, const Node& st, NodeId statement, const fs::path& base,
                std::vector<fs::path>& active, std::size_t& target) {
        std::string argument;
        if (!literal_argument(ast, st, argument)) return refuse(index, st.begin, "import-dynamic");
        const bool package = argument.find('/') == std::string::npos && argument.find('\\') == std::string::npos &&
                             fs::path(argument).extension().empty();
        if (package) return refuse(index, st.begin, "import-package");
        std::replace(argument.begin(), argument.end(), '\\', '/');
        fs::path path = argument;
        if (path.is_relative()) path = base / path;
        path = fs::absolute(path).lexically_normal();
        if (!options_.project_root.empty() && !inside(fs::absolute(options_.project_root).lexically_normal(), path))
            return refuse(index, st.begin, "import-outside-project");
        const fs::path identity = module_identity(path);
        if (std::find(active.begin(), active.end(), identity) != active.end())
            return refuse(index, st.begin, "import-cycle");
        bool fresh = false;
        if (!load(index, st, path, identity, target, fresh)) return false;
        Module& mod = *graph_.modules[target];
        ++mod.legacy_initialisations;
        graph_.modules[index]->imports.push_back({statement, target});
        if (!fresh) {
            // Legacy runs the module again here (a fresh environment); the
            // VM initialises it once. The single policy point (InitPolicy).
            if (options_.policy == InitPolicy::RefuseReinitialisation)
                return refuse(index, st.begin, "import-reinitialised");
            return true;
        }
        active.push_back(identity);
        const bool ok = visit(target, *mod.ast, mod.root, mod.path.parent_path(), active);
        active.pop_back();
        return ok;
    }

    static bool literal_argument(const Ast& ast, const Node& st, std::string& out) {
        if (st.a == kNoNode) return false;
        const Node& arg = ast.at(st.a);
        Value value;
        if (arg.kind != NodeKind::String || arg.count != 0) return false;
        if (!decode_string_literal(ast.text(arg), value)) return false;
        out = std::string(string_view_of(value));
        return !out.empty();
    }

    bool load(std::size_t importer, const Node& st, const fs::path& path, const fs::path& identity,
              std::size_t& index, bool& fresh) {
        const auto known = by_identity_.find(identity.generic_string());
        if (known != by_identity_.end()) {
            index = known->second;
            return true;
        }
        if (graph_.modules.size() >= kMaxModules) return refuse(importer, st.begin, "import-limit");
        ModuleSource source = load_module_source(path);
        if (!source.found) {
            graph_.absent.push_back(path);
            return refuse(importer, st.begin, "import-missing");
        }
        graph_.dependencies.push_back(path);
        auto mod = std::make_unique<Module>();
        mod->path = path;
        mod->identity = identity;
        mod->source = std::move(source.text);
        mod->ast = std::make_unique<Ast>(mod->source);
        const ParseResult parsed = parse_script(*mod->ast);
        mod->root = parsed.root;
        index = graph_.modules.size();
        fresh = true;
        by_identity_.emplace(identity.generic_string(), index);
        graph_.modules.push_back(std::move(mod));
        if (!parsed.ok) return refuse(index, parsed.error.offset, "import-parse-error");
        const Module& m = *graph_.modules[index];
        const NodeId opaque = first_opaque_node(*m.ast, m.root);
        if (opaque != kNoNode)
            return refuse(index, m.ast->at(opaque).begin,
                          std::string("import-opaque:") + opaque_reason_name(m.ast->at(opaque).reason));
        return true;
    }

    // Names an export may collide with in the importer: its top-level
    // bindings, and functions, structs and enums declared anywhere (legacy's
    // global tables). Declared after the import too (conservative). A name
    // both declared here and imported is refused even if declared later.
    static Names importer_names(const Ast& ast, NodeId root) {
        Names names;
        const Node& program = ast.at(root);
        const NodeId* items = ast.list(program);
        for (std::uint32_t k = 0; k < program.count; ++k)
            if (const std::string_view name = declared_name(ast, ast.at(items[k])); !name.empty())
                names.push_back(name);
        walk(ast, root, [&](NodeId id, bool) {
            const Node& n = ast.at(id);
            if (n.kind == NodeKind::Declare || (n.flags & kFlagMethod)) return;
            if (const std::string_view name = declared_name(ast, n); !name.empty() && !has(names, name))
                names.push_back(name);
        });
        return names;
    }

    // `export(x)` (modules only): x is bound by an earlier top-level statement
    // of the module (legacy checks at the export; exported values are taken at
    // the module's end).
    bool check_export(std::size_t index, const Ast& ast, const Node& st, const Names& provided,
                      const std::vector<Origin>& bound) {
        Module& mod = *graph_.modules[index];
        if (st.a == kNoNode || ast.at(st.a).kind != NodeKind::Ident) return refuse(index, st.begin, "export-missing");
        const std::string_view name = ast.text(ast.at(st.a));
        if (has(mod.exports, name)) return refuse(index, st.begin, "export-duplicate");
        if (!has(provided, name)) return refuse(index, st.begin, "export-missing");
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

    // Imports and exports only as top-level statements; in a module, no
    // `return <value>` outside functions.
    bool check_placement(std::size_t index, const Ast& ast, NodeId root) {
        const Node& program = ast.at(root);
        const NodeId* items = ast.list(program);
        const NodeId* last = items + program.count;
        const auto top = [&](NodeId id) { return std::find(items, last, id) != last; };
        bool ok = true;
        walk(ast, root, [&](NodeId id, bool in_fn) {
            const Node& n = ast.at(id);
            if (!ok) return;
            if (n.kind == NodeKind::Import && !top(id)) ok = refuse(index, n.begin, "import-nested");
            else if (n.kind == NodeKind::Export && !top(id)) ok = refuse(index, n.begin, "export-nested");
            else if (index > 0 && !in_fn && n.kind == NodeKind::Return && !(n.flags & kFlagBare) && n.a != kNoNode)
                ok = refuse(index, n.begin, "module-return");
        });
        return ok;
    }

    ModuleGraph& graph_;
    const ModuleGraphOptions& options_;
    std::unordered_map<std::string, std::size_t> by_identity_;
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
    GraphBuilder(graph, options).visit(0, entry, root, base, active);
    return graph;
}

} // namespace nift::rt
