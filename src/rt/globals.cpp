#include "rt/globals.h"

#include "rt/builtin_bind.h"
#include "rt/legacy_names.h"
#include "rt/native.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

// Static analysis, once per unit at compile time (not an execution path).

namespace nift::rt {

namespace {

using NameSet = std::unordered_set<std::string_view>;

// Iterative pre-order walk (the unit's depth is bounded, but this analysis
// does not rely on it).
template <typename F>
void walk(const Ast& ast, NodeId root, F&& visit) {
    if (root == kNoNode) return;
    std::vector<NodeId> stack{root};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const Node& n = ast.at(id);
        visit(id, n);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = n.count; k-- > 0;) stack.push_back(list[k]);
        if (n.c != kNoNode) stack.push_back(n.c);
        if (n.b != kNoNode) stack.push_back(n.b);
        if (n.a != kNoNode) stack.push_back(n.a);
    }
}

// Names a statement subtree declares in any scope: `:=` targets (identifiers
// and patterns), `for` bindings, `catch` bindings, parameters (of lambdas).
void declared_names(const Ast& ast, NodeId root, NameSet& out) {
    walk(ast, root, [&](NodeId, const Node& n) {
        if (n.kind == NodeKind::Declare && n.a != kNoNode) {
            const Node& target = ast.at(n.a);
            if (target.kind == NodeKind::Ident) out.insert(ast.text(target));
            for (std::uint32_t k = 0; target.kind == NodeKind::Pattern && k < target.count; ++k)
                out.insert(ast.text(ast.at(ast.list(target)[k])));
        } else if (n.kind == NodeKind::ForBinding) {
            for (std::uint32_t k = 0; k < n.count; ++k) out.insert(ast.text(ast.at(ast.list(n)[k])));
        } else if (n.kind == NodeKind::Try && n.b != kNoNode) {
            out.insert(ast.text(ast.at(n.b)));
        } else if (n.kind == NodeKind::Param) {
            out.insert(ast.text(n));
        }
    });
}

// Identifiers read or written as bindings in a subtree: not declared names,
// member names, object keys, callees of direct calls, or names that are part
// of a declaration's shape.
void binding_uses(const Ast& ast, NodeId root, const UnitPlan& plan, NameSet& reads, NameSet& writes) {
    std::unordered_set<NodeId> skip;
    walk(ast, root, [&](NodeId id, const Node& n) {
        switch (n.kind) {
            case NodeKind::Declare: case NodeKind::FnDecl: case NodeKind::StructDecl: case NodeKind::EnumDecl:
            case NodeKind::StructField: case NodeKind::Export: case NodeKind::Property:
                if (n.a != kNoNode) skip.insert(n.a);
                break;
            case NodeKind::Member: case NodeKind::SafeMember: case NodeKind::Try:
                if (n.b != kNoNode) skip.insert(n.b);
                break;
            case NodeKind::Call:
                if (n.a != kNoNode && ast.at(n.a).kind == NodeKind::Ident) {
                    const std::string_view callee = ast.text(ast.at(n.a));
                    NativeInfo native{};
                    const bool direct = plan.index.count(callee) || plan.struct_index.count(callee) ||
                                        lookup_native(callee, native) || bind_function(callee) != nullptr;
                    if (direct) skip.insert(n.a);
                    else reads.insert(callee);  // a callable binding: a free-name read when not local
                }
                break;
            case NodeKind::Assign: case NodeKind::CompoundAssign: case NodeKind::IncDec:
                if (n.a != kNoNode && ast.at(n.a).kind == NodeKind::Ident) {
                    writes.insert(ast.text(ast.at(n.a)));
                    if (n.kind == NodeKind::Assign) skip.insert(n.a);  // compound forms also read
                }
                break;
            case NodeKind::Ident:
                if (!skip.count(id)) reads.insert(ast.text(n));
                break;
            default: break;
        }
    });
}

// Direct callees (user functions) of a subtree.
void callees(const Ast& ast, NodeId root, const UnitPlan& plan, std::vector<std::size_t>& out) {
    walk(ast, root, [&](NodeId, const Node& n) {
        if (n.kind != NodeKind::Call || n.a == kNoNode || ast.at(n.a).kind != NodeKind::Ident) return;
        const auto fn = plan.index.find(ast.text(ast.at(n.a)));
        if (fn != plan.index.end()) out.push_back(fn->second - 1u);
    });
}

} // namespace

GlobalPlan plan_globals(const Ast& ast, NodeId root, const UnitPlan& plan) {
    GlobalPlan result;
    const std::size_t nf = plan.functions.size();
    result.free.resize(nf);
    // Top level: direct declarations (count, position, node) and block ones.
    const Node& program = ast.at(root);
    const NodeId* items = ast.list(program);
    std::unordered_map<std::string_view, std::size_t> top_count;
    std::unordered_map<std::string_view, std::pair<NodeId, std::uint32_t>> top_decl;
    NameSet top_block_declared, top_pattern_declared;
    constexpr std::uint32_t kNever = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> earliest(nf, kNever);  // first top-level statement reaching f
    std::vector<std::vector<std::size_t>> edges(nf);
    for (std::size_t f = 0; f < nf; ++f) callees(ast, ast.at(plan.functions[f]).b, plan, edges[f]);
    for (std::uint32_t k = 0; k < program.count; ++k) {
        const Node& st = ast.at(items[k]);
        if (st.kind == NodeKind::FnDecl) continue;
        if (st.kind == NodeKind::Declare && st.a != kNoNode && ast.at(st.a).kind == NodeKind::Ident) {
            const std::string_view name = ast.text(ast.at(st.a));
            if (top_count[name]++ == 0) top_decl[name] = {items[k], k};
        } else if (st.kind == NodeKind::Declare) {
            declared_names(ast, items[k], top_pattern_declared);
        } else {
            declared_names(ast, items[k], top_block_declared);  // if/while/for/try bodies
        }
        // Functions reachable from this statement (transitively).
        std::vector<std::size_t> work;
        callees(ast, items[k], plan, work);
        while (!work.empty()) {
            const std::size_t f = work.back();
            work.pop_back();
            if (earliest[f] != kNever) continue;
            earliest[f] = k;
            for (std::size_t g : edges[f]) work.push_back(g);
        }
    }
    // Transitive callers of each function, itself included (G2).
    std::vector<std::vector<std::size_t>> callers(nf);
    for (std::size_t f = 0; f < nf; ++f)
        for (std::size_t g : edges[f]) callers[g].push_back(f);
    std::vector<NameSet> declares(nf);
    for (std::size_t f = 0; f < nf; ++f) {
        const Node& fn = ast.at(plan.functions[f]);
        const NodeId* params = ast.list(fn);
        for (std::uint32_t k = 0; k < fn.count; ++k) declares[f].insert(ast.text(ast.at(params[k])));
        declared_names(ast, fn.b, declares[f]);
    }
    for (std::size_t f = 0; f < nf; ++f) {
        NameSet reads, writes;
        binding_uses(ast, ast.at(plan.functions[f]).b, plan, reads, writes);
        NameSet used = reads;
        used.insert(writes.begin(), writes.end());
        std::vector<char> active(nf, 0);
        std::vector<std::size_t> work{f};
        while (!work.empty()) {
            const std::size_t g = work.back();
            work.pop_back();
            if (active[g]) continue;
            active[g] = 1;
            for (std::size_t h : callers[g]) work.push_back(h);
        }
        for (std::string_view name : used) {
            if (declares[f].count(name) || plan.index.count(name)) continue;  // local, or a function name
            GlobalRef ref;
            ref.written = writes.count(name) > 0;
            const auto decl = top_decl.find(name);
            bool shadowed = false;
            for (std::size_t g = 0; g < nf && !shadowed; ++g) shadowed = active[g] && declares[g].count(name);
            const bool top_any = decl != top_decl.end() || top_pattern_declared.count(name) ||
                                 top_block_declared.count(name);
            if (shadowed && !top_any)
                ref.refusal = "free-name:dynamic-caller";  // only a caller's binding can resolve it (GETDYN)
            else if (!top_any)
                ref.refusal = legacy_special_name(name) ? "free-name:legacy-binding" : "free-name:undeclared";
            else if (decl == top_decl.end() || top_count[name] != 1 || top_pattern_declared.count(name) ||
                     top_block_declared.count(name))
                ref.refusal = "free-name:not-one-top-level-binding";  // G1
            else if (shadowed)
                ref.refusal = "free-name:dynamic-shadow";  // G2
            else if (earliest[f] != kNever && earliest[f] <= decl->second.second)
                ref.refusal = "free-name:before-declaration";  // G3
            if (ref.refusal.empty()) {
                ref.ok = true;
                ref.decl = decl->second.first;
                ref.position = decl->second.second;
                result.globals.emplace(name, ref.decl);
            }
            result.free[f].emplace(name, std::move(ref));
        }
    }
    return result;
}

} // namespace nift::rt
