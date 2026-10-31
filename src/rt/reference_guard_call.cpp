#include "rt/reference_guard_call.h"

#include "rt/builtin_bind.h"
#include "rt/collection.h"

// rt-lint: cold-file compile-time Phase A forward-compatibility guard: calls, lendings and callbacks, once per unit

namespace nift::rt::reference_guard_detail {

void Guard::lend(NodeId value, NodeId region, int owner) {
    if (!shared(value)) return;
    const Root r = root_of(value, owner);
    if (!r.known) {
        if (at(value).kind == NodeKind::Ident || r.opaque) unknown_.push_back(value);
        return;
    }
    if (r.decl != kNoNode) lends_.push_back({r.decl, region, value});
}

void Guard::tie_callbacks(const Node& call, NodeId root, bool known) {
    const NodeId* args = ast_.list(call);
    for (std::uint32_t k = 0; k < call.count; ++k) {
        const Node& lambda = at(args[k]);
        if (lambda.kind != NodeKind::Lambda || (lambda.flags & kFlagAsync)) continue;
        const NodeId* params = ast_.list(lambda);
        for (std::uint32_t j = 0; j < lambda.count; ++j) {
            Facts& p = facts(params[j]);
            p.loop = true;  // a part of the native's operand, like a loop binding
            p.loop_root = known ? root : kNoNode;
            p.owner = lambda_owner(args[k]);
            p.declared = true;
        }
    }
}

bool Guard::escapes(int f, std::uint32_t k) const {
    return f >= 0 && static_cast<std::size_t>(f) < escape_.size() && k < escape_[f].size() && escape_[f][k];
}

int Guard::user_function(const Node& n, int owner) const {
    const Node& callee = at(n.a);
    if (callee.kind == NodeKind::Ident) {
        const std::string_view name = ast_.text(callee);
        if (kinds_.bindings.count(n.a)) return -1;
        if (const auto fn = plan_.index.find(name); fn != plan_.index.end()) return fn->second - 1;
        if (const auto s = plan_.struct_index.find(name); s != plan_.struct_index.end()) {
            const StructInfo& info = plan_.structs[s->second];
            return info.ctor >= 0 ? static_cast<int>(info.methods[static_cast<std::size_t>(info.ctor)].function)
                                  : -1;
        }
        if (owner >= 0 && plan_.is_method(static_cast<std::size_t>(owner))) {
            const StructInfo& s = plan_.structs[static_cast<std::size_t>(plan_.method_of[owner])];
            if (const StructMethodInfo* m = s.method(name)) return static_cast<int>(m->function);
        }
        return -1;
    }
    if (callee.kind != NodeKind::Member) return -1;
    int s = -1;
    if (at(callee.a).kind == NodeKind::Ident && ast_.text(at(callee.a)) == "this" && owner >= 0 &&
        plan_.is_method(static_cast<std::size_t>(owner)))
        s = plan_.method_of[owner];
    else if (const auto found = kinds_.struct_ids.find(callee.a); found != kinds_.struct_ids.end())
        s = found->second;
    if (s < 0) return -1;
    const StructMethodInfo* m = plan_.structs[static_cast<std::size_t>(s)].method(ast_.text(at(callee.b)));
    return m != nullptr ? static_cast<int>(m->function) : -1;
}

bool Guard::callable_value(NodeId arg) const {
    return at(arg).kind != NodeKind::Lambda && (kinds(arg) & kKindFunction) != 0;
}

// A native given a callable value (not a literal) passes it parts of its
// operands: those parts reach a holder the guard cannot see.
void Guard::native_operands(const Node& n, NodeId receiver, NodeId id, int owner) {
    const NodeId* args = ast_.list(n);
    bool opaque_callback = false;
    for (std::uint32_t k = 0; k < n.count; ++k) opaque_callback = opaque_callback || callable_value(args[k]);
    if (opaque_callback) user_calls_.push_back(id);
    NodeId tie = receiver;
    if (tie == kNoNode) {  // a free native: its one non-callable aggregate operand
        std::uint32_t operands = 0;
        for (std::uint32_t k = 0; k < n.count; ++k)
            if (at(args[k]).kind != NodeKind::Lambda && shared(args[k])) { tie = args[k]; ++operands; }
        if (operands != 1) tie = kNoNode;
    }
    const Root r = tie != kNoNode ? root_of(tie, owner) : Root{};
    tie_callbacks(n, r.decl, r.known && r.decl != kNoNode);
    auto operand = [&](NodeId value) {
        lend(value, id, owner);
        if (!opaque_callback || !shared(value)) return;
        const Root v = root_of(value, owner);
        if (v.known && v.decl != kNoNode) facts(v.decl).retained_sub.push_back(value);
        else if (!v.known && (at(value).kind == NodeKind::Ident || v.opaque)) unknown_.push_back(value);
    };
    if (receiver != kNoNode) operand(receiver);
    for (std::uint32_t k = 0; k < n.count; ++k)
        if (at(args[k]).kind != NodeKind::Lambda) operand(args[k]);
}

void Guard::call(NodeId id, bool keep, int owner) {
    const Node& n = at(id);
    const Node& callee = at(n.a);
    const NodeId* args = ast_.list(n);
    if (keep) retain(id, owner);  // an element-returning method of a binding's value
    const int f = user_function(n, owner);
    if (f >= 0) {
        calls_.push_back({id, f});
        user_calls_.push_back(id);
        if (callee.kind == NodeKind::Member) walk(callee.a, false, owner);
        // An async function's arguments are copied at task creation (both models).
        const bool task = (at(plan_.functions[static_cast<std::size_t>(f)]).flags & kFlagAsync) != 0;
        for (std::uint32_t k = 0; k < n.count; ++k) {
            const bool retained = !task && escapes(f, k) && !idiom_args_.count(args[k]);
            // A parameter that does not escape holds the argument for the call.
            if (!task && !retained && !idiom_args_.count(args[k])) lend(args[k], id, owner);
            walk(args[k], retained, owner);
        }
        return;
    }
    if (callee.kind == NodeKind::Ident) {
        const std::string_view name = ast_.text(callee);
        const bool value_call = kinds_.bindings.count(n.a) != 0;
        const bool isolate = name == "thread";
        const bool deep = name == "deepcopy";
        // A name that is no binding, user function or built-in the compiler
        // knows (an earlier session statement's function is a binding).
        const bool unknown_fn = false;
        if (value_call || unknown_fn) user_calls_.push_back(id);
        if (value_call) walk(n.a, false, owner);
        if (!value_call && !isolate && !unknown_fn) native_operands(n, kNoNode, id, owner);
        for (std::uint32_t k = 0; k < n.count; ++k) {
            // A built-in whose result may hold aggregates may hold the
            // argument's parts (copy, merge, ...); a callable value's
            // parameters may escape anywhere.
            // A built-in returning one of its arguments (validate, min, max:
            // Selected) keeps the whole value; one whose result may hold
            // aggregates below its top may hold the argument's parts.
            const natives::BuiltinSpec* spec = value_call ? nullptr : bind_function(name);
            const bool whole = spec != nullptr && spec->rule == natives::ResultRule::Selected && name != "copy" &&
                               name != "deepcopy";
            const bool part = !isolate && !deep && !value_call && shared(id) && (whole || nested(id));
            walk(args[k], (value_call || unknown_fn) && !isolate, owner);
            if (part && shared(args[k])) {
                const Root r = root_of(args[k], owner);
                if (r.known && r.decl != kNoNode)
                    (whole && r.whole ? facts(r.decl).retained_top : facts(r.decl).retained_sub).push_back(args[k]);
                else if (!r.known && at(args[k]).kind == NodeKind::Ident) unknown_.push_back(args[k]);
            }
        }
        return;
    }
    if (callee.kind != NodeKind::Member) {
        user_calls_.push_back(id);
        walk(n.a, false, owner);
        walk_list(n, true, owner);
        return;
    }
    const std::string_view name = ast_.text(at(callee.b));
    const KindSet recv = kinds(callee.a);
    if ((kinds(n.a) & kKindFunction) && !(recv & kKindArray)) {  // `o.f(args)`: a callable member, called
        user_calls_.push_back(id);
        walk(callee.a, false, owner);
        for (std::uint32_t k = 0; k < n.count; ++k) walk(args[k], true, owner);
        return;
    }
    if (recv & kKindCollection) {  // map/set/stack stores keep their arguments (references)
        for (std::uint32_t k = 0; k < n.count; ++k)
            if (callable_value(args[k])) user_calls_.push_back(id);
        walk(callee.a, false, owner);
        for (std::uint32_t k = 0; k < n.count; ++k) walk(args[k], true, owner);
        return;
    }
    const int m = collection_method(name);
    const bool mutator = m >= 0 && m < static_cast<int>(CollectionMethod::Map);
    if ((recv & kShared) && (mutator || (m < 0 && !pure_method_name(name) && name != "length" &&
                                         name != "size"))) {
        const NodeId receiver = callee.a;
        mutate(receiver, true, id, owner);
        const bool stores = m == static_cast<int>(CollectionMethod::Push) ||
                            m == static_cast<int>(CollectionMethod::Insert) ||
                            m == static_cast<int>(CollectionMethod::Splice);
        // push/insert/splice store their arguments and never call them.
        bool callback = false;
        for (std::uint32_t k = 0; k < n.count && !stores; ++k)
            callback = callback || at(args[k]).kind == NodeKind::Lambda || callable_value(args[k]);
        // A mutator running a callback (a comparator) holds its receiver
        // while user code runs (its own mutation is the call itself).
        if (callback) native_operands(n, receiver, id, owner);
        walk(callee.a, false, owner);
        for (std::uint32_t k = 0; k < n.count; ++k) {
            walk(args[k], stores, owner);
            if (stores) store_into(receiver, args[k], owner);
        }
        return;
    }
    // A reading method: a result holding aggregates may hold the
    // receiver's parts (handled by retain above) or an argument's.
    native_operands(n, (recv & kShared) ? callee.a : kNoNode, id, owner);
    walk(callee.a, false, owner);
    for (std::uint32_t k = 0; k < n.count; ++k) {
        walk(args[k], false, owner);
        if (shared(id) && shared(args[k]) && (nested(id) || !fresh(id, false))) {
            const Root r = root_of(args[k], owner);
            if (r.known && r.decl != kNoNode) facts(r.decl).retained_sub.push_back(args[k]);
        }
    }
}

void Guard::idiom(const Node& assign, NodeId d, int owner) {
    const Node& value = at(assign.b);
    if (value.kind != NodeKind::Call) return;
    const int f = user_function(value, owner);
    if (f < 0) return;
    const NodeId* args = ast_.list(value);
    std::uint32_t uses = 0, position = 0;
    for (std::uint32_t k = 0; k < value.count; ++k)
        if (at(args[k]).kind == NodeKind::Ident && decl(args[k], owner) == d) { ++uses; position = k; }
    if (uses != 1 || !shared(args[position])) return;
    facts(d).idioms.push_back({assign.b, args[position], f, position});
    idiom_args_.insert(args[position]);
}

} // namespace nift::rt::reference_guard_detail
