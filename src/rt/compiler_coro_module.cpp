#include "rt/compiler_coro_module.h"

#include "rt/builtin_bind.h"
#include "rt/module_link.h"
#include "rt/native.h"

#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// rt-lint: cold-file compile-time module coroutine admission, once per linked coroutine unit

// Test-only deliberate defects (`-DNIFT_CORO_MODULE_MUTANT=n`,
// tests/runtime2/coro_module/mutants.sh). Normal builds contain none.
#ifndef NIFT_CORO_MODULE_MUTANT
#define NIFT_CORO_MODULE_MUTANT 0
#endif

namespace nift::rt {

namespace {

constexpr int kMutant = NIFT_CORO_MODULE_MUTANT;

// What code may do when it runs. Conservative: every flag may be set for
// code that never does it (the unit is then refused), never the reverse.
struct Effects {
    std::vector<std::size_t> refs;  // functions it may call or pass on, by name
    bool spawn = false;        // may start a task: thread(), an async call, an async lambda
    bool state = false;        // may read or write a module's top-level binding
    bool by_value = false;     // calls through a value (the target is not known by name)
    void merge(const Effects& o) {
        spawn = spawn || o.spawn;
        state = state || o.state;
        by_value = by_value || o.by_value;
    }
};

// Task: code that runs. Spawner: a task's arguments, evaluated by the code
// that starts it; only the values passed into the task (function names,
// lambda bodies) can run in it. Init: a module's top-level statements
// (function and lambda bodies run only when called).
enum class Mode { Task, Spawner, Init };

class ModuleCoroutines {
public:
    ModuleCoroutines(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds,
                     const LinkedUnit& link)
        : ast_(ast), root_(root), plan_(plan), kinds_(kinds), link_(link) {
        for (std::string_view name : link.module_names) state_names_.insert(name);
        for (const StructInfo& s : plan.structs) {
            for (const StructMethodInfo& m : s.methods) methods_[m.name].push_back(m.function);
            if (s.ctor >= 0) ctors_[s.name] = s.methods[static_cast<std::size_t>(s.ctor)].function;
        }
        const std::size_t n = plan.functions.size();
        fn_effects_.resize(n);
        closure_.resize(n);
        closed_.assign(n, false);
        for (std::size_t f = 0; f < n; ++f) {
            fn_effects_[f] = walk(ast.at(plan.functions[f]).b, Mode::Task);
        }
        values();
    }

    bool run(CompileResult& result) {
        NodeId at = kNoNode;
        if (kMutant != 1 && init_task(at)) return refuse(result, "coro-module-init-task", at);
        if (capture(at)) return refuse(result, "coro-module-capture", at);
        return true;
    }

private:
    struct Item { NodeId id; Mode mode; };

    std::size_t module_of(std::uint32_t offset) const { return link_.locate(offset).module; }
    std::string_view name(NodeId id) const { return ast_.text(ast_.at(id)); }
    bool thread_builtin(std::string_view t) const { return t == "thread" && !plan_.index.count("thread"); }
    bool function(std::string_view t, std::size_t& f) const {
        const auto fn = plan_.index.find(t);
        if (fn == plan_.index.end()) return false;
        f = fn->second - 1u;
        return true;
    }
    bool is_async(std::size_t f) const { return (ast_.at(plan_.functions[f]).flags & kFlagAsync) != 0; }

    bool refuse(CompileResult& result, const char* reason, NodeId at) const {
        result.reason = reason;
        result.offset = ast_.at(at).begin;
        result.positioned = true;  // the refused node's own offset
        return false;
    }

    Effects walk(NodeId start, Mode mode) const {
        Effects e;
        std::vector<Item> work{{start, mode}};
        while (!work.empty()) {
            const Item item = work.back();
            work.pop_back();
            visit(item.id, item.mode, e, work);
        }
        return e;
    }

    // One node's own effects; its children go on `work`.
    void visit(NodeId id, Mode mode, Effects& e, std::vector<Item>& work) const {
        if (id == kNoNode) return;
        const Node& n = ast_.at(id);
        const auto push = [&](NodeId child, Mode m) { if (child != kNoNode) work.push_back({child, m}); };
        switch (n.kind) {
            case NodeKind::FnDecl: case NodeKind::StructDecl: case NodeKind::Param:
                return;  // a definition: its body runs only when called (by name: refs)
            case NodeKind::Lambda:
                if (mode == Mode::Init) return;  // runs only when called (a call through a value)
                if (n.flags & kFlagAsync) e.spawn = true;
                push(n.b, Mode::Task);
                return;
            case NodeKind::Member: case NodeKind::SafeMember:
                push(n.a, mode);  // n.b is the member's name
                return;
            case NodeKind::Ident: {
                const std::string_view t = ast_.text(n);
                const bool by_spawner = mode == Mode::Spawner && kMutant != 10;  // mutant 10: as task code
                if (!by_spawner && state_names_.count(t) && kMutant != 2) e.state = true;
                std::size_t f = 0;
                if (function(t, f)) e.refs.push_back(f);
                return;
            }
            case NodeKind::Call:
                call(n, mode, e, work);
                return;
            default:
                break;
        }
        for (NodeId child : {n.a, n.b, n.c}) push(child, mode);
        const NodeId* list = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) push(list[k], mode);
    }

    void call(const Node& n, Mode mode, Effects& e, std::vector<Item>& work) const {
        const bool spawner = mode == Mode::Spawner;  // the call itself runs in the spawner
        const Node& callee = ast_.at(n.a);
        if (callee.kind == NodeKind::Ident) {
            const std::string_view t = ast_.text(callee);
            NativeInfo native{};
            std::size_t f = 0;
            if (thread_builtin(t)) {
                e.spawn = true;
            } else if (function(t, f)) {
                if (is_async(f)) e.spawn = true;
                if (!spawner) e.refs.push_back(f);
            } else if (ctors_.count(t)) {
                if (!spawner) e.refs.push_back(ctors_.at(t));
            } else if (!lookup_native(t, native) && bind_function(t) == nullptr && t != "error") {
                if (!spawner) e.by_value = true;
            }
        } else if (callee.kind == NodeKind::Member || callee.kind == NodeKind::SafeMember) {
            const auto m = methods_.find(name(callee.b));
            if (!spawner && m != methods_.end()) e.refs.insert(e.refs.end(), m->second.begin(), m->second.end());
            else if (!spawner && (kinds_.of(callee.a) & (kKindFunction | kKindObject | kKindUnknown)))
                e.by_value = true;  // a callable field, or a method the unit does not name
            work.push_back({callee.a, mode});
        } else {
            if (!spawner) e.by_value = true;
            work.push_back({n.a, mode});
        }
        const NodeId* list = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) work.push_back({list[k], mode});
    }

    // Everything the code may do, through every function it may reach.
    Effects closure(const Effects& start) const {
        Effects out = start;
        for (std::size_t f : start.refs) out.merge(closure_of(f));
        return out;
    }
    const Effects& closure_of(std::size_t f) const {
        if (closed_[f]) return closure_[f];
        Effects out;
        std::vector<bool> seen(fn_effects_.size(), false);
        std::vector<std::size_t> stack{f};
        while (!stack.empty()) {
            const std::size_t g = stack.back();
            stack.pop_back();
            if (seen[g]) continue;
            seen[g] = true;
            out.merge(fn_effects_[g]);
            if (kMutant == 3 && g != f) continue;  // mutant: the direct body only
            stack.insert(stack.end(), fn_effects_[g].refs.begin(), fn_effects_[g].refs.end());
        }
        closed_[f] = true;
        return closure_[f] = out;
    }

    // What a call through a value may reach: any function named as a value,
    // and any lambda.
    void values() {
        std::unordered_set<NodeId> callee_or_name;  // and assignment targets (no kind of their own)
        for (NodeId id = 0; id < ast_.size(); ++id) {
            const Node& n = ast_.at(id);
            if (n.kind == NodeKind::Call || n.kind == NodeKind::FnDecl) callee_or_name.insert(n.a);
            if (n.kind == NodeKind::Member || n.kind == NodeKind::SafeMember) callee_or_name.insert(n.b);
            const bool target = n.kind == NodeKind::Declare || n.kind == NodeKind::Assign ||
                                n.kind == NodeKind::CompoundAssign || n.kind == NodeKind::IncDec;
            if (!target || n.a == kNoNode || ast_.at(n.a).kind != NodeKind::Ident) continue;
            callee_or_name.insert(n.a);
            if (state_names_.count(ast_.text(ast_.at(n.a))) && n.b != kNoNode) handle(n.b);  // the value stored
        }
        for (NodeId id = 0; id < ast_.size(); ++id) {
            const Node& n = ast_.at(id);
            Effects e;
            if (n.kind == NodeKind::Lambda) {
                e = closure(walk(n.b, Mode::Task));
                e.spawn = e.spawn || (n.flags & kFlagAsync) != 0;
            } else if (n.kind == NodeKind::Ident && !callee_or_name.count(id)) {
                if (state_names_.count(ast_.text(n))) handle(id);  // a read of module state
                std::size_t f = 0;
                if (!function(ast_.text(n), f)) continue;
                e = closure_of(f);
            } else {
                continue;
            }
            value_spawn_ = value_spawn_ || e.spawn;
            value_state_ = value_state_ || e.state;
        }
    }
    // Legacy captures a module's whole environment: a handle (or a value of
    // unknown kind, refused anyway) anywhere in it may be non-transferable.
    void handle(NodeId value) {
        if ((kinds_.of(value) & (kKindHandle | kKindUnknown)) && kMutant != 7) module_handles_ = true;
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
            visit(item.id, Mode::Init, own, work);
            const std::uint32_t begin = ast_.at(item.id).begin;
            if (kMutant != 9 && module_of(begin) == 0) continue;  // mutant 9: the entry's top level too
            const Effects all = kMutant == 4 ? own : closure(own);
            if (!all.spawn && !(all.by_value && value_spawn_)) continue;
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
        if (c.state || module_handles_) return true;
        return c.by_value && value_state_ && kMutant != 5;
    }

    bool capture(NodeId& at) const {
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
            if (!captures(closure(e))) continue;
            if (!found || n.begin < ast_.at(at).begin) at = id;
            found = true;
        }
        return found;
    }

    const Ast& ast_;
    NodeId root_;
    const UnitPlan& plan_;
    const UnitKinds& kinds_;
    const LinkedUnit& link_;
    std::unordered_set<std::string_view> state_names_;  // top-level bindings of imported modules
    std::unordered_map<std::string_view, std::vector<std::size_t>> methods_;
    std::unordered_map<std::string_view, std::size_t> ctors_;
    std::vector<Effects> fn_effects_;  // each function's own body
    mutable std::vector<Effects> closure_;
    mutable std::vector<bool> closed_;
    bool value_spawn_ = false;     // some function value or lambda may start a task
    bool value_state_ = false;     // some function value or lambda may touch module state
    bool module_handles_ = false;  // a module's top-level binding may hold a handle
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
