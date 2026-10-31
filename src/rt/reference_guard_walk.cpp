#include "rt/reference_guard_walk.h"

#include "rt/builtin_bind.h"
#include "rt/collection.h"

#include <algorithm>

// rt-lint: cold-file compile-time Phase A forward-compatibility guard: the syntax walk, once per unit

namespace nift::rt::reference_guard_detail {

NodeId Guard::decl(NodeId ident, int owner) {
    const auto found = kinds_.bindings.find(ident);
    if (found != kinds_.bindings.end()) {
        // In an async body a binding declared outside it is the task's own
        // copy (made at task creation in both models): a distinct holder.
        NodeId d = found->second;
        const auto outer = facts_.find(d);
        const int declared_in = outer == facts_.end() ? -1 : outer->second.owner;
        if (async_depth_ != 0 && declared_in != owner) {
            const std::uint64_t key = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(owner)) << 32) | d;
            auto copy = isolated_.find(key);
            if (copy == isolated_.end())
                copy = isolated_.emplace(key, static_cast<NodeId>(ast_.size() + isolated_.size())).first;
            facts(copy->second).owner = owner;
            facts(copy->second).declared = true;
            d = copy->second;
        }
        std::vector<int>& seen = owners_[d];
        if (std::find(seen.begin(), seen.end(), owner) == seen.end()) seen.push_back(owner);
        return d;
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
            p.declared = true;
            p.isolated = (fn.flags & kFlagAsync) != 0;  // its arguments are copied at task creation
        }
    }
}

Guard::Root Guard::root_of(NodeId id, int owner) {
    const Node& n = at(id);
    switch (n.kind) {
        case NodeKind::Ident: {
            // A struct instance's state (`this`, an implicit field): no binding
            // tracks it, so a use that may share or mutate an aggregate there
            // is treated as unknown (D-NEXT-refsem #1).
            if (ast_.text(n) == "this") return {kNoNode, false, false, true};
            const NodeId d = decl(id, owner);
            if (d == kNoNode) return {kNoNode, true, false};
            if (!kinds_.bindings.count(id)) return {kNoNode, false, false, true};  // an implicit field
            const Facts& f = facts(d);
            if (f.loop) return {f.loop_root, false, f.loop_root != kNoNode};
            return {d, true, true};
        }
        case NodeKind::Index: case NodeKind::Member: case NodeKind::SafeIndex: case NodeKind::SafeMember: {
            if (kinds(n.a) & kKindStruct) return {kNoNode, false, false, true};
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
        if (at(id).kind == NodeKind::Ident || r.opaque) unknown_.push_back(id);
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
    // A store into a struct instance's own field mutates the instance (a
    // reference in both models), not an array or object.
    const Node& r0 = at(receiver);
    if ((r0.kind == NodeKind::Ident && ast_.text(r0) == "this") || (kinds(receiver) & kKindStruct)) return;
    const Root r = root_of(receiver, owner);
    if (!r.known) { unknown_mutations_.push_back(site); return; }
    if (r.decl == kNoNode) return;
    Facts& f = facts(r.decl);
    const bool top = r.whole && whole_receiver;
    (top ? f.mut_top : f.mut_nested) = true;
    if (f.mut_at == kNoNode) f.mut_at = site;
    f.mut_sites.push_back(site);
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
                Facts& p = facts(params[k]);
                p.param_fn = -2;
                p.isolated = task != 0;
                p.owner = lambda_owner(id);
                p.declared = true;
            }
            async_depth_ += task;
            walk(n.b, !(n.flags & kFlagBlockBody), lambda_owner(id));  // an expression body is returned
            async_depth_ -= task;
            return;
        }
        case NodeKind::Declare: {
            walk(n.b, true, owner);
            if (at(n.a).kind == NodeKind::Ident) {
                Facts& f = facts(n.a);  // the inference keys a declaration by its target name node
                f.owner = owner;
                f.declared = true;
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
            // The loop holds its iterable (a snapshot in the copy model, the
            // live value in Phase B) while the body runs: a lending.
            const Node& header = at(n.a);
            const Root r = shared(header.a) ? root_of(header.a, owner) : Root{};
            lend(header.a, id, owner);
            walk(header.a, false, owner);
            // Over an iterable no binding holds (a literal, a constructing
            // call: fresh at every depth) the loop binding holds an exclusive
            // part: an independent binding.
            const bool exclusive = r.decl == kNoNode && !r.opaque && fresh(header.a, true);
            const NodeId* names = ast_.list(header);
            for (std::uint32_t k = 0; k < header.count; ++k) {
                Facts& f = facts(names[k]);
                f.loop = !exclusive;
                f.loop_root = r.known ? r.decl : kNoNode;
                f.owner = owner;
                f.declared = true;
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
                if (d == kNoNode && !kinds_.bindings.count(id)) {  // a user function used as a value
                    if (const auto fn = plan_.index.find(ast_.text(n)); fn != plan_.index.end())
                        fn_value_[fn->second - 1u] = true;
                }
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

} // namespace nift::rt::reference_guard_detail
