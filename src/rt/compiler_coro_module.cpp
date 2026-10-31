#include "rt/compiler_coro_module.h"

#include "rt/builtin_bind.h"
#include "rt/compiler_coro_module_effects.h"
#include "rt/module_link.h"
#include "rt/native.h"

#include <cstddef>
#include <string_view>
#include <vector>

// rt-lint: cold-file compile-time module coroutine admission, once per linked coroutine unit

namespace nift::rt {

namespace {

using coro_module::Effects;
using coro_module::Mode;
using coro_module::kMutant;

class ModuleCoroutines {
public:
    ModuleCoroutines(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds,
                     const LinkedUnit& link)
        : ast_(ast), root_(root), plan_(plan), a_(ast, plan, kinds, link) {}

    bool run(CompileResult& result) {
        NodeId at = kNoNode;
        if (kMutant != 1 && init_task(at)) return refuse(result, "coro-module-init-task", at);
        if (kMutant != 14 && private_async(at)) return refuse(result, "coro-module-private-async", at);
        if (tasks(at, true)) return refuse(result, "coro-module-before-import", at);
        if (tasks(at, false)) return refuse(result, "coro-module-capture", at);
        return true;
    }

private:
    using Item = coro_module::Analysis::Item;
    std::string_view name(NodeId id) const { return ast_.text(ast_.at(id)); }
    std::size_t module_of(std::uint32_t offset) const { return a_.module_of(offset); }
    bool function(std::string_view t, std::size_t& f) const { return a_.function(t, f); }
    bool is_async(std::size_t f) const { return a_.is_async(f); }
    bool thread_builtin(std::string_view t) const { return a_.thread_builtin(t); }
    Effects walk(NodeId start, Mode mode) const { return a_.walk(start, mode); }
    Effects closure(const Effects& e) const { return a_.closure(e); }

    bool refuse(CompileResult& result, const char* reason, NodeId at) const {
        result.reason = reason;
        result.offset = ast_.at(at).begin;
        result.positioned = true;  // the refused node's own offset
        return false;
    }

    // A module's top-level code (outside function bodies) that may start a
    // task, directly or through any function it calls (D-041 initialisation).
    // The first such node in the text is reported.
    bool init_task(NodeId& at) const {
        bool found = false;
        std::vector<Item> work{{root_, Mode::Init}};
        while (!work.empty()) {
            const Item item = work.back();
            work.pop_back();
            if (item.id == kNoNode) continue;
            Effects own;
            a_.visit(item.id, Mode::Init, own, work);
            const std::uint32_t begin = ast_.at(item.id).begin;
            if (kMutant != 9 && module_of(begin) == 0) continue;  // mutant 9: the entry's top level too
            const Effects all = kMutant == 4 ? own : closure(own);
            if (!all.spawn && !(all.by_value && a_.value_spawn())) continue;
            if (!found || begin < ast_.at(at).begin) at = item.id;
            found = true;
        }
        return found;
    }

    // A task that may capture module state: one that reads or writes a
    // module's top-level binding (through any function it reaches, or a
    // function or lambda passed to it), or calls through a value while some
    // function value or lambda can. And any task while a module's top-level
    // binding may hold a handle: legacy captures a module's whole environment
    // with every module callable a task receives (directly or in a container),
    // and fails on a non-transferable one. Such a unit is refused anyway (a
    // handle needs a host built-in: R1 module-host-op; unknown kinds: refused).
    bool captures(const Effects& c) const {
        if (c.state || a_.module_handles()) return true;
        return c.by_value && a_.value_state() && kMutant != 5;
    }

    // Legacy refuses any call of an async function made from module code
    // ("module-private async callables are unsupported", exported or not;
    // review R1). Refused, not modelled: a named async call located in an
    // imported module's text, or a call through a value there while the unit
    // has an async lambda.
    bool private_async(NodeId& at) const {
        bool async_lambda = false;
        for (NodeId id = 0; id < ast_.size(); ++id)
            async_lambda = async_lambda || (ast_.at(id).kind == NodeKind::Lambda && (ast_.at(id).flags & kFlagAsync));
        for (NodeId id = 0; id < ast_.size(); ++id) {  // ids follow the text within a unit; the minimum is kept
            const Node& n = ast_.at(id);
            if (n.kind != NodeKind::Call || module_of(n.begin) == 0) continue;
            const Node& callee = ast_.at(n.a);
            std::size_t f = 0;
            const bool named = callee.kind == NodeKind::Ident && function(ast_.text(callee), f);
            NativeInfo native{};
            const bool known = callee.kind == NodeKind::Ident &&
                               (named || thread_builtin(ast_.text(callee)) || a_.constructor(ast_.text(callee)) ||
                                lookup_native(ast_.text(callee), native) || bind_function(ast_.text(callee)));
            if (!(named && is_async(f)) && !(async_lambda && !known && callee.kind != NodeKind::Member)) continue;
            if (at == kNoNode || n.begin < ast_.at(at).begin) at = id;
        }
        return at != kNoNode;
    }

    // A task started in the entry before an import whose code may reach a
    // function of a module imported after the spawn (review R4): legacy's
    // worker does not see it; the linked unit already holds it. Spawns inside
    // module code run only after that module's imports (or are init tasks).
    // The inline form (a function declared after the spawn) is a pre-existing
    // CP22/CP23 issue, left alone (weekend-coro-module.md §7).
    bool before_import(NodeId root, const Effects& e) const {
        if (kMutant == 15) return false;
        const std::uint32_t spawn = ast_.at(root).begin;
        if (module_of(spawn) != 0) return false;
        const std::vector<bool> seen = a_.reached(e.refs);
        const bool by_value = closure(e).by_value;
        for (std::size_t f = 0; f < plan_.functions.size(); ++f) {
            const std::uint32_t begin = ast_.at(plan_.functions[f]).begin;
            if (begin > spawn && module_of(begin) != 0 && (seen[f] || by_value)) return true;
        }
        return false;
    }

    // Every task root (a thread() call, an async call, an async lambda) that
    // starts before an import it reaches (before_import), or that may capture
    // module state (captures). The first in the text is reported.
    bool tasks(NodeId& at, bool before) const {
        bool found = false;
        for (NodeId id = 0; id < ast_.size(); ++id) {
            const Node& n = ast_.at(id);
            Effects e;
            if (n.kind == NodeKind::Lambda && (n.flags & kFlagAsync)) {
                e = walk(n.b, Mode::Task);
            } else if (n.kind == NodeKind::Call && ast_.at(n.a).kind == NodeKind::Ident) {
                const std::string_view t = name(n.a);
                const NodeId* list = ast_.list(n);
                std::uint32_t first = 0;
                std::size_t f = 0;
                if (thread_builtin(t) && n.count > 0) {
                    const Node& target = ast_.at(list[0]);
                    if (target.kind == NodeKind::Lambda) e = walk(target.b, Mode::Task);
                    else if (target.kind == NodeKind::Ident && function(ast_.text(target), f)) e.refs.push_back(f);
                    else e.by_value = true;  // a task body known only as a value
                    first = 1;
                } else if (function(t, f)) {
                    if (!is_async(f)) continue;
                    e.refs.push_back(f);
                } else {
                    continue;
                }
                for (std::uint32_t k = first; k < n.count && kMutant != 6; ++k) {
                    const Effects arg = walk(list[k], Mode::Spawner);
                    e.refs.insert(e.refs.end(), arg.refs.begin(), arg.refs.end());
                    e.merge(arg);
                }
            } else {
                continue;
            }
            if (before ? !before_import(id, e) : !captures(closure(e))) continue;
            if (!found || n.begin < ast_.at(at).begin) at = id;
            found = true;
        }
        return found;
    }

    const Ast& ast_;
    NodeId root_;
    const UnitPlan& plan_;
    coro_module::Analysis a_;
};

}  // namespace

bool check_coroutine_modules(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds,
                             const CompileLimits& limits, CompileResult& result) {
    // The cooperative model keeps CP22's whole-unit `coro-module` refusal
    // (limits.linked, compiler_coro.cpp); sessions refuse coroutines first.
    if (!plan.coroutines || limits.link == nullptr || !limits.link->linked || limits.linked) return true;
    return ModuleCoroutines(ast, root, plan, kinds, *limits.link).run(result);
}

}  // namespace nift::rt
