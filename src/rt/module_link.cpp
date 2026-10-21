#include "rt/module_link.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

// rt-lint: cold-file compile-time linking, once per unit

namespace nift::rt {

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
                if (!in_fn && !name_of(n.a).empty()) u.fns.insert(name_of(n.a));
                skip = n.a;
                break;
            case NodeKind::StructDecl: case NodeKind::EnumDecl:
                if (!name_of(n.a).empty()) u.top.insert(name_of(n.a));
                skip = n.a;
                break;
            case NodeKind::Declare:
                if (!in_fn && !name_of(n.a).empty()) u.top.insert(name_of(n.a));
                break;
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
    Linker(LinkedUnit& out, std::vector<UnitView>& units) : out_(out), units_(units) {}

    void emit(std::size_t m) {
        const UnitView& u = units_[m];
        const Ast& ast = *u.ast;
        struct Edit {
            std::size_t begin, end;
            std::size_t target;  // module to splice in; npos: blank
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
            edits.push_back({st.begin, statement_end(u.source, st), imp.module});
        }
        std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin < b.begin; });
        std::size_t at = 0;
        for (const Edit& e : edits) {
            copy(m, at, e.begin);
            if (e.target == npos) {
                const std::size_t begin = out_.text.size();
                for (std::size_t k = e.begin; k < e.end; ++k) out_.text.push_back(u.source[k] == '\n' ? '\n' : ' ');
                out_.segments.push_back({begin, out_.text.size(), m, e.begin});
            } else {
                out_.depth[e.target] = out_.depth[m] + 1;
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
};

} // namespace

LinkedUnit::Location LinkedUnit::locate(std::size_t offset) const {
    Location out;
    const LinkSegment* best = nullptr;
    for (const LinkSegment& s : segments) {
        if (s.begin <= offset && offset < s.end) {
            out.module = s.module;
            out.offset = s.source_begin + (offset - s.begin);
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

LinkedUnit link_unit(const Ast& entry, NodeId root, std::string_view entry_source, const fs::path& entry_path) {
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
    const auto refuse = [&](std::size_t where, std::uint32_t offset, std::string reason) {
        out.ok = false;
        out.where = where;
        out.offset = offset;
        out.reason = std::move(reason);
    };
    ModuleGraphOptions options;
    options.policy = InitPolicy::RefuseReinitialisation;  // D-041 #2: until both engines initialise once
    out.graph = build_module_graph(entry, root, entry_path, options);
    if (!out.graph.ok) { refuse(out.graph.where, out.graph.offset, out.graph.reason); return out; }
    const std::size_t count = out.graph.modules.size();
    std::vector<UnitView> units(count);
    for (std::size_t m = 0; m < count; ++m) {
        const Module& mod = *out.graph.modules[m];
        units[m].ast = m == 0 ? &entry : mod.ast.get();
        units[m].root = m == 0 ? root : mod.root;
        units[m].source = m == 0 ? entry_source : std::string_view(mod.source);
        collect(units[m]);
        std::uint32_t at = 0;
        if (m > 0 && has_top_level_return(*units[m].ast, units[m].root, at)) {
            refuse(m, at, "module-return");
            return out;
        }
    }
    // Names declared outside functions, and function names, are the unit's
    // own: no other unit declares them.
    std::unordered_map<std::string_view, std::size_t> owner;
    for (std::size_t m = 0; m < count; ++m)
        for (const Names* set : {&units[m].top, &units[m].fns})
            for (std::string_view name : *set) {
                const auto seen = owner.emplace(name, m);
                if (!seen.second && seen.first->second != m)
                    { refuse(m, units[m].ast->at(units[m].root).begin, "import-name-clash"); return out; }
            }
    // A unit names another unit's binding or function only where it
    // imports it (legacy isolates module scopes).
    for (std::size_t m = 0; m < count; ++m) {
        Names imported;
        for (const ModuleImport& imp : out.graph.modules[m]->imports)
            for (std::string_view name : out.graph.modules[imp.module]->exports) imported.insert(name);
        for (NodeId id : units[m].uses) {
            const std::string_view name = units[m].ast->text(units[m].ast->at(id));
            const auto o = owner.find(name);
            if (o == owner.end() || o->second == m || imported.count(name)) continue;
            { refuse(m, units[m].ast->at(id).begin, "import-scope"); return out; }
        }
    }
    for (std::size_t m = 1; m < count; ++m) out.module_names.insert(out.module_names.end(), units[m].top.begin(),
                                                                     units[m].top.end());
    out.depth.assign(count, 0);
    Linker(out, units).emit(0);
    return out;
}

} // namespace nift::rt
