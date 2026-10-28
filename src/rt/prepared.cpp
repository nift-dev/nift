#include "rt/prepared.h"

// rt-lint: cold-file compile-time analysis, once per unit

#include "rt/prepared_outer.h"
#include "rt/prepared_parse.h"
#include "rt/source_scan.h"
#include "rt/text_type.h"

#include <functional>
#include <unordered_set>
#include <vector>

namespace nift::rt {

namespace {

// Pushes the children of `n` so that they pop in pre-order (a, b, c, list).
void push_children(const Ast& ast, const Node& n, std::vector<NodeId>& stack) {
    const NodeId* list = ast.list(n);
    for (std::uint32_t k = n.count; k-- > 0;)
        if (list[k] != kNoNode) stack.push_back(list[k]);
    for (NodeId child : {n.c, n.b, n.a})
        if (child != kNoNode) stack.push_back(child);
}

// Pre-order walk of the subtree `id`; stops (returns false) when `visit`
// (bool(NodeId, const Node&)) does. Iterative (review round 3, F3): no
// tree shape can exhaust the C++ stack here.
template <class Visit> bool walk(const Ast& ast, NodeId id, const Visit& visit) {
    if (id == kNoNode) return true;
    std::vector<NodeId> stack{id};
    while (!stack.empty()) {
        const NodeId next = stack.back();
        stack.pop_back();
        const Node& n = ast.at(next);
        if (!visit(next, n)) return false;
        push_children(ast, n, stack);
    }
    return true;
}

class Analysis {
public:
    Analysis(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds* kinds)
        : ast_(ast), root_(root), plan_(plan), kinds_(kinds) {}

    PreparedRefusal run() {
        mark_prepared_functions();
        result_.loop_called = marked_;
        mark_unpreparable_functions();
        check_unpreparable_calls();
        for (std::size_t f = 0; f < plan_.functions.size() && !result_.refused; ++f)
            check_regions(ast_.at(plan_.functions[f]).b, marked_[f], f);
        if (!result_.refused) top_level([this](NodeId id) { check_regions(id, false); return !result_.refused; });
        for (std::size_t f = 0; f < plan_.functions.size() && !result_.refused; ++f) {
            caller_ = static_cast<int>(f);
            visible_ready_ = false;
            walk(ast_, ast_.at(plan_.functions[f]).b, [this](NodeId, const Node& n) { return check_string_args(n); });
        }
        if (!result_.refused) {
            caller_ = -1;
            visible_ready_ = false;
            top_level([this](NodeId id) {
                return walk(ast_, id, [this](NodeId, const Node& n) { return check_string_args(n); });
            });
        }
        return result_;
    }

private:
    bool refuse(const Node& at, const char* reason) {
        if (!result_.refused) {
            result_.refused = true;
            result_.reason = reason;
            result_.offset = at.begin;
        }
        return false;
    }

    int callee_index(const Node& call) const {
        if (call.kind != NodeKind::Call || ast_.at(call.a).kind != NodeKind::Ident) return -1;
        const auto fn = plan_.index.find(ast_.text(ast_.at(call.a)));
        return fn == plan_.index.end() ? -1 : static_cast<int>(fn->second) - 1;
    }

    // Calls `visit` for each top-level statement other than a function.
    template <class Visit> bool top_level(const Visit& visit) const {
        const Node& program = ast_.at(root_);
        const NodeId* list = ast_.list(program);
        for (std::uint32_t k = 0; k < program.count; ++k)
            if (ast_.at(list[k]).kind != NodeKind::FnDecl && !visit(list[k])) return false;
        return true;
    }

    static void declared_in(const Ast& ast, NodeId id, std::unordered_set<std::string_view>& out) {
        walk(ast, id, [&](NodeId, const Node& n) {
            if (n.kind == NodeKind::Declare && n.a != kNoNode && ast.at(n.a).kind == NodeKind::Ident)
                out.insert(ast.text(ast.at(n.a)));
            if (n.kind == NodeKind::Param) out.insert(ast.text(n));
            return true;
        });
    }

    // Binding names per function (parameters and locals) and at top level,
    // and the call graph (callers of each function).
    void collect_declared_names() {
        top_level([this](NodeId id) { declared_in(ast_, id, global_names_); return true; });
        function_names_.resize(plan_.functions.size());
        callers_.resize(plan_.functions.size());
        for (std::size_t f = 0; f < plan_.functions.size(); ++f) {
            declared_in(ast_, plan_.functions[f], function_names_[f]);
            walk(ast_, ast_.at(plan_.functions[f]).b, [&](NodeId, const Node& n) {
                const int g = callee_index(n);
                if (g >= 0) callers_[static_cast<std::size_t>(g)].push_back(f);
                return true;
            });
        }
    }

    // Names legacy may find in its scope chain during a call made from
    // function `f` (-1: top level): globals, and the parameters and locals of
    // f and of every function that can be active below it (its callers,
    // transitively).
    void visible_names(int f) {
        visible_ = global_names_;
        if (f < 0) return;
        std::vector<bool> seen(plan_.functions.size(), false);
        std::vector<std::size_t> pending{static_cast<std::size_t>(f)};
        seen[static_cast<std::size_t>(f)] = true;
        while (!pending.empty()) {
            const std::size_t g = pending.back();
            pending.pop_back();
            visible_.insert(function_names_[g].begin(), function_names_[g].end());
            for (std::size_t caller : callers_[g])
                if (!seen[caller]) { seen[caller] = true; pending.push_back(caller); }
        }
    }

    void mark_calls(NodeId id) {
        walk(ast_, id, [this](NodeId, const Node& n) {
            const int f = callee_index(n);
            if (f >= 0 && !marked_[static_cast<std::size_t>(f)] &&  // CP22: a task runs canonically
                !(ast_.at(plan_.functions[static_cast<std::size_t>(f)]).flags & kFlagAsync)) {
                marked_[static_cast<std::size_t>(f)] = true;
                changed_ = true;
            }
            return true;
        });
    }

    // Seeds: calls in any loop condition or body; then every call in the body
    // of a marked function, to a fixpoint.
    void mark_prepared_functions() {
        marked_.assign(plan_.functions.size(), false);
        walk(ast_, root_, [this](NodeId id, const Node& n) {
            if (n.kind == NodeKind::While) mark_calls(id);
            return true;
        });
        do {
            changed_ = false;
            for (std::size_t f = 0; f < plan_.functions.size(); ++f)
                if (marked_[f]) mark_calls(ast_.at(plan_.functions[f]).b);
        } while (changed_);
    }

    // Prepared function bodies: every declaration referring to a binding (the
    // body is prepared in the caller's scope, before its own parameters
    // exist) and preparability. Elsewhere, loops: legacy prepares the
    // outermost loop when it starts, so a declaration inside it reads the
    // types of the bindings visible at that loop's header; that is the
    // binding the VM resolves unless the name is (re)declared inside the
    // loop (a loop-local shadow, as in `a := 2.5; b := a`).
    void check_regions(NodeId body, bool prepared_function, std::size_t f = 0) {
        if (prepared_function) {
            std::vector<std::string_view> params;
            walk(ast_, plan_.functions[f], [&](NodeId, const Node& n) {
                if (n.kind == NodeKind::Param) params.push_back(ast_.text(n));
                return true;
            });
            walk(ast_, body, [&](NodeId, const Node& n) { return check_declaration(n, nullptr, f, &params); });
            return;
        }
        outermost_loops(body, [this](const Node& n) {
            std::unordered_set<std::string_view> inside;
            walk(ast_, n.b, [&](NodeId, const Node& d) {
                if (d.kind == NodeKind::Declare && d.a != kNoNode && ast_.at(d.a).kind == NodeKind::Ident)
                    inside.insert(ast_.text(ast_.at(d.a)));
                return true;
            });
            return walk(ast_, n.b, [&](NodeId, const Node& inner) { return check_declaration(inner, &inside); });
        });
    }

    // Calls `visit` for every `while` not nested in another one (nested loops
    // are prepared with their outermost loop); false stops.
    template <class Visit> bool outermost_loops(NodeId id, const Visit& visit) {
        if (id == kNoNode) return true;
        std::vector<NodeId> stack{id};
        while (!stack.empty()) {
            const NodeId next = stack.back();
            stack.pop_back();
            const Node& n = ast_.at(next);
            if (n.kind == NodeKind::While) {
                if (!visit(n)) return false;
                continue;
            }
            push_children(ast_, n, stack);
        }
        return true;
    }

    bool is_binding_name(std::string_view name) const {
        if (name == "true" || name == "false" || name == "null") return false;
        return !plan_.index.count(name);
    }

    // Does the text type name a binding (from `names`, when given)?
    bool refers_to(const TextType& type, const std::unordered_set<std::string_view>* names) const {
        if (type.kind == TextType::Kind::Binding)
            return is_binding_name(type.binding) && (!names || names->count(type.binding));
        if (type.kind == TextType::Kind::Meet) return refers_to(*type.left, names) || refers_to(*type.right, names);
        return false;
    }

    // Legacy types the declaration from the text after `:=` (decl_type),
    // evaluated when the body is prepared.
    // Stage 4 (D-046 #1): `x := p` keeps p's declared type (P5). Legacy types it when preparing, from
    // `p` in the caller's scopes (decl_type; not found or int: the value's type): admitted only where
    // that is equal, i.e. every binding p outside the function is certainly int typed (rt/prepared_outer.h)
    // and every call passes p untyped or int text. Otherwise refused (review2_m1_param_caller_scope, L52).
    bool param_copy_matches_legacy(std::size_t f, const std::vector<std::string_view>& params, std::size_t k) {
        if (!outer_bindings_int_typed(ast_, root_, plan_, f, params[k], kinds_)) return false;
        return walk(ast_, root_, [&](NodeId, const Node& n) {
            if (callee_index(n) != static_cast<int>(f)) return true;
            if (k >= n.count) return false;
            const Node& arg = ast_.at(ast_.list(n)[k]);
            const TextType t = infer_text_type(ast_.text(arg));
            return arg.kind == NodeKind::String || (t.kind == TextType::Kind::Known && (t.known == -1 || t.known == 2));
        });
    }

    bool check_declaration(const Node& n, const std::unordered_set<std::string_view>* risky_names, std::size_t f = 0,
                           const std::vector<std::string_view>* params = nullptr) {
        if (n.kind != NodeKind::Declare) return true;
        const std::string_view src = ast_.source();
        const std::uint32_t op = detail::find_in(src, ":=", n.begin, n.end);
        if (op == n.end) return refuse(n, "prepared-decl-type");
        for (std::size_t k = 0; params && k < params->size(); ++k)
            if ((*params)[k] == detail::trim(src.substr(op + 2, n.end - op - 2)))
                return param_copy_matches_legacy(f, *params, k) || refuse(n, "prepared-decl-type");
        if (refers_to(infer_text_type(src.substr(op + 2, n.end - op - 2)), risky_names))
            return refuse(n, "prepared-decl-type");
        return true;
    }

    // The parenthesised header of `if`/`while` at node.begin, as legacy's
    // translator copies it.
    bool header_text(const Node& n, std::size_t keyword, std::string_view& out) const {
        const std::string_view src = ast_.source();
        const auto end = static_cast<std::uint32_t>(src.size());
        const std::uint32_t open = detail::skip_space(src, n.begin + static_cast<std::uint32_t>(keyword), end);
        std::uint32_t close = 0;
        if (open >= end || src[open] != '(' || !detail::find_balanced(src, open, end, '(', ')', close)) return false;
        out = src.substr(open + 1, close - open - 1);
        return true;
    }

    // `@return(EXPR)`: legacy's find_balanced must close at the generated ')'.
    bool return_preparable(const Node& n) const {
        const std::string_view src = ast_.source();
        const std::string_view expr = detail::trim(src.substr(n.begin + 6, n.end - n.begin - 6));
        const std::string wrapped = "(" + std::string(expr) + ")";
        std::uint32_t close = 0;
        if (!detail::find_balanced(wrapped, 0, static_cast<std::uint32_t>(wrapped.size()), '(', ')', close) ||
            close + 1 != wrapped.size())
            return false;
        return legacy_prepared_parse_accepts(expr);
    }

    // Can legacy's prepared evaluator prepare this statement of a function
    // body (a bare `return`, or an `if`/`while` condition or `return`
    // expression its parser rejects, cannot be prepared)?
    bool preparable(const Node& n) const {
        std::string_view text;
        switch (n.kind) {
            case NodeKind::Return: return !(n.flags & kFlagBare) && n.a != kNoNode && return_preparable(n);
            case NodeKind::If: return header_text(n, 2, text) && legacy_prepared_parse_accepts(text);
            case NodeKind::While: return header_text(n, 5, text) && legacy_prepared_parse_accepts(text);
            default: return true;
        }
    }

    // Functions whose body legacy cannot prepare (P2/P3).
    void mark_unpreparable_functions() {
        unpreparable_.assign(plan_.functions.size(), false);
        for (std::size_t f = 0; f < plan_.functions.size(); ++f)
            unpreparable_[f] = !walk(ast_, ast_.at(plan_.functions[f]).b,
                                     [this](NodeId, const Node& n) { return preparable(n); });
    }

    // An argument whose second evaluation cannot be observed: a literal or a
    // name read (a local binding; anything else the compiler refuses).
    bool unobservable(NodeId id) const {
        const Node& n = ast_.at(id);
        switch (n.kind) {
            case NodeKind::Ident: return !plan_.index.count(ast_.text(n));
            case NodeKind::Number: case NodeKind::True: case NodeKind::False: case NodeKind::Null: return true;
            case NodeKind::String: return n.count == 0;
            case NodeKind::Unary: return n.op == Op::Neg && ast_.at(n.a).kind == NodeKind::Number;
            default: return false;
        }
    }

    // records20k coverage (stage 4): when legacy cannot prepare a function
    // called from prepared code, it evaluates the arguments, abandons the
    // preparation and evaluates the call's text again on its textual
    // (canonical) path: the body runs once, canonically (the VM's meaning),
    // and only the second evaluation of the arguments differs (M2). Such a
    // call is admitted when every argument is a literal or a name read
    // (evaluating it twice cannot be observed); otherwise it is refused
    // (prepared-fn-unprepared). Prepared code: loop conditions and bodies
    // anywhere, and the bodies of functions called from them (marked_).
    bool check_call_sites(NodeId id) {
        return walk(ast_, id, [this](NodeId, const Node& n) {
            const int f = callee_index(n);
            if (f < 0 || !unpreparable_[static_cast<std::size_t>(f)]) return true;
            const NodeId* args = ast_.list(n);
            for (std::uint32_t k = 0; k < n.count; ++k)
                if (!unobservable(args[k])) return refuse(ast_.at(args[k]), "prepared-fn-unprepared");
            return true;
        });
    }

    void check_unpreparable_calls() {
        for (std::size_t f = 0; f < plan_.functions.size() && !result_.refused; ++f)
            if (marked_[f]) check_call_sites(ast_.at(plan_.functions[f]).b);
        if (result_.refused) return;
        walk(ast_, root_, [this](NodeId, const Node& n) {
            if (n.kind != NodeKind::While) return true;
            return check_call_sites(n.a) && check_call_sites(n.b);
        });
    }

    // Legacy's bind_call_param types a quoted argument from its unquoted text.
    int quoted_type(const TextType& type, bool& risky) {
        if (type.kind == TextType::Kind::Known) return type.known;
        if (type.kind == TextType::Kind::Binding) {
            if (!visible_ready_) {  // computed only when a string literal names something
                if (!names_collected_) {
                    collect_declared_names();
                    names_collected_ = true;
                }
                visible_names(caller_);
                visible_ready_ = true;
            }
            if (visible_.count(type.binding) || type.binding.substr(0, 2) == "__") risky = true;
            return -1;
        }
        return meet_text_types(quoted_type(*type.left, risky), quoted_type(*type.right, risky));
    }

    bool check_string_args(const Node& n) {
        if (callee_index(n) < 0) return true;
        const NodeId* args = ast_.list(n);
        for (std::uint32_t k = 0; k < n.count; ++k) {
            const Node& arg = ast_.at(args[k]);
            if (arg.kind != NodeKind::String) continue;
            const std::string_view text = ast_.text(arg);
            if (text.size() < 2) return refuse(arg, "string-arg-type");
            bool risky = false;
            const int type = quoted_type(infer_text_type(text.substr(1, text.size() - 2)), risky);
            if (risky || (type != -1 && type != 2)) return refuse(arg, "string-arg-type");
        }
        return true;
    }

    const Ast& ast_;
    NodeId root_;
    const UnitPlan& plan_;
    const UnitKinds* kinds_;  // null: not converged
    std::unordered_set<std::string_view> global_names_, visible_;
    int caller_ = -1;
    bool visible_ready_ = false;
    bool names_collected_ = false;
    std::vector<std::unordered_set<std::string_view>> function_names_;
    std::vector<std::vector<std::size_t>> callers_;
    std::vector<bool> marked_;
    std::vector<bool> unpreparable_;  // per function: legacy cannot prepare its body
    bool changed_ = false;
    PreparedRefusal result_;
};

} // namespace

PreparedRefusal check_prepared_contexts(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds* kinds) {
    return Analysis(ast, root, plan, kinds).run();
}

} // namespace nift::rt
