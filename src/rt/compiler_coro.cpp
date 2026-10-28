#include "rt/compiler_coro.h"

#include "rt/builtin_bind.h"
#include "rt/compiler_expr.h"
#include "rt/native.h"

#include <vector>

// rt-lint: cold-file compile-time coroutine admission, once per unit

// Test-only deliberate defects (`-DNIFT_CORO_MUTANT=n`, tests/runtime2/cp22):
// 18 captured cells admitted (coro-capture off). Normal builds contain none.
#ifndef NIFT_CORO_MUTANT
#define NIFT_CORO_MUTANT 0
#endif

namespace nift::rt {

namespace detail {

OpCode UnitCompiler::call_opcode(std::size_t f) const {
    return (ast_.at(plan_.functions[f]).flags & kFlagAsync) ? OpCode::Spawn : OpCode::Call;
}

// `type(future)` is legacy's "future" (the native has no future kind);
// `sleep(ms)` in a coroutine unit suspends (Sleep runs the native's checks).
int UnitCompiler::coro_call(const Node& node, std::string_view name, const natives::BuiltinSpec* builtin,
                            std::uint16_t dst) {
    (void)name;
    if (!plan_.coroutines || builtin == nullptr) return -1;
    if (builtin->id == natives::BuiltinId::FnType && node.count == 1) {
        const NodeId arg = ast_.list(node)[0];
        const KindSet k = kinds(arg);
        if (!(k & kKindTask)) return -1;
        if (k != kKindFuture && k != kKindThread) return fail(node, "coro-future-flow:type") ? 1 : 0;
        const std::uint16_t mark = fb_->top();
        std::uint16_t reg = 0, text = 0;
        if (!temp(reg) || !expr_to(arg, reg)) return 0;
        if (!fb_->constant(make_string(k == kKindFuture ? "future" : "thread"), text)) return builder_ok() ? 1 : 0;
        fb_->emit(OpCode::LoadK, dst, static_cast<std::uint16_t>(text & ~kConstantBit));
        fb_->release_to(mark);
        return builder_ok() ? 1 : 0;
    }
    if (builtin->id == natives::BuiltinId::HostSleep) {
        const std::size_t first = fb_->here();
        if (!builtin_call(node, *builtin, kNoNode, dst)) return 0;
        const auto sleep_id = static_cast<std::uint16_t>(kFirstBuiltin + static_cast<std::uint16_t>(builtin->id));
        for (std::size_t at = first; at < fb_->here(); ++at)
            if (fb_->at(at).op == OpCode::CallNative && fb_->at(at).b == sleep_id) fb_->at(at).op = OpCode::Sleep;
        return 1;
    }
    return -1;
}

// `p.done()`, `p.status()` (FutureOp); a thread handle's `t.join()` is an
// Await of the thread (c = 1: legacy's `thread: ` failures).
int UnitCompiler::future_method(const Node& node, std::uint16_t dst) {
    if (!plan_.coroutines) return -1;
    const Node& callee = ast_.at(node.a);
    const KindSet k = kinds(callee.a);
    if (!(k & kKindTask)) return -1;
    const std::string_view name = text(ast_.at(callee.b));
    const bool done = name == "done";
    const bool status = name == "status";
    const bool join = name == "join" && k == kKindThread;
    if ((!done && !status && !join) || node.count != 0 || (k != kKindFuture && k != kKindThread))
        return fail(node, "coro-future-flow:method") ? 1 : 0;
    const std::uint16_t mark = fb_->top();
    std::uint16_t receiver = 0;
    if (!reg_any(callee.a, receiver)) return 0;
    fb_->set_offset(site(node));
    if (join) fb_->emit(OpCode::Await, dst, receiver, 1);
    else fb_->emit(OpCode::FutureOp, dst, receiver, done ? 0 : 1);
    fb_->release_to(mark);
    return builder_ok() ? 1 : 0;
}

bool UnitCompiler::await_expr(const Node& node, std::uint16_t dst) {
    if (!plan_.coroutines) return fail(node, "await-outside-coroutine-unit");
    if (node.a == kNoNode) return fail(node, "await-shape");
    const std::uint16_t mark = fb_->top();
    std::uint16_t operand = 0;
    if (!reg_any(node.a, operand)) return false;
    fb_->set_offset(node.begin);
    fb_->emit(OpCode::Await, dst, operand);
    fb_->release_to(mark);
    return builder_ok();
}

}  // namespace detail

namespace {

// Walks the unit with each node's parent; refuses a future (or an aggregate
// that may hold one) outside the admitted positions (§5 of the record).
class FutureFlow {
public:
    FutureFlow(const Ast& ast, const UnitPlan& plan, const UnitKinds& kinds) : ast_(ast), plan_(plan), kinds_(kinds) {
        // A thread body's result reaches the joining parent (legacy: a handle
        // created in the thread is then invalid, "thread: invalid handle").
        for (NodeId id = 0; id < ast.size(); ++id) {
            const Node& n = ast.at(id);
            if (n.kind != NodeKind::Call || n.count == 0 || !thread_builtin(n.a)) continue;
            const Node& f = ast.at(ast.list(n)[0]);
            const auto fn = f.kind == NodeKind::Ident ? plan.index.find(ast.text(f)) : plan.index.end();
            if (fn != plan.index.end()) thread_bodies_.push_back(plan.functions[fn->second - 1]);
        }
    }

    bool run(NodeId root, std::string& reason, std::uint32_t& offset) {
        // in_function: 0 top level, 1 a function or lambda, 2 an async one.
        struct Item { NodeId id; NodeId parent; int in_function; };
        std::vector<Item> work{{root, kNoNode, 0}};
        while (!work.empty()) {
            const Item item = work.back();
            work.pop_back();
            if (item.id == kNoNode) continue;
            const Node& n = ast_.at(item.id);
            int in_function = item.in_function;
            if (n.kind == NodeKind::FnDecl || n.kind == NodeKind::Lambda) in_function = (n.flags & kFlagAsync) ? 2 : 1;
            if (n.kind == NodeKind::FnDecl && thread_body(item.id)) in_function = 2;
            if (!check(item.id, item.parent, item.in_function, reason)) {
                offset = n.begin;
                return false;
            }
            for (NodeId child : {n.a, n.b, n.c}) work.push_back({child, item.id, in_function});
            const NodeId* list = ast_.list(n);
            for (std::uint32_t k = 0; k < n.count; ++k) work.push_back({list[k], item.id, in_function});
        }
        return true;
    }

private:
    bool is_async_function(std::string_view name) const {
        const auto fn = plan_.index.find(name);
        return fn != plan_.index.end() && (ast_.at(plan_.functions[fn->second - 1]).flags & kFlagAsync);
    }
    bool holds(NodeId id) const {
        return (kinds_.of(id) & kKindTask) || kinds_.future_holders.count(id) != 0;
    }
    bool thread_builtin(NodeId callee) const {
        return callee != kNoNode && ast_.at(callee).kind == NodeKind::Ident && ast_.text(ast_.at(callee)) == "thread" &&
               !plan_.index.count("thread");
    }
    bool thread_body(NodeId fn) const {
        for (NodeId body : thread_bodies_) if (body == fn) return true;
        return false;
    }
    std::string_view member_name(const Node& member) const { return ast_.text(ast_.at(member.b)); }

    bool check(NodeId id, NodeId parent, int in_function, std::string& reason) const {
        const Node& n = ast_.at(id);
        // A named async function is only ever called directly (legacy L14:
        // as a value its future fails with "invalid async callable").
        if (n.kind == NodeKind::Ident && value_position(id, parent) && is_async_function(ast_.text(n)) &&
            !(ast_.at(parent).kind == NodeKind::Call && ast_.at(parent).a == id)) {
            reason = "coro-async-value";
            return false;
        }
        if (parent == kNoNode || !holds(id) || n.kind == NodeKind::Param) return true;  // a parameter's name
        if (allowed(id, ast_.at(parent), in_function)) return true;
        reason = std::string("coro-future-flow:") + node_kind_name(ast_.at(parent).kind);
        return false;
    }

    // An identifier read as a value (not a declaration's, parameter's,
    // member's or property's name, nor an assignment target).
    bool value_position(NodeId id, NodeId parent) const {
        if (parent == kNoNode) return false;
        const Node& p = ast_.at(parent);
        switch (p.kind) {
            case NodeKind::FnDecl: case NodeKind::Param: case NodeKind::Property: case NodeKind::ForBinding:
            case NodeKind::Pattern: case NodeKind::StructDecl: case NodeKind::StructField: case NodeKind::Import:
            case NodeKind::Export: case NodeKind::Try:
                return false;
            case NodeKind::Member: case NodeKind::SafeMember: return p.a == id;
            case NodeKind::Declare: case NodeKind::Assign: case NodeKind::CompoundAssign: case NodeKind::IncDec:
                return p.a != id;
            default: return true;
        }
    }

    bool allowed(NodeId id, const Node& p, int in_function) const {
        const KindSet k = kinds_.of(id);
        switch (p.kind) {
            case NodeKind::Declare:  // `x := f()`, or the declared name itself
                return (p.b == id || p.a == id) && ast_.at(p.a).kind == NodeKind::Ident;
            case NodeKind::Assign:  // `x = f()` / `hs[i] = f()`, or that target
                return ast_.at(p.a).kind == NodeKind::Ident || ast_.at(p.a).kind == NodeKind::Index;
            // A task's result may not hold a future: legacy's parent cannot use
            // a future created in the task ("await: invalid future").
            case NodeKind::Return: return p.a == id && in_function == 1;
            case NodeKind::Array: case NodeKind::Await: return true;
            case NodeKind::ExprStmt: return n_is(id, NodeKind::Call) || n_is(id, NodeKind::Await);
            case NodeKind::If: case NodeKind::While: return p.a == id;
            case NodeKind::Unary: return p.op == Op::Not;
            case NodeKind::Ternary: return true;  // the condition's truth, or a branch (checked as the ternary)
            case NodeKind::Index: return p.a == id;
            case NodeKind::ForBinding: return p.a != id || k == kKindArray;  // a loop name, or the iterated array
            case NodeKind::Member: return member_receiver(id, p, k);
            case NodeKind::Call: return call_argument(id, p, k);
            default: return false;
        }
    }

    bool n_is(NodeId id, NodeKind kind) const { return ast_.at(id).kind == kind; }

    // `p.done()` / `p.status()` on a future; `hs.push(x)`, `hs.size()`,
    // `hs.length()` on an array holding futures (never inspected).
    bool member_receiver(NodeId id, const Node& member, KindSet k) const {
        if (member.a != id) return false;
        const std::string_view name = member_name(member);
        if (k == kKindFuture) return name == "done" || name == "status";
        if (k == kKindThread) return name == "done" || name == "status" || name == "join";
        return k == kKindArray && (name == "push" || name == "size" ||
                                   name == "length");
    }

    // An argument of a user function, a function value, `push` or `type`.
    bool call_argument(NodeId id, const Node& call, KindSet k) const {
        if (call.a == id) return false;  // a future is never called
        const Node& callee = ast_.at(call.a);
        if (callee.kind == NodeKind::Member)
            return member_name(callee) == "push" && kinds_.of(callee.a) == kKindArray;
        if (callee.kind != NodeKind::Ident) return false;
        const std::string_view name = ast_.text(callee);
        if (plan_.index.count(name)) return true;
        if (thread_builtin(call.a)) return true;  // handles reach a thread's snapshot (legacy shares them)
        if (name == "cancel") return k == kKindFuture || k == kKindThread;  // CP23 cooperative cancellation
        if (name == "type") return k == kKindFuture || k == kKindThread || !(k & kKindTask);
        // A call through a binding (a lambda value): legacy binds the value.
        return (kinds_.of(call.a) & kKindFunction) && !(kinds_.of(call.a) & kKindUnknown);
    }

    const Ast& ast_;
    const UnitPlan& plan_;
    const UnitKinds& kinds_;
    std::vector<NodeId> thread_bodies_;
};

// Calls made in a subtree: named functions of the unit (by index), and
// whether any call goes through a value (a binding, not a function or built-in).
void calls_in(const Ast& ast, NodeId root, const UnitPlan& plan, std::vector<std::size_t>& named, bool& by_value) {
    std::vector<NodeId> work{root};
    while (!work.empty()) {
        const NodeId id = work.back();
        work.pop_back();
        if (id == kNoNode) continue;
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::Call && n.a != kNoNode && ast.at(n.a).kind == NodeKind::Ident) {
            const std::string_view name = ast.text(ast.at(n.a));
            const auto fn = plan.index.find(name);
            NativeInfo native{};
            if (fn != plan.index.end()) {
                named.push_back(fn->second - 1u);
            } else if (name == "thread" && n.count > 0 && ast.at(ast.list(n)[0]).kind == NodeKind::Ident) {
                const auto body = plan.index.find(ast.text(ast.at(ast.list(n)[0])));  // `thread(f, …)` calls f
                if (body != plan.index.end()) named.push_back(body->second - 1u);
            } else if (!lookup_native(name, native) && bind_function(name) == nullptr && name != "error") {
                by_value = true;
            }
        }
        for (NodeId child : {n.a, n.b, n.c}) work.push_back(child);
        const NodeId* list = ast.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) work.push_back(list[k]);
    }
}

// Legacy runs an async callable's task with that callable made synchronous
// (`set_named_callable_async(…, false)`, lambda `async = false`): a call of it
// from inside its own task (recursion, also through other functions) returns
// a value where the VM would return a future. Such units are refused.
bool async_recursion(const Ast& ast, NodeId root, const UnitPlan& plan, std::uint32_t& offset) {
    const std::size_t n = plan.functions.size();
    std::vector<std::vector<std::size_t>> callees(n);
    std::vector<bool> by_value(n, false);
    for (std::size_t f = 0; f < n; ++f) {
        bool v = false;
        calls_in(ast, ast.at(plan.functions[f]).b, plan, callees[f], v);
        by_value[f] = v;
    }
    const auto reach = [&](const std::vector<std::size_t>& start, std::size_t target, bool& value_call) {
        std::vector<bool> seen(n, false);
        std::vector<std::size_t> work(start);
        bool found = false;
        while (!work.empty()) {
            const std::size_t f = work.back();
            work.pop_back();
            if (f >= n || seen[f]) continue;
            seen[f] = true;
            found = found || f == target;
            value_call = value_call || by_value[f];
            work.insert(work.end(), callees[f].begin(), callees[f].end());
        }
        return found;
    };
    for (std::size_t f = 0; f < n; ++f) {
        if (!(ast.at(plan.functions[f]).flags & kFlagAsync)) continue;
        bool value_call = false;
        if (reach(callees[f], f, value_call)) {
            offset = ast.at(plan.functions[f]).begin;
            return true;
        }
    }
    for (NodeId id = 0; id < ast.size(); ++id) {
        const Node& lambda = ast.at(id);
        if (lambda.kind != NodeKind::Lambda || !(lambda.flags & kFlagAsync)) continue;
        std::vector<std::size_t> named;
        bool value_call = false;
        calls_in(ast, lambda.b, plan, named, value_call);
        reach(named, n, value_call);
        if (value_call) {
            offset = lambda.begin;
            return true;
        }
    }
    (void)root;
    return false;
}

}  // namespace

bool check_coroutine_unit(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds,
                          const CompileLimits& limits, std::string& reason, std::uint32_t& offset) {
    if (!plan.coroutines) return true;
    for (NodeId id = 0; !(limits.cooperative_threads || limits.parallel_tasks) && id < ast.size(); ++id) {
        const Node& n = ast.at(id);
        if (n.kind == NodeKind::Call && n.a != kNoNode && ast.at(n.a).kind == NodeKind::Ident &&
            ast.text(ast.at(n.a)) == "thread" && !plan.index.count("thread"))
            return reason = "callable:thread", offset = ast.at(n.a).begin, false;
    }
    offset = ast.at(root).begin;
    if (limits.linked) { reason = "coro-module"; return false; }
    if (!plan.structs.empty()) {
        reason = "coro-struct";
        offset = ast.at(plan.structs.front().decl).begin;
        return false;
    }
    if (async_recursion(ast, root, plan, offset)) {
        reason = "coro-async-recursion";
        return false;
    }
    return FutureFlow(ast, plan, kinds).run(root, reason, offset);  // cooperative admission: after compiling
}

bool check_coroutine_program(const Program& program, std::string& reason, std::uint32_t& offset) {
    for (std::size_t p = 0; p < program.protos.size(); ++p) {
        const Proto& proto = *program.protos[p];
        for (std::size_t pc = 0; pc < proto.code.size(); ++pc) {
            const Instr& in = proto.code[pc];
            const char* why = nullptr;
            switch (in.op) {
                case OpCode::Closure: if (in.c != 0 && NIFT_CORO_MUTANT != 18) why = "coro-capture"; break;
                case OpCode::NewCell: case OpCode::NewSourceSlot: case OpCode::CopyCellLocation:
                case OpCode::CellAnchor: case OpCode::CellSlot: case OpCode::CellTake:
                    if (p == 0) why = "coro-main-cell";
                    break;
                case OpCode::CollectionCall: case OpCode::CollectionSort: case OpCode::CollectionStep:
                    why = "coro-collection-callback";
                    break;
                default: break;
            }
            if (why == nullptr) continue;
            reason = why;
            offset = pc < proto.offsets.size() ? proto.offsets[pc] : 0;
            return false;
        }
    }
    return true;
}

}  // namespace nift::rt
