#include "rt/reference_guard.h"

#include "rt/builtin_bind.h"
#include "rt/collection.h"
#include "rt/reference_guard_walk.h"

// rt-lint: cold-file compile-time Phase A forward-compatibility guard: freshness and verdict, once per unit

namespace nift::rt {
namespace reference_guard_detail {

bool Guard::run(NodeId root, std::string& reason, std::uint32_t& offset) {
    escape_.assign(plan_.functions.size(), {});
    for (std::size_t f = 0; f < plan_.functions.size(); ++f)
        escape_[f].assign(ast_.at(plan_.functions[f]).count, false);
    for (int round = 0; round < 64; ++round) {
        facts_.clear();
        calls_.clear();
        idiom_args_.clear();
        isolated_.clear();
        unknown_.clear();
        unknown_mutations_.clear();
        opaque_mutations_.clear();
        returns_.assign(plan_.functions.size(), {});
        free_reads_.clear();
        owners_.clear();
        lends_.clear();
        user_calls_.clear();
        fn_value_.assign(plan_.functions.size(), false);
        note_params();
        walk(root, false, -1);
        if (!grow_escapes()) break;
    }
    return check(reason, offset);
}

// `handed`: a parameter whose value counts as fresh (the idiom's argument,
// handed over by an exclusive caller binding that is rebound to the result).
bool Guard::fresh(NodeId id, bool deep, int depth, NodeId handed) const {
    if (id == kNoNode || !shared(id)) return true;
    if (depth > 32) return false;
    const Node& n = at(id);
    switch (n.kind) {
        case NodeKind::Array: case NodeKind::Object: {
            if (!deep) return true;
            const NodeId* items = ast_.list(n);
            for (std::uint32_t k = 0; k < n.count; ++k) {
                const Node& item = at(items[k]);
                const NodeId value = item.kind == NodeKind::Property ? item.b : items[k];
                if (item.kind == NodeKind::Spread || !fresh(value, true, depth + 1, handed)) return false;
            }
            return true;
        }
        case NodeKind::Ternary: return fresh(n.b, deep, depth + 1, handed) && fresh(n.c, deep, depth + 1, handed);
        case NodeKind::Coalesce: return fresh(n.a, deep, depth + 1, handed) && fresh(n.b, deep, depth + 1, handed);
        case NodeKind::Call: return fresh_call(id, deep, depth);
        case NodeKind::Ident: {
            if (handed == kNoNode) return false;
            const auto found = kinds_.bindings.find(id);
            return found != kinds_.bindings.end() && found->second == handed;
        }
        default: return false;  // a place, an awaited result (the future keeps it), anything else
    }
}

bool Guard::fresh_rule(natives::ResultRule rule, NodeId id, bool deep) const {
    using natives::ResultRule;
    if (rule == ResultRule::Fixed) return true;
    if (rule == ResultRule::ElementsOf || rule == ResultRule::Merged) return !deep || !nested(id);
    return false;  // ElementOf, MemberOf, Selected: a part of an argument
}

bool Guard::fresh_call(NodeId id, bool deep, int depth) const {
    const Node& n = at(id);
    const Node& callee = at(n.a);
    if (callee.kind == NodeKind::Ident) {
        const std::string_view name = ast_.text(callee);
        if (kinds_.bindings.count(n.a)) return false;  // a callable value: unknown returns
        if (const auto fn = plan_.index.find(name); fn != plan_.index.end())
            return returns_fresh(fn->second - 1u, deep, depth);
        if (name == "deepcopy") return true;
        if (name == "copy") return !deep || !nested(id);
        if (const natives::BuiltinSpec* spec = bind_function(name)) return fresh_rule(spec->rule, id, deep);
        return false;
    }
    if (callee.kind != NodeKind::Member) return false;
    const std::string_view name = ast_.text(at(callee.b));
    const KindSet recv = kinds(callee.a);
    if (recv & (kKindCollection | kKindStruct)) return false;  // extracted from a collection or a method's result
    const int m = collection_method(name);
    if (m >= 0) {
        using M = CollectionMethod;
        switch (static_cast<M>(m)) {
            case M::Map: case M::Filter: case M::SortBy: case M::UniqueBy: case M::GroupBy: case M::IndexBy:
            case M::Partition: case M::CountBy: case M::GroupByEach: case M::Splice:
                return !deep || !nested(id);
            default: return false;  // an element, the receiver, a stored argument
        }
    }
    const natives::Receiver receiver = recv == kKindArray    ? natives::Receiver::Array
                                       : recv == kKindObject ? natives::Receiver::Object
                                       : recv == kKindString ? natives::Receiver::String
                                                             : natives::Receiver::Any;
    const natives::BuiltinSpec* spec = natives::find_builtin(receiver, name);
    return spec != nullptr && fresh_rule(spec->rule, id, deep);
}

bool Guard::returns_fresh(std::size_t f, bool deep, int depth) const {
    if (f >= returns_.size() || depth > 8) return false;
    for (NodeId value : returns_[f])
        if (!fresh(value, deep, depth + 1)) return false;
    return true;
}

bool Guard::grow_escapes() {
    bool grew = false;
    for (std::size_t fn = 0; fn < plan_.functions.size(); ++fn) {
        const Node& node = at(plan_.functions[fn]);
        const NodeId* params = ast_.list(node);
        for (std::uint32_t k = 0; k < node.count; ++k) {
            if (escape_[fn][k]) continue;
            const auto found = facts_.find(params[k]);
            if (found == facts_.end()) continue;
            const Facts& p = found->second;
            // A parameter a closure captures outlives the call (its cell).
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 18
            const bool captured = false;  // R18: a parameter a closure captures does not escape
#else
            const bool captured = outside(params[k]);
#endif
            if (!mutated(p) && !p.returned && p.retained_top.empty() && p.retained_sub.empty() && !captured)
                continue;
            escape_[fn][k] = true;
            grew = true;
        }
    }
    return grew;
}

bool Guard::outside(NodeId d) const {
    const auto f = facts_.find(d);
    const int owner = f == facts_.end() ? -1 : f->second.owner;
    if (session_ && owner == -1) return true;  // a session binding: later statements reach it
    const auto seen = owners_.find(d);
    if (seen == owners_.end()) return false;
    if (seen->second.size() >= 2) return true;
    return f != facts_.end() && f->second.declared && seen->second.front() != owner;
}

bool Guard::user_code(NodeId region) const {
    const Node& r = at(region);
    for (NodeId c : user_calls_)
        if (within(at(c), r)) return true;
    return false;
}

bool Guard::fail(NodeId at_node, const char* route, std::string& reason, std::uint32_t& offset) const {
    reason = std::string("reference-guard:") + route;
    offset = at_node == kNoNode ? 0 : at(at_node).begin;
    return false;
}

bool Guard::check(std::string& reason, std::uint32_t& offset) {
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 13
    return true;  // R13: the forward-compatibility guard is off
#endif
    if (!opaque_mutations_.empty()) return fail(opaque_mutations_.front(), "collection-element", reason, offset);
    if (!unknown_mutations_.empty()) return fail(unknown_mutations_.front(), "unresolved", reason, offset);
    // A loop binding or a native callback's parameter is a part of its
    // operand's root: captured by a closure, that part outlives the loop.
    std::vector<std::pair<NodeId, NodeId>> captured_parts;
    for (const auto& [d, f] : facts_)
        if (f.loop && shared(d) && outside(d)) {  // a scalar part shares nothing
            if (f.loop_root != kNoNode) captured_parts.push_back({f.loop_root, d});
            else unknown_.push_back(d);
        }
    for (const auto& [root, part] : captured_parts) facts_[root].retained_sub.push_back(part);
    for (auto& [d, f] : facts_) {
        if (f.loop) continue;  // a part of its iterable's root (checked there)
        // An idiom argument may be mutated at any depth by the callee.
        // A persistent session binding (declared at the top level) may be
        // mutated by any later statement; a function's or lambda's local is
        // reached only by this unit's code, which the guard sees (review
        // round 2: treating locals as session state refused recompiles of
        // admitted history units).
        const bool persistent = session_ && f.owner == -1;
        const bool nested_mutation = persistent || f.mut_nested || !f.idioms.empty();
        if (!persistent && !mutated(f)) continue;
        if (!f.retained_top.empty()) return fail(f.retained_top.front(), "shared", reason, offset);
        if (nested_mutation && !f.retained_sub.empty())
            return fail(f.retained_sub.front(), "shared-part", reason, offset);
        for (NodeId value : f.inflows)
            if (!fresh(value, nested_mutation) && !idiom_result(f, value) && !carried(value))
                return fail(value, "inflow", reason, offset);
        if (nested_mutation)
            for (NodeId value : f.contents)
                if (!fresh(value, true)) return fail(value, "contents", reason, offset);
        if (f.param_fn == -2 && !f.isolated && mutated(f)) return fail(f.mut_at, "lambda-parameter", reason, offset);
        if (f.param_fn >= 0 && !f.isolated && mutated(f))
            for (const auto& [call, fn] : calls_) {
                if (fn != f.param_fn || f.param_k >= at(call).count) continue;
                const NodeId arg = ast_.list(at(call))[f.param_k];
                if (fresh(arg, nested_mutation) || idiom_argument(arg)) continue;
                return fail(arg, "argument", reason, offset);
            }
    }
    // The lent rule (D-NEXT-refsem #1): while a binding's value is lent (to
    // a call's parameter, a native and its callbacks, a loop), no mutation of
    // the binding may run; one written inside the region, or one in another
    // body when the region may run user code, would be seen through the
    // lent alias in Phase B but not in the copy model.
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 17
    lends_.clear();  // R17: the lent rule is off (the review's g01/h01/i08/j01 shapes run)
#endif
    for (const Lend& lend : lends_) {
        const auto found = facts_.find(lend.decl);
        if (found == facts_.end()) continue;
        const Facts& f = found->second;
        if (!(session_ && f.owner == -1) && !mutated(f)) continue;
        const Node& region = at(lend.region);
        for (NodeId site : f.mut_sites)
            if (site != lend.region && within(at(site), region) && !within(at(site), at(lend.value)))
                return fail(site, "lent", reason, offset);
        if (outside(lend.decl) && user_code(lend.region)) return fail(lend.region, "lent", reason, offset);
    }
    // A user function used as a value, or any in a session, has callers the
    // guard cannot see: a parameter it mutates may hold anyone's value.
    for (std::size_t fn = 0; fn < plan_.functions.size(); ++fn) {
        if (!session_ && !fn_value_[fn]) continue;
        const Node& node = at(plan_.functions[fn]);
        if (node.flags & kFlagAsync) continue;
        const NodeId* params = ast_.list(node);
        for (std::uint32_t k = 0; k < node.count; ++k) {
            const auto p = facts_.find(params[k]);
            if (p != facts_.end() && mutated(p->second)) return fail(params[k], "argument", reason, offset);
        }
    }
    if (!unknown_.empty()) {
        // An unresolved holder (an imported binding): only harmless when
        // nothing in the unit mutates an array or object in place.
        for (const auto& [d, f] : facts_)
            if (!f.loop && mutated(f)) return fail(unknown_.front(), "unresolved", reason, offset);
    }
    return true;
}

bool Guard::idiom_result(const Facts& f, NodeId value) const {
    for (const Idiom& idiom : f.idioms) {
        if (idiom.call != value || idiom.fn < 0 || static_cast<std::size_t>(idiom.fn) >= returns_.size()) continue;
        const NodeId param = ast_.list(at(plan_.functions[static_cast<std::size_t>(idiom.fn)]))[idiom.k];
        bool ok = true;
        for (NodeId r : returns_[static_cast<std::size_t>(idiom.fn)]) ok = ok && fresh(r, true, 0, param);
        if (ok) return true;
    }
    return false;
}

bool Guard::idiom_argument(NodeId arg) const {
    for (const auto& [d, f] : facts_) {
        for (const Idiom& idiom : f.idioms) {
            if (idiom.arg != arg) continue;
            // x = f(x): x is exclusive (checked as mutated above) and no
            // other body references x (it would see the callee's mutation
            // through a second holder).
            for (NodeId read : free_reads_)
                if (read == d) return false;
            return !outside(d);
        }
    }
    return false;
}

} // namespace reference_guard_detail

bool check_reference_guard(const Ast& ast, NodeId root, const UnitPlan& plan, const UnitKinds& kinds, bool session,
                           std::string& reason, std::uint32_t& offset,
                           const std::unordered_map<NodeId, NodeId>* carried) {
    reference_guard_detail::Guard guard(ast, plan, kinds, session);
    if (carried)
        for (const auto& [declaration, initializer] : *carried) guard.carry(initializer);
    return guard.run(root, reason, offset);
}

} // namespace nift::rt
