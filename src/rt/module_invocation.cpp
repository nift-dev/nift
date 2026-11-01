#include "rt/module_invocation.h"

#include <algorithm>
#include <utility>

// rt-lint: cold-file compile-time linking, once per unit

namespace nift::rt {

namespace {

struct Mention {
    std::string_view name;
    std::uint32_t offset = 0;
    NodeId id = kNoNode;
};

struct Function {
    std::size_t unit = 0;
    std::string_view name;
    std::vector<Mention> mentions;  // every Ident below its body
};

struct UnitScan {
    std::vector<Mention> top;            // Ident mentions outside named functions (top-level lambdas included)
    std::vector<std::size_t> functions;  // indices into the function table
    std::vector<Mention> non_plain;      // declarations of a name outside functions but not as a statement
    std::vector<Mention> kept;           // in-function Declare targets: locals, refused by the compiler (never renamed)
    std::vector<NodeId> declared;        // top-level Declare targets: not calls, never init seeds
};

std::string_view ident(const Ast& ast, NodeId at) {
    return at != kNoNode && ast.at(at).kind == NodeKind::Ident ? ast.text(ast.at(at)) : std::string_view();
}

// The unit's mentions, as module_link's `collect` sees them (member keys,
// export arguments and declared function/struct/enum names are not mentions).
void scan(const Ast& ast, NodeId root, std::size_t unit, UnitScan& out, std::vector<Function>& table) {
    const Node& program = ast.at(root);
    const NodeId* items = ast.list(program);
    const NodeId* last = items + program.count;
    struct Item { NodeId id; std::size_t fn; bool in_fn; };
    constexpr std::size_t none = static_cast<std::size_t>(-1);
    std::vector<Item> stack{{root, none, false}};
    while (!stack.empty()) {
        const Item it = stack.back();
        stack.pop_back();
        if (it.id == kNoNode) continue;
        const Node& n = ast.at(it.id);
        std::size_t fn = it.fn;
        NodeId skip = kNoNode;
        switch (n.kind) {
            case NodeKind::FnDecl:
                skip = n.a;
                if (fn == none) {
                    fn = table.size();
                    table.push_back({unit, ident(ast, n.a), {}});
                    out.functions.push_back(fn);
                }
                break;
            case NodeKind::StructDecl: case NodeKind::EnumDecl: skip = n.a; break;
            case NodeKind::Member: case NodeKind::SafeMember: skip = n.b; break;
            case NodeKind::Export: skip = n.a; break;
            case NodeKind::Declare:
                if (it.in_fn) out.kept.push_back({ident(ast, n.a), n.begin, n.a});
                else if (std::find(items, last, it.id) == last)
                    out.non_plain.push_back({ident(ast, n.a), n.begin, n.a});
                else {
                    out.declared.push_back(n.a);
                    // `g := (…) => …` at top level: the lambda is a function named g
                    // (reached when g is mentioned, like a named function).
                    if (fn == none && n.b != kNoNode && ast.at(n.b).kind == NodeKind::Lambda) {
                        const std::size_t lambda = table.size();
                        table.push_back({unit, ident(ast, n.a), {}});
                        out.functions.push_back(lambda);
                        stack.push_back({n.a, fn, it.in_fn});
                        stack.push_back({n.b, lambda, true});
                        continue;
                    }
                }
                break;
            case NodeKind::Ident: {
                const Mention m{ast.text(n), n.begin, it.id};
                (fn == none ? out.top : table[fn].mentions).push_back(m);
                break;
            }
            default: break;
        }
        const bool below = it.in_fn || n.kind == NodeKind::FnDecl || n.kind == NodeKind::Lambda;
        for (NodeId child : {n.a, n.b, n.c})
            if (child != skip) stack.push_back({child, fn, below});
        if (n.kind == NodeKind::EnumDecl) continue;  // members are not mentions
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) stack.push_back({list[k], fn, below});
    }
}

struct Root {
    std::size_t unit = 0;
    std::uint32_t offset = 0;
};

// Functions reachable by name from `seeds`, each with the seed that reached it
// first (seeds in (unit, offset) order, breadth first: deterministic).
std::vector<std::pair<bool, Root>> reach(const std::vector<Function>& table,
                                         std::vector<std::pair<Mention, Root>> seeds) {
    std::sort(seeds.begin(), seeds.end(), [](const auto& a, const auto& b) {
        return a.second.unit != b.second.unit ? a.second.unit < b.second.unit : a.first.offset < b.first.offset;
    });
    std::vector<std::pair<bool, Root>> reached(table.size(), {false, Root{}});
    std::vector<std::size_t> queue;
    const auto visit = [&](std::string_view name, const Root& root) {
        for (std::size_t f = 0; f < table.size(); ++f)
            if (!reached[f].first && !table[f].name.empty() && table[f].name == name) {
                reached[f] = {true, root};
                queue.push_back(f);
            }
    };
    for (const auto& [mention, root] : seeds) visit(mention.name, root);
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 8  // test-only (packages/mutants.sh)
    queue.clear();
#endif
    for (std::size_t q = 0; q < queue.size(); ++q) {
        const std::size_t f = queue[q];
        for (const Mention& m : table[f].mentions) visit(m.name, reached[f].second);
    }
    return reached;
}

bool named(const std::vector<std::string_view>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

InvocationPlan plan_invocation(const std::vector<InvocationUnit>& units, const ModuleGraph& graph,
                               const std::vector<std::string_view>& names) {
    InvocationPlan plan;
    const std::size_t count = units.size();
    plan.private_names.assign(count, {});
    plan.rename_ids.assign(count, {});
    if (names.empty() || count < 2) return plan;
    std::vector<Function> table;
    std::vector<UnitScan> scans(count);
    for (std::size_t m = 1; m < count; ++m) scan(*units[m].ast, units[m].root, m, scans[m], table);
    // Private declarations: a plain top-level `args := …` statement.
    std::vector<std::vector<std::pair<std::string_view, const Node*>>> declared(count);
    for (std::size_t m = 1; m < count; ++m) {
        const Ast& ast = *units[m].ast;
        const Node& program = ast.at(units[m].root);
        const NodeId* items = ast.list(program);
        for (std::uint32_t k = 0; k < program.count; ++k) {
            const Node& st = ast.at(items[k]);
            const std::string_view name = st.kind == NodeKind::Declare ? ident(ast, st.a) : std::string_view();
            if (named(names, name) && !named(plan.private_names[m], name)) {
                plan.private_names[m].push_back(name);
                declared[m].push_back({name, &st});
            }
        }
    }
    // Code that may run while any module initialises.
    std::vector<std::pair<Mention, Root>> init_seeds;
    const auto declaration = [&](std::size_t m, NodeId id) {
        return std::find(scans[m].declared.begin(), scans[m].declared.end(), id) != scans[m].declared.end();
    };
    for (std::size_t m = 1; m < count; ++m)
        for (const Mention& t : scans[m].top)
            if (!declaration(m, t.id)) init_seeds.push_back({t, Root{m, t.offset}});
    const auto init = reach(table, init_seeds);
    struct Candidate { std::uint32_t offset; const char* reason; bool note; Root root; };
    for (std::size_t m = 1; m < count; ++m) {
        const Ast& ast = *units[m].ast;
        const auto kept = [&](NodeId id) {
            const auto& k = scans[m].kept;
            return std::any_of(k.begin(), k.end(), [&](const Mention& e) { return e.id == id; });
        };
        std::vector<Candidate> found;
        const auto add = [&](std::uint32_t at, const char* reason, bool note = false, Root root = {}) {
            found.push_back({at, reason, note, root});
        };
        for (const Mention& d : scans[m].non_plain)
            if (named(names, d.name)) add(d.offset, "import-scope:invocation-ambiguous");
        for (NodeId id = 0; id < ast.size(); ++id) {
            const Node& n = ast.at(id);
            if ((n.kind == NodeKind::StructDecl || n.kind == NodeKind::EnumDecl) && named(names, ident(ast, n.a)))
                add(ast.at(n.a).begin, "import-scope:invocation-ambiguous");
        }
        for (const auto& [name, decl] : declared[m]) {
            // Read before the declaration ran: top-level text before its end
            // (its own name aside), or a function reached from earlier top-level code.
            for (const Mention& t : scans[m].top)
                if (t.name == name && t.offset < decl->end && t.id != decl->a)
                    add(t.offset, "import-scope:invocation-ambiguous");
            std::vector<std::pair<Mention, Root>> early;
            for (const Mention& t : scans[m].top)
                if (t.offset < decl->begin && !declaration(m, t.id)) early.push_back({t, Root{m, t.offset}});
            const auto before = reach(table, early);
            for (std::size_t f = 0; f < table.size(); ++f)
                if (before[f].first)
                    for (const Mention& r : table[f].mentions)
                        if (r.name == name && named(plan.private_names[table[f].unit], name) && table[f].unit == m)
                            add(r.offset, "import-scope:invocation-ambiguous");
            // An export of the private binding: imported by the entry, legacy
            // fails at that import (`export collides with existing binding`), a
            // program error; into another module it is not proved (refused).
            const Node& program = ast.at(units[m].root);
            const NodeId* items = ast.list(program);
            for (std::uint32_t k = 0; k < program.count; ++k) {
                const Node& st = ast.at(items[k]);
                if (st.kind != NodeKind::Export || ident(ast, st.a) != name) continue;
                // Into the entry it is the graph's program error (import-scope:invocation-export).
                bool into_entry = false;
                for (const ModuleImport& imp : graph.modules[0]->imports) into_entry = into_entry || imp.module == m;
                if (!into_entry) add(st.begin, "import-scope:invocation-ambiguous");
            }
        }
        // The entry's binding: never at top level, never in init-reachable code.
        for (const Mention& t : scans[m].top)
            if (named(names, t.name) && !named(plan.private_names[m], t.name))
                add(t.offset, "import-scope:invocation-init");
        for (std::size_t f : scans[m].functions) {
            if (!init[f].first) continue;
            for (const Mention& r : table[f].mentions)
                if (named(names, r.name) && !named(plan.private_names[m], r.name) && !kept(r.id))
                    add(r.offset, "import-scope:invocation-init", true, init[f].second);
        }
        if (found.empty()) continue;
        const Candidate& first = *std::min_element(found.begin(), found.end(),
            [](const Candidate& a, const Candidate& b) { return a.offset < b.offset; });
        plan.ok = false;
        plan.reason = first.reason;
        plan.where = m;
        plan.offset = first.offset;
        plan.positioned = true;
        plan.note = first.note;
        plan.note_where = first.root.unit;
        plan.note_offset = first.root.offset;
        return plan;
    }
    // The private binding's mentions, to rename (Param names and locals in
    // functions keep their spelling: the compiler refuses them as before).
#if defined(NIFT_PACKAGES_MUTANT) && NIFT_PACKAGES_MUTANT == 9  // test-only (packages/mutants.sh)
    {
        std::vector<std::string_view> all;
        for (std::size_t m = 1; m < count; ++m)
            for (std::string_view n : plan.private_names[m])
                if (!named(all, n)) all.push_back(n);
        for (std::size_t m = 1; m < count; ++m) if (plan.private_names[m].empty()) plan.private_names[m] = all;
    }
#endif
    for (std::size_t m = 1; m < count; ++m) {
        if (plan.private_names[m].empty()) continue;
        const auto kept = [&](NodeId id) {
            const auto& k = scans[m].kept;
            return std::any_of(k.begin(), k.end(), [&](const Mention& e) { return e.id == id; });
        };
        std::vector<const std::vector<Mention>*> lists{&scans[m].top};
        for (std::size_t f : scans[m].functions) lists.push_back(&table[f].mentions);
        for (const auto* list : lists)
            for (const Mention& t : *list)
                if (named(plan.private_names[m], t.name) && !kept(t.id)) plan.rename_ids[m].push_back(t.id);
        std::sort(plan.rename_ids[m].begin(), plan.rename_ids[m].end());
    }
    return plan;
}

} // namespace nift::rt
