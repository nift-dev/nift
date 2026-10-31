#include "rt/reference_guard_walk.h"

#include "rt/builtin_bind.h"
#include "rt/collection.h"

// rt-lint: cold-file compile-time Phase A forward-compatibility guard: the syntax walk, once per unit

namespace nift::rt::reference_guard_detail {

NodeId Guard::decl(NodeId ident, int owner) {
    const auto found = kinds_.bindings.find(ident);
    if (found != kinds_.bindings.end()) {
        // In an async body a binding declared outside it is the task's own
        // copy (made at task creation in both models): a distinct holder.
        const NodeId d = found->second;
        const auto outer = facts_.find(d);
        const int declared_in = outer == facts_.end() ? -1 : outer->second.owner;
        if (async_depth_ == 0 || declared_in == owner) return d;
        const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(owner)) << 32) | d;
        auto copy = isolated_.find(key);
        if (copy == isolated_.end())
            copy = isolated_.emplace(key, static_cast<NodeId>(ast_.size() + isolated_.size())).first;
        facts(copy->second).owner = owner;
        return copy->second;
    }
    if (owner >= 0 && plan_.is_method(static_cast<std::size_t>(owner))) {  // an implicit struct field
        const StructInfo& s = plan_.structs[static_cast<std::size_t>(plan_.method_of[owner])];
        if (const StructFieldInfo* field = s.field(ast_.text(at(ident)))) return field->node;
    }
    return kNoNode;
}

void Guard::note_params() {
    for (std::size_t f = 0; f < plan_.functions.size(); ++f) {
        const Node& fn = at(plan_.functions[f]);
        const NodeId* params = ast_.list(fn);
        for (std::uint32_t k = 0; k < fn.count; ++k) {
            Facts& p = facts(params[k]);
            p.param_fn = static_cast<int>(f);
            p.param_k = k;
            p.owner = static_cast<int>(f);
            p.isolated = (fn.flags & kFlagAsync) != 0;  // its arguments are copied at task creation
        }
    }
}

Guard::Root Guard::root_of(NodeId id, int owner) {
    const Node& n = at(id);
    switch (n.kind) {
        case NodeKind::Ident: {
            if (ast_.text(n) == "this") return {kNoNode, false, true};
            const NodeId d = decl(id, owner);
            if (d == kNoNode) return {kNoNode, true, false};
            const Facts& f = facts(d);
            if (f.loop) return {f.loop_root, false, f.loop_root != kNoNode};
            return {d, true, true};
        }
        case NodeKind::Index: case NodeKind::Member: case NodeKind::SafeIndex: case NodeKind::SafeMember: {
            Root r = root_of(n.a, owner);
            r.whole = false;
            return r;
        }
        case NodeKind::Call: {
            const Node& callee = at(n.a);
            if (callee.kind != NodeKind::Member) return {};
            Root r = root_of(callee.a, owner);
            // An array mutator's array result may be the receiver itself.
            const int m = collection_method(ast_.text(at(callee.b)));
            r.whole = r.whole && (m == static_cast<int>(CollectionMethod::Sort) ||
                                  m == static_cast<int>(CollectionMethod::Reverse) ||
                                  m == static_cast<int>(CollectionMethod::ComparatorSort));
            return r;
        }
        case NodeKind::Await: return root_of(n.a, owner);
        default: return {};
    }
}

void Guard::retain(NodeId id, int owner) {
    if (!shared(id)) return;
    const Root r = root_of(id, owner);
    if (!r.known) {
        if (at(id).kind == NodeKind::Ident) unknown_.push_back(id);
        return;
    }
    if (r.decl == kNoNode) return;  // `this` or a fresh value
    // A new outer container built from the binding's parts (map, filter,
    // slice, ...) shares those parts only when they may be arrays or objects.
    if (!r.whole && at(id).kind == NodeKind::Call && fresh(id, false) && !nested(id)) return;
    Facts& f = facts(r.decl);
    (r.whole ? f.retained_top : f.retained_sub).push_back(id);
}

void Guard::mutate(NodeId receiver, bool whole_receiver, NodeId site, int owner) {
    if (!shared(receiver)) return;
    const Root r = root_of(receiver, owner);
    if (!r.known) { unknown_mutations_.push_back(site); return; }
    if (r.decl == kNoNode) return;
    Facts& f = facts(r.decl);
    const bool top = r.whole && whole_receiver;
    (top ? f.mut_top : f.mut_nested) = true;
    if (f.mut_at == kNoNode) f.mut_at = site;
}

void Guard::store_into(NodeId receiver, NodeId value, int owner) {
    const Root r = root_of(receiver, owner);
    if (r.known && r.decl != kNoNode && shared(value)) facts(r.decl).contents.push_back(value);
}

void Guard::walk_list(const Node& n, bool keep, int owner) {
    const NodeId* list = ast_.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k) walk(list[k], keep, owner);
}

void Guard::walk(NodeId id, bool keep, int owner) {
    if (id == kNoNode) return;
    const Node& n = at(id);
    switch (n.kind) {
        case NodeKind::FnDecl: {
            int f = -1;
            for (std::size_t k = 0; k < plan_.functions.size(); ++k)
                if (plan_.functions[k] == id) f = static_cast<int>(k);
            const int task = (n.flags & kFlagAsync) ? 1 : 0;
            async_depth_ += task;
            walk(n.b, false, f);
            async_depth_ -= task;
            return;
        }
        case NodeKind::StructDecl: walk_list(n, false, owner); return;
        case NodeKind::Lambda: {
            const NodeId* params = ast_.list(n);
            const int task = (n.flags & kFlagAsync) ? 1 : 0;
            for (std::uint32_t k = 0; k < n.count; ++k) {
                facts(params[k]).param_fn = -2;
                facts(params[k]).isolated = task != 0;
            }
            async_depth_ += task;
            walk(n.b, !(n.flags & kFlagBlockBody), -3);  // an expression body is returned
            async_depth_ -= task;
            return;
        }
        case NodeKind::Declare: {
            walk(n.b, true, owner);
            if (at(n.a).kind == NodeKind::Ident) {
                Facts& f = facts(n.a);  // the inference keys a declaration by its target name node
                f.owner = owner;
                f.inflows.push_back(n.b);
            }
            walk(n.a, false, owner);
            return;
        }
        case NodeKind::Assign: case NodeKind::CompoundAssign: case NodeKind::IncDec: {
            const Node& target = at(n.a);
            const NodeId d = target.kind == NodeKind::Ident && n.kind == NodeKind::Assign ? decl(n.a, owner)
                                                                                            : kNoNode;
            if (d != kNoNode) idiom(n, d, owner);  // before the call is walked (its argument is no retention)
            if (n.kind == NodeKind::Assign) walk(n.b, true, owner);
            else walk(n.b, false, owner);
            if (target.kind == NodeKind::Ident) {
                if (n.kind == NodeKind::Assign) {
                    if (d != kNoNode) {
                        facts(d).inflows.push_back(n.b);
                    } else if (shared(n.b)) {
                        unknown_.push_back(n.a);
                    }
                }
                return;
            }
            if (target.kind == NodeKind::Index || target.kind == NodeKind::Member) {
                mutate(target.a, true, id, owner);
                if (n.kind == NodeKind::Assign) store_into(target.a, n.b, owner);
                walk(target.a, false, owner);
                walk(target.b, false, owner);
            }
            return;
        }
        case NodeKind::Return:
            if (n.a != kNoNode && owner >= 0) returns_[static_cast<std::size_t>(owner)].push_back(n.a);
            if (n.a != kNoNode && at(n.a).kind == NodeKind::Ident) {
                // `return p` of a parameter: the caller's own value goes back
                // to it; the call sites see it (returns_fresh, the escape).
                const NodeId d = decl(n.a, owner);
                if (d != kNoNode && facts(d).param_fn >= 0) {
                    facts(d).returned = true;
                    return;
                }
            }
            walk(n.a, true, owner);
            return;
        case NodeKind::For: {
            const Node& header = at(n.a);
            walk(header.a, false, owner);
            const Root r = shared(header.a) ? root_of(header.a, owner) : Root{};
            const NodeId* names = ast_.list(header);
            for (std::uint32_t k = 0; k < header.count; ++k) {
                Facts& f = facts(names[k]);
                f.loop = true;
                f.loop_root = r.known ? r.decl : kNoNode;
            }
            walk(n.b, false, owner);
            return;
        }
        case NodeKind::Ternary:
            walk(n.a, false, owner);
            walk(n.b, keep, owner);
            walk(n.c, keep, owner);
            return;
        case NodeKind::Coalesce:
            walk(n.a, keep, owner);
            walk(n.b, keep, owner);
            return;
        case NodeKind::Array: walk_list(n, true, owner); return;
        case NodeKind::Object: {
            const NodeId* items = ast_.list(n);
            for (std::uint32_t k = 0; k < n.count; ++k) {
                const Node& item = at(items[k]);
                walk(item.kind == NodeKind::Property ? item.b : items[k], true, owner);
            }
            return;
        }
        case NodeKind::Spread: case NodeKind::Await: walk(n.a, keep, owner); return;
        case NodeKind::Ident: case NodeKind::Index: case NodeKind::Member: case NodeKind::SafeIndex:
        case NodeKind::SafeMember:
            if (keep) retain(id, owner);
            if (n.kind == NodeKind::Ident) {
                const NodeId d = decl(id, owner);
                if (d != kNoNode && owner != facts(d).owner) free_reads_.push_back(d);
            } else {
                walk(n.a, false, owner);
                if (n.kind == NodeKind::Index || n.kind == NodeKind::SafeIndex) walk(n.b, false, owner);
            }
            return;
        case NodeKind::Call: call(id, keep, owner); return;
        default:
            walk(n.a, false, owner);
            walk(n.b, false, owner);
            walk(n.c, false, owner);
            walk_list(n, false, owner);
            return;
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

void Guard::call(NodeId id, bool keep, int owner) {
    const Node& n = at(id);
    const Node& callee = at(n.a);
    const NodeId* args = ast_.list(n);
    if (keep) retain(id, owner);  // an element-returning method of a binding's value
    const int f = user_function(n, owner);
    if (f >= 0) {
        calls_.push_back({id, f});
        if (callee.kind == NodeKind::Member) walk(callee.a, false, owner);
        // An async function's arguments are copied at task creation (both models).
        const bool task = (at(plan_.functions[static_cast<std::size_t>(f)]).flags & kFlagAsync) != 0;
        for (std::uint32_t k = 0; k < n.count; ++k)
            walk(args[k], !task && escapes(f, k) && !idiom_args_.count(args[k]), owner);
        return;
    }
    if (callee.kind == NodeKind::Ident) {
        const std::string_view name = ast_.text(callee);
        const bool value_call = kinds_.bindings.count(n.a) != 0;
        const bool isolate = name == "thread";
        const bool deep = name == "deepcopy";
        if (value_call) walk(n.a, false, owner);
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
            walk(args[k], value_call && !isolate, owner);
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
        walk(n.a, false, owner);
        walk_list(n, true, owner);
        return;
    }
    const std::string_view name = ast_.text(at(callee.b));
    const KindSet recv = kinds(callee.a);
    walk(callee.a, false, owner);
    if ((kinds(n.a) & kKindFunction) && !(recv & kKindArray)) {  // `o.f(args)`: a callable member, called
        for (std::uint32_t k = 0; k < n.count; ++k) walk(args[k], true, owner);
        return;
    }
    if (recv & kKindCollection) {  // map/set/stack stores keep their arguments (references)
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
        for (std::uint32_t k = 0; k < n.count; ++k) {
            walk(args[k], stores, owner);
            if (stores) store_into(receiver, args[k], owner);
        }
        return;
    }
    // A reading method: a result holding aggregates may hold the
    // receiver's parts (handled by retain above) or an argument's.
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
