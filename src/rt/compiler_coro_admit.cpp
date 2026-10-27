#include "rt/compiler_coro_admit.h"

#include "rt/builtin_bind.h"
#include "rt/native.h"
#include "rt/natives/builtin.h"

#include <array>
#include <charconv>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// rt-lint: cold-file compile-time cooperative admission, once per coroutine unit

// Test-only deliberate defect (`-DNIFT_CORO_MUTANT=12`, tests/runtime2/cp22):
// the admission rule admits everything. Normal builds contain none.
#ifndef NIFT_CORO_MUTANT
#define NIFT_CORO_MUTANT 0
#endif

namespace nift::rt {

namespace {

// Host built-ins that neither block nor wait on I/O: `sleep` suspends the
// task; the rest read process state in constant time.
constexpr std::array<std::string_view, 7> kHostAllowed = {"sleep", "epoch", "getenv", "os", "arch", "platform",
                                                         "hardware_concurrency"};
// Free functions task code may call (besides the unit's own functions):
// constant-time or output-only, none blocking. `thread` only matters with the
// non-contractual cooperative-thread switch.
constexpr std::array<std::string_view, 11> kTaskFunctions = {
    "print", "sleep", "error", "type", "epoch", "getenv", "os", "arch", "platform", "hardware_concurrency", "thread"};
// Methods task code may call: future/thread state, array size and append.
constexpr std::array<std::string_view, 6> kTaskMethods = {"done", "status", "size", "length", "push", "join"};

template <std::size_t N>
bool listed(const std::array<std::string_view, N>& list, std::string_view name) {
    for (std::string_view entry : list)
        if (entry == name) return true;
    return false;
}

class Admission {
public:
    Admission(const Ast& ast, const UnitPlan& plan) : ast_(ast), plan_(plan) {}

    bool run(NodeId root, std::string& reason, std::uint32_t& offset) {
#if NIFT_CORO_MUTANT == 12
        return true;  // M12: cooperative admission disabled
#endif
        if (!host_calls(root, reason, offset)) return false;
        collect_tasks();
        if (tasks_.empty()) return true;
        if (cyclic(reason, offset)) return false;
        for (NodeId fn : tasks_)
            if (!task_body(fn, reason, offset)) return false;
        return true;
    }

private:
    bool user_function(std::string_view name) const { return plan_.index.count(name) != 0; }
    NodeId function_node(std::string_view name) const {
        const auto it = plan_.index.find(name);
        return it == plan_.index.end() ? kNoNode : plan_.functions[it->second - 1];
    }
    template <class Visit>
    void walk(NodeId root, Visit&& visit, bool into_functions) const {
        std::vector<NodeId> work{root};
        while (!work.empty()) {
            const NodeId id = work.back();
            work.pop_back();
            if (id == kNoNode) continue;
            const Node& n = ast_.at(id);
            if (!visit(id, n)) return;
            if (!into_functions && id != root && (n.kind == NodeKind::FnDecl || n.kind == NodeKind::Lambda)) continue;
            for (NodeId child : {n.a, n.b, n.c}) work.push_back(child);
            const NodeId* list = ast_.list(n);
            for (std::uint32_t k = 0; k < n.count; ++k) work.push_back(list[k]);
        }
    }

    // A blocking Host call anywhere in the unit (main included): while it
    // blocks, legacy's other threads progress and the VM's tasks cannot.
    bool host_calls(NodeId root, std::string& reason, std::uint32_t& offset) const {
        bool ok = true;
        walk(root, [&](NodeId, const Node& n) {
            if (n.kind != NodeKind::Call || n.a == kNoNode || ast_.at(n.a).kind != NodeKind::Ident) return true;
            const std::string_view name = ast_.text(ast_.at(n.a));
            if (user_function(name) || listed(kHostAllowed, name)) return true;
            const natives::BuiltinSpec* spec = bind_function(name);
            if (spec == nullptr || !natives::host_builtin(spec->id)) return true;
            reason = "coro-blocking-host:" + std::string(name);
            offset = n.begin;
            return ok = false;
        }, true);
        return ok;
    }

    // Task code: async functions and async lambdas, every function or lambda
    // they can reach by name, and, when task code calls through a value,
    // every lambda and every function named as a value (conservative).
    void collect_tasks() {
        std::vector<NodeId> work;
        for (NodeId fn : plan_.functions)
            if (ast_.at(fn).flags & kFlagAsync) work.push_back(fn);
        for (NodeId id = 0; id < ast_.size(); ++id)
            if (ast_.at(id).kind == NodeKind::Lambda && (ast_.at(id).flags & kFlagAsync)) work.push_back(id);
        bool widened = false;
        while (!work.empty()) {
            const NodeId fn = work.back();
            work.pop_back();
            if (!seen_.insert(fn).second) continue;
            tasks_.push_back(fn);
            std::vector<NodeId>& out = callees_[fn];
            bool by_value = false;
            walk(ast_.at(fn).b, [&](NodeId id, const Node& n) {
                if (n.kind == NodeKind::Lambda) out.push_back(id);
                if (n.kind == NodeKind::Ident) {
                    const NodeId target = function_node(ast_.text(n));
                    if (target != kNoNode) out.push_back(target);  // a call, or the function as a value
                }
                if (n.kind == NodeKind::Call && n.a != kNoNode) {
                    const Node& callee = ast_.at(n.a);
                    by_value = by_value || (callee.kind != NodeKind::Member &&
                                            (callee.kind != NodeKind::Ident || !known_callee(ast_.text(callee))));
                }
                return true;
            }, true);
            if (by_value) values_.push_back(fn);
            if (by_value && !widened) {
                widened = true;
                value_targets();
                work.insert(work.end(), targets_.begin(), targets_.end());
            }
            work.insert(work.end(), out.begin(), out.end());
        }
    }
    bool known_callee(std::string_view name) const {
        NativeInfo native{};
        return user_function(name) || bind_function(name) != nullptr || lookup_native(name, native) ||
               listed(kTaskFunctions, name);
    }
    // What a call through a value may reach: every lambda, and every function
    // named anywhere other than as a direct callee.
    void value_targets() {
        for (NodeId id = 0; id < ast_.size(); ++id) {
            const Node& n = ast_.at(id);
            if (n.kind == NodeKind::Lambda) targets_.push_back(id);
            if (n.kind == NodeKind::Call && n.a != kNoNode) direct_.insert(n.a);
        }
        for (NodeId id = 0; id < ast_.size(); ++id) {
            const Node& n = ast_.at(id);
            if (n.kind != NodeKind::Ident || direct_.count(id)) continue;
            const NodeId target = function_node(ast_.text(n));
            if (target != kNoNode && ast_.at(target).a != id) targets_.push_back(target);
        }
    }

    // A call cycle among task functions (sync recursion inside a task is
    // unbounded CPU work); a call through a value may reach any of them.
    bool cyclic(std::string& reason, std::uint32_t& offset) {
        enum Mark : std::uint8_t { None, Open, Done };
        std::unordered_map<NodeId, Mark> mark;
        for (NodeId start : tasks_) {
            if (mark[start] != None) continue;
            std::vector<std::pair<NodeId, std::size_t>> stack{{start, 0}};
            mark[start] = Open;
            while (!stack.empty()) {
                auto& [fn, next] = stack.back();
                const std::vector<NodeId> edges = edges_of(fn);
                if (next == edges.size()) {
                    mark[fn] = Done;
                    stack.pop_back();
                    continue;
                }
                const NodeId to = edges[next++];
                if (mark[to] == Open) {
                    reason = "coro-task-recursion";
                    offset = ast_.at(to).begin;
                    return true;
                }
                if (mark[to] == None) {
                    mark[to] = Open;
                    stack.push_back({to, 0});
                }
            }
        }
        return false;
    }
    std::vector<NodeId> edges_of(NodeId fn) const {
        std::vector<NodeId> edges;
        const auto it = callees_.find(fn);
        if (it != callees_.end()) edges = it->second;
        for (NodeId v : values_)
            if (v == fn) edges.insert(edges.end(), targets_.begin(), targets_.end());
        return edges;
    }

    // A loop is admitted only when every iteration suspends: its body has a
    // top-level `sleep(<integer literal >= 1>)` statement.
    bool suspending_loop(const Node& loop) const {
        const NodeId body = loop.b;
        if (body == kNoNode) return false;
        const Node& b = ast_.at(body);
        const NodeId* list = ast_.list(b);
        const std::uint32_t count = b.kind == NodeKind::Block ? b.count : 0;
        for (std::uint32_t k = 0; k < count; ++k) {
            const Node& s = ast_.at(list[k]);
            if (s.kind != NodeKind::ExprStmt || s.a == kNoNode) continue;
            const Node& call = ast_.at(s.a);
            if (call.kind != NodeKind::Call || call.count != 1 || ast_.at(call.a).kind != NodeKind::Ident) continue;
            if (ast_.text(ast_.at(call.a)) != "sleep" || user_function("sleep")) continue;
            const Node& arg = ast_.at(ast_.list(call)[0]);
            if (arg.kind != NodeKind::Number) continue;
            const std::string_view text = ast_.text(arg);
            long long ms = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), ms);
            if (parsed.ec == std::errc() && parsed.ptr == text.data() + text.size() && ms >= 1) return true;
        }
        return false;
    }

    bool task_body(NodeId fn, std::string& reason, std::uint32_t& offset) const {
        bool ok = true;
        walk(ast_.at(fn).b, [&](NodeId, const Node& n) {
            if ((n.kind == NodeKind::While || n.kind == NodeKind::For) && !suspending_loop(n)) {
                reason = "coro-task-loop";
                offset = n.begin;
                return ok = false;
            }
            if (n.kind != NodeKind::Call || n.a == kNoNode) return true;
            const Node& callee = ast_.at(n.a);
            std::string_view name;
            if (callee.kind == NodeKind::Member && callee.b != kNoNode) {
                name = ast_.text(ast_.at(callee.b));
                if (listed(kTaskMethods, name)) return true;
            } else if (callee.kind == NodeKind::Ident) {
                name = ast_.text(callee);
                if (user_function(name) || listed(kTaskFunctions, name) || !known_callee(name)) return true;
            } else {
                return true;  // a call through a value: its target is task code too
            }
            reason = "coro-task-native:" + std::string(name);
            offset = n.begin;
            return ok = false;
        }, false);
        return ok;
    }

    const Ast& ast_;
    const UnitPlan& plan_;
    std::vector<NodeId> tasks_;
    std::unordered_set<NodeId> seen_;
    std::unordered_map<NodeId, std::vector<NodeId>> callees_;
    std::vector<NodeId> values_;   // task functions that call through a value
    std::vector<NodeId> targets_;  // what a call through a value may reach
    std::unordered_set<NodeId> direct_;
};

}  // namespace

bool check_cooperative_admission(const Ast& ast, NodeId root, const UnitPlan& plan, std::string& reason,
                                 std::uint32_t& offset) {
    return Admission(ast, plan).run(root, reason, offset);
}

}  // namespace nift::rt
