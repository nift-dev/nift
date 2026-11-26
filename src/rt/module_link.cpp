#include "rt/module_link.h"

#include "rt/module_invocation.h"
#include "rt/module_scan.h"

#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>

// rt-lint: cold-file compile-time linking, once per unit

namespace nift::rt {

bool plain_private_binding(const Ast& ast, NodeId root, std::string_view name);

namespace {

namespace fs = std::filesystem;
using Names = std::unordered_set<std::string_view>;

struct UnitView {
    const Ast* ast = nullptr;
    NodeId root = kNoNode;
    std::string_view source;
    Names top;   // declared outside functions (any block), struct and enum names
    Names fns;   // function names
    std::vector<NodeId> uses;  // Ident nodes naming a binding or a callee
};

// Collects a unit's names. Member keys and declaration names of functions,
// structs and enums are not uses; a declared binding's own name is (a local
// of that name is conservatively a use).
void collect(UnitView& u) {
    const Ast& ast = *u.ast;
    std::vector<std::pair<NodeId, bool>> stack{{u.root, false}};
    while (!stack.empty()) {
        const auto [id, in_fn] = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast.at(id);
        const auto name_of = [&](NodeId at) {
            return at != kNoNode && ast.at(at).kind == NodeKind::Ident ? ast.text(ast.at(at)) : std::string_view();
        };
        NodeId skip = kNoNode;
        switch (n.kind) {
            case NodeKind::FnDecl:
                if (!in_fn && !(n.flags & kFlagMethod) && !name_of(n.a).empty()) u.fns.insert(name_of(n.a));
                skip = n.a;
                break;
            case NodeKind::StructDecl: case NodeKind::EnumDecl:
                if (!name_of(n.a).empty()) u.top.insert(name_of(n.a));
                skip = n.a;
                break;
            case NodeKind::Declare:
                if (!in_fn && !name_of(n.a).empty()) u.top.insert(name_of(n.a));
                break;
            case NodeKind::StructField: skip = n.a; break;
            case NodeKind::Member: case NodeKind::SafeMember: skip = n.b; break;
            case NodeKind::Export: skip = n.a; break;
            case NodeKind::Ident: u.uses.push_back(id); break;
            default: break;
        }
        const bool below = in_fn || n.kind == NodeKind::FnDecl || n.kind == NodeKind::Lambda;
        for (NodeId child : {n.a, n.b, n.c})
            if (child != skip) stack.push_back({child, below});
        const NodeId* list = ast.list(n);
        if (n.kind == NodeKind::EnumDecl) continue;  // members are not uses
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back({list[k], below});
    }
}

void plan_entry_shadows(const ModuleGraph& graph, std::vector<UnitView>& units, InvocationPlan& plan) {
    for (std::size_t m = 1; m < units.size() && plan.ok; ++m) {
        std::vector<std::string_view> collisions;
        const auto& exports = graph.modules[m]->exports;
        for (std::string_view name : units[m].top) {
            if ((units[0].top.count(name) || units[0].fns.count(name)) &&
                std::find(exports.begin(), exports.end(), name) == exports.end())
                collisions.push_back(name);
        }
        std::sort(collisions.begin(), collisions.end());
        for (std::string_view name : collisions) {
            const Ast& ast = *units[m].ast;
            if (!plain_private_binding(ast, units[m].root, name)) continue;
            units[m].top.erase(name);
            plan.private_names[m].push_back(name);
            for (NodeId id : units[m].uses)
                if (ast.text(ast.at(id)) == name) plan.rename_ids[m].push_back(id);
        }
        std::sort(plan.rename_ids[m].begin(), plan.rename_ids[m].end());
    }
}

// A `return` outside functions in a module ends only the module in legacy.
bool has_top_level_return(const Ast& ast, NodeId root, std::uint32_t& offset) {
    std::vector<NodeId> stack{root};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::FnDecl || n.kind == NodeKind::Lambda) continue;
        if (n.kind == NodeKind::Return) {
            offset = n.begin;
            return true;
        }
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
    }
    return false;
}

// The statement's span plus a `;` right after it (the parser consumed it).
std::size_t statement_end(std::string_view text, const Node& st) {
    std::size_t end = st.end;
    if (end < text.size() && text[end] == ';') ++end;
    return end;
}

class Linker {
public:
    Linker(LinkedUnit& out, std::vector<UnitView>& units, const InvocationPlan& plan,
           const std::vector<std::vector<std::string>>& generated)
        : out_(out), units_(units), plan_(plan), generated_(generated) {}

    void emit(std::size_t m) {
        const UnitView& u = units_[m];
        const Ast& ast = *u.ast;
        struct Edit {
            std::size_t begin, end;
            std::size_t target;  // module to splice in; npos: blank; renamed: the generated name
            const std::string* rename = nullptr;
            bool at_import = false;
        };
        std::vector<Edit> edits;
        const Node& program = ast.at(u.root);
        const NodeId* items = ast.list(program);
        for (std::uint32_t k = 0; k < program.count; ++k) {
            const Node& st = ast.at(items[k]);
            // A module's exports are its bindings themselves (D-046 #2): blanked.
            // The entry's own `export` stays (the compiler refuses it).
            if (m > 0 && st.kind == NodeKind::Export) edits.push_back({st.begin, statement_end(u.source, st), npos});
        }
        for (const ModuleImport& imp : out_.graph.modules[m]->imports) {
            const Node& st = ast.at(imp.statement);
            edits.push_back({st.begin, statement_end(u.source, st), imp.module, nullptr,
                             (st.flags & kFlagTemplateImport) != 0});
        }
        for (NodeId id : plan_.rename_ids[m]) {  // roles resolved in the module's source
            const Node& n = ast.at(id);
            const std::string_view name = ast.text(n);
            std::size_t g = 0;
            while (plan_.private_names[m][g] != name) ++g;
            edits.push_back({n.begin, n.end, npos, &generated_[m][g]});
        }
        std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin < b.begin; });
        std::size_t at = 0;
        for (const Edit& e : edits) {
            copy(m, at, e.begin);
            if (e.rename) {
                const std::size_t begin = out_.text.size();
                out_.text += *e.rename;
                out_.segments.push_back({begin, out_.text.size(), m, e.begin, true});
            } else if (e.target == npos) {
                const std::size_t begin = out_.text.size();
                for (std::size_t k = e.begin; k < e.end; ++k) out_.text.push_back(u.source[k] == '\n' ? '\n' : ' ');
                out_.segments.push_back({begin, out_.text.size(), m, e.begin});
            } else {
                out_.depth[e.target] = out_.depth[m] + 1;
                out_.prefix[e.target] = out_.prefix[m] + (e.at_import ? "@import: " : "import: ");
                out_.text.push_back('\n');
                emit(e.target);
                out_.text.push_back('\n');
            }
            at = e.end;
        }
        copy(m, at, u.source.size());
    }

private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    void copy(std::size_t m, std::size_t from, std::size_t to) {
        if (to <= from) return;
        const std::size_t begin = out_.text.size();
        out_.text.append(units_[m].source.substr(from, to - from));
        out_.segments.push_back({begin, out_.text.size(), m, from});
    }

    LinkedUnit& out_;
    std::vector<UnitView>& units_;
    const InvocationPlan& plan_;
    const std::vector<std::vector<std::string>>& generated_;  // per module, per private name
};

// Generated names for private bindings: absent from every unit's text and
// from each other (C1); module_names views them in the heap-stable modules.
std::vector<std::vector<std::string>> name_private_bindings(LinkedUnit& out, const std::vector<UnitView>& units,
                                               const InvocationPlan& plan) {
    const std::size_t count = units.size();
    std::vector<std::vector<std::string>> generated(count);
    for (std::size_t m = 1; m < count; ++m)
        for (std::size_t g = 0; g < plan.private_names[m].size(); ++g) {
            const std::string base = "nift__" + std::string(plan.private_names[m][g]) + "__m" + std::to_string(m);
            std::string name = base;
            const auto used = [&](const std::string& candidate) {
                for (const UnitView& u : units) if (u.source.find(candidate) != std::string_view::npos) return true;
                for (const auto& names : generated)
                    if (std::find(names.begin(), names.end(), candidate) != names.end()) return true;
                return false;
            };
#if !(defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 15)  // test-only (packages/mutants.sh)
            for (int k = 1; used(name); ++k) name = base + "_" + std::to_string(k);
#endif
            generated[m].push_back(name);
            out.renames.push_back({name, std::string(plan.private_names[m][g])});
        }
    for (std::size_t m = 1; m < count; ++m) out.module_names.insert(out.module_names.end(), units[m].top.begin(),
                                                                     units[m].top.end());
    for (std::size_t m = 1; m < count; ++m) {  // views into the (heap-stable) module, never into a moved string
        Module& mod = *out.graph.modules[m];
        mod.generated_names = generated[m];
        for (const std::string& name : mod.generated_names) out.module_names.push_back(name);
    }
    return generated;
}

} // namespace

LinkedUnit::Location LinkedUnit::locate(std::size_t offset) const {
    Location out;
    const LinkSegment* best = nullptr;
    for (const LinkSegment& s : segments) {
        if (s.begin <= offset && offset < s.end) {
            out.module = s.module;
            out.offset = s.source_begin + (s.atom ? 0 : offset - s.begin);
            return out;
        }
        if (s.end <= offset && (best == nullptr || s.end > best->end)) best = &s;
    }
    if (best != nullptr) {  // an inserted newline: the end of the text before it
        out.module = best->module;
        out.offset = best->source_begin + (best->end - best->begin);
    }
    return out;
}

std::string LinkedUnit::display(std::string text) const {
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 14  // test-only (packages/mutants.sh)
    return text;
#endif
    // Whole identifiers only: `nift__args__m1` is not replaced inside `nift__args__m11`.
    const auto ident = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    };
    for (const auto& [generated, original] : renames)
        for (std::size_t at = text.find(generated); at != std::string::npos; at = text.find(generated, at)) {
            const std::size_t end = at + generated.size();
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 18  // test-only: substrings replaced (packages/mutants.sh)
            if (false) {
#else
            if ((at > 0 && ident(text[at - 1])) || (end < text.size() && ident(text[end]))) {
#endif
                at = end;
                continue;
            }
            text.replace(at, generated.size(), original);
            at += original.size();
        }
    return text;
}


LinkedUnit link_unit(const Ast& entry, NodeId root, std::string_view entry_source, const fs::path& entry_path,
                     const std::vector<std::string_view>& entry_names) {
    LinkedUnit out;
    bool imports = false;
    std::vector<NodeId> stack{root};
    while (!stack.empty() && !imports) {  // anywhere: nested imports are the graph's refusal
        const NodeId id = stack.back();
        stack.pop_back();
        if (id == kNoNode) continue;
        const Node& n = entry.at(id);
        imports = n.kind == NodeKind::Import;
        for (NodeId child : {n.a, n.b, n.c}) stack.push_back(child);
        const NodeId* list = entry.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back(list[k]);
    }
    if (!imports) return out;
    out.linked = true;
    const auto refuse = [&](std::size_t where, std::uint32_t offset, std::string reason, bool positioned) {
        out.ok = false;
        out.where = where;
        out.offset = offset;
        out.positioned = positioned;
        out.reason = std::move(reason);
    };
    ModuleGraphOptions options;
    options.policy = InitPolicy::RefuseReinitialisation;  // D-041 #2: until both engines initialise once
    options.invocation_names = entry_names;
    out.graph = build_module_graph(entry, root, entry_path, options);
    if (out.graph.program_error) {  // an invalid program (D-NEXT-packages): rejected with its error
        const GraphError& e = out.graph.error;
        refuse(e.where, e.offset, e.id, e.positioned);
        out.program_error = true;
        return out;
    }
    if (!out.graph.ok) {
        refuse(out.graph.where, out.graph.offset, out.graph.reason, out.graph.positioned);
        return out;
    }
    const std::size_t count = out.graph.modules.size();
    std::vector<UnitView> units(count);
    std::vector<InvocationUnit> views(count);
    for (std::size_t m = 0; m < count; ++m) {
        const Module& mod = *out.graph.modules[m];
        units[m].ast = m == 0 ? &entry : mod.ast.get();
        units[m].root = m == 0 ? root : mod.root;
        units[m].source = m == 0 ? entry_source : std::string_view(mod.source);
        views[m] = {units[m].ast, units[m].root};
        collect(units[m]);
        std::uint32_t at = 0;
        if (m > 0 && has_top_level_return(*units[m].ast, units[m].root, at)) {
            // A `return <value>` is the graph's program error; a bare one ends
            // the module in legacy, which the linked unit cannot express.
            refuse(m, at, "module-bare-return", true);  // the `return` statement
            return out;
        }
    }
    // The entry's `args`/`cmd` in modules (rt/module_invocation.h): private
    // declarations are renamed below, so they take no part in name ownership.
    InvocationPlan plan = plan_invocation(views, out.graph, entry_names);
    for (std::size_t m = 1; m < count; ++m)
        for (std::string_view name : plan.private_names[m]) units[m].top.erase(name);
    plan_entry_shadows(out.graph, units, plan);
    // Names declared outside functions, and function names, are the unit's
    // own: no other unit declares them.
    // A clash is refused at unit m's own declaration of the name (Review 8:
    // never at the unit's start): the declared name's text in m's source.
    // Of several clashing names, the first in m's text.
    std::unordered_map<std::string_view, std::size_t> owner;  // lookup only: what is reported is chosen by offset
    for (std::size_t m = 0; m < count; ++m) {
        const std::string_view text = units[m].source;
        const std::less<const char*> before;
        bool clash = false, positioned = false;
        std::uint32_t at = 0;
        for (const Names* set : {&units[m].top, &units[m].fns})
            for (std::string_view name : *set) {
                const auto seen = owner.emplace(name, m);
                if (seen.second || seen.first->second == m) continue;
                // Declared names view the unit's source (Ast::text); otherwise no position.
                const bool inside = !before(name.data(), text.data()) &&
                                    before(name.data(), text.data() + text.size());
                const std::uint32_t offset = inside ? static_cast<std::uint32_t>(name.data() - text.data()) : 0;
                if (!clash || (inside && (!positioned || offset < at))) {
                    at = offset;
                    positioned = inside;
                }
                clash = true;
            }
        if (clash) { refuse(m, at, "import-name-clash", positioned); return out; }
    }
    // Check cross-module uses against direct exports. Invocation bindings
    // are resolved by the planner. Report the first inaccessible use.
    for (std::size_t m = 0; m < count; ++m) {
        Names imported;
        for (const ModuleImport& imp : out.graph.modules[m]->imports)
            for (std::string_view name : out.graph.modules[imp.module]->exports) imported.insert(name);
        std::uint32_t first = UINT32_MAX;
        const auto local = m ? module_local_uses(*units[m].ast, units[m].root, units[m].uses) : std::vector<bool>{};
        for (NodeId id : units[m].uses) {
            if (m > 0 && local[id]) continue;
            if (std::binary_search(plan.rename_ids[m].begin(), plan.rename_ids[m].end(), id)) continue;
            const std::string_view name = units[m].ast->text(units[m].ast->at(id));
            if (m > 0 && std::find(entry_names.begin(), entry_names.end(), name) != entry_names.end()) continue;
            const auto o = owner.find(name);
            if (o == owner.end() || o->second == m || imported.count(name)) continue;
            first = std::min(first, units[m].ast->at(id).begin);
        }
        if (first != UINT32_MAX) { refuse(m, first, "import-scope", true); return out; }  // the use
    }
    if (!plan.ok) {
        refuse(plan.where, plan.offset, plan.reason, plan.positioned);
        out.message = plan.message;
        out.note = plan.note;
        out.note_where = plan.note_where;
        out.note_offset = plan.note_offset;
        return out;
    }
    const auto generated = name_private_bindings(out, units, plan);
    out.depth.assign(count, 0);
    out.prefix.assign(count, std::string());
    Linker(out, units, plan, generated).emit(0);
    return out;
}

} // namespace nift::rt
