#include "rt/compiler_coro_module_effects.h"

#include "rt/builtin_bind.h"
#include "rt/module_link.h"
#include "rt/native.h"

// rt-lint: cold-file compile-time module coroutine admission, once per linked coroutine unit

namespace nift::rt::coro_module {

Analysis::Analysis(const Ast& ast, const UnitPlan& plan, const UnitKinds& kinds, const LinkedUnit& link)
    : ast_(ast), plan_(plan), kinds_(kinds), link_(link) {
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

std::size_t Analysis::module_of(std::uint32_t offset) const { return link_.locate(offset).module; }
bool Analysis::thread_builtin(std::string_view t) const { return t == "thread" && !plan_.index.count("thread"); }
bool Analysis::function(std::string_view t, std::size_t& f) const {
    const auto fn = plan_.index.find(t);
    if (fn == plan_.index.end()) return false;
    f = fn->second - 1u;
    return true;
}
bool Analysis::is_async(std::size_t f) const { return (ast_.at(plan_.functions[f]).flags & kFlagAsync) != 0; }

Effects Analysis::walk(NodeId start, Mode mode) const {
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
void Analysis::visit(NodeId id, Mode mode, Effects& e, std::vector<Item>& work) const {
    if (id == kNoNode) return;
    const Node& n = ast_.at(id);
    const auto push = [&](NodeId child, Mode m) { if (child != kNoNode) work.push_back({child, m}); };
    switch (n.kind) {
        case NodeKind::FnDecl: case NodeKind::StructDecl: case NodeKind::Param:
            return;  // a definition: its body runs only when called (by name: refs)
        case NodeKind::Lambda:
            // A lambda created during initialisation may run during it
            // (called, or passed to a built-in callback): its body counts
            // as initialisation code (review R2; mutant 13: skipped).
            if (mode == Mode::Init && kMutant == 13) return;
            if (n.flags & kFlagAsync) e.spawn = true;
            push(n.b, mode == Mode::Init ? Mode::Init : Mode::Task);
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

// An argument that may be a function: a lambda, a function's name, or a
// value whose kinds include a function.
bool Analysis::function_argument(NodeId arg) const {
    const Node& a = ast_.at(arg);
    std::size_t f = 0;
    if (a.kind == NodeKind::Lambda || (a.kind == NodeKind::Ident && function(ast_.text(a), f))) return true;
    return (kinds_.of(arg) & kKindFunction) != 0;
}

void Analysis::call(const Node& n, Mode mode, Effects& e, std::vector<Item>& work) const {
    const bool spawner = mode == Mode::Spawner;  // the call itself runs in the spawner
    const Node& callee = ast_.at(n.a);
    // A built-in or native callee (a callback method such as map/filter/
    // sort, or a native function) may call any function value it is given:
    // a call through a value (review R2/R3a; mutant 11: not seen).
    bool native_callee = false;
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
        } else {
            native_callee = true;
        }
    } else if (callee.kind == NodeKind::Member || callee.kind == NodeKind::SafeMember) {
        // A method is never resolved by its name alone (review R3b): the
        // receiver's struct is not known here, so a receiver that may be
        // an object, a function or unknown is a call through a value (a
        // callable field), and every user method of that name may run.
        const auto m = methods_.find(name(callee.b));
        const bool named = m != methods_.end();
        if (!spawner && named) e.refs.insert(e.refs.end(), m->second.begin(), m->second.end());
        if (!spawner && (!named || kMutant != 12) &&
            (kinds_.of(callee.a) & (kKindFunction | kKindObject | kKindUnknown)))
            e.by_value = true;
        native_callee = !named;
        work.push_back({callee.a, mode});
    } else {
        if (!spawner) e.by_value = true;
        work.push_back({n.a, mode});
    }
    const NodeId* list = ast_.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k) {
        const bool seen = kMutant != 11 && !(kMutant == 13 && mode == Mode::Init);  // mutants 11, 13
            if (!spawner && native_callee && seen && function_argument(list[k])) e.by_value = true;
        work.push_back({list[k], mode});
    }
}

// Everything the code may do, through every function it may reach.
Effects Analysis::closure(const Effects& start) const {
    Effects out = start;
    for (std::size_t f : start.refs) out.merge(closure_of(f));
    return out;
}
const Effects& Analysis::closure_of(std::size_t f) const {
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
void Analysis::values() {
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
void Analysis::handle(NodeId value) {
    if ((kinds_.of(value) & (kKindHandle | kKindUnknown)) && kMutant != 7) module_handles_ = true;
}

// Functions a task may reach by name.
std::vector<bool> Analysis::reached(const std::vector<std::size_t>& start) const {
    std::vector<bool> seen(fn_effects_.size(), false);
    std::vector<std::size_t> stack(start);
    while (!stack.empty()) {
        const std::size_t g = stack.back();
        stack.pop_back();
        if (seen[g]) continue;
        seen[g] = true;
        stack.insert(stack.end(), fn_effects_[g].refs.begin(), fn_effects_[g].refs.end());
    }
    return seen;
}


}  // namespace nift::rt::coro_module
