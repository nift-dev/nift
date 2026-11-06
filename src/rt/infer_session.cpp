#include "rt/infer_session.h"
#include "rt/infer_fixpoint.h"
#include "rt/compiler.h"
#include "rt/session_shape.h"
#include "rt/collection.h"
#include <algorithm>
#include <map>
#include <set>

// rt-lint: cold-file incremental namespace facts in the shared kind inference
namespace nift::rt::infer_detail {
namespace {
// B5 (U6): the session heap's two pools: every kind an earlier array's elements, and every kind
// an earlier object's members, may hold, at any depth (an array is never the same object as an
// object, so the two never alias). An instance or callable there is of an earlier statement:
// struct -2 (any schema), callables historical; an open part makes both pools open.
struct HeapKinds { Ty arrays, objects; };
void heap_parts(const TyRef& t, HeapKinds& h, int depth = 0);
void heap_add(const TyRef& part, Ty& pool, HeapKinds& h, int depth) {
    if (!part) return;
    pool.kinds |= part->kinds;
    if (part->any) pool.any = true;
    // a callable keeps the targets this unit knows for it (a capture's current closures)
    pool.historical_callable = pool.historical_callable || part->historical_callable ||
        ((part->kinds & kKindFunction) && part->callables.empty() && !part->untracked_callable);
    pool.untracked_callable = pool.untracked_callable || part->untracked_callable;
    for (std::uint32_t unit : part->callables)
        if (std::find(pool.callables.begin(), pool.callables.end(), unit) == pool.callables.end())
            pool.callables.push_back(unit);
    heap_parts(part, h, depth + 1);
}
// `t` a value: its parts join the pool of its container kind.
void heap_parts(const TyRef& t, HeapKinds& h, int depth) {
    if (!t || depth > 64) return;
    if (t->any) { h.arrays.any = h.objects.any = true; return; }
    if (t->kinds & kKindArray) {
        heap_add(t->elem, h.arrays, h, depth);
        for (const auto& item : t->items) heap_add(item, h.arrays, h, depth);
    }
    if (t->kinds & kKindObject) {
        heap_add(t->elem, h.objects, h, depth);
        for (const auto& field : t->fields) heap_add(field.second, h.objects, h, depth);
    }
}
TyRef heap_type(const Ty& kinds, std::uint32_t arrays, std::uint32_t objects) {
    if (!kinds.kinds && !kinds.any) return {};
    auto out = std::make_shared<Ty>();
    out->kinds = kinds.kinds;
    out->any = kinds.any;
    if (out->kinds & kKindStruct) out->struct_id = -2;
    out->historical_callable = kinds.historical_callable;
    out->untracked_callable = kinds.untracked_callable;
    out->callables = kinds.callables;
    std::sort(out->callables.begin(), out->callables.end());
    if (out->kinds & kKindArray) out->sites.push_back(arrays);  // its parts: the heap again
    if (out->kinds & kKindObject) out->sites.push_back(objects);
    std::sort(out->sites.begin(), out->sites.end());
    return out;
}
} // namespace

TyRef Inference::session_expression_seed(NodeId id) {
    if (!session_) return {};
    const auto seed = session_->capture_seeds.find(id);
    if (seed == session_->capture_seeds.end()) return {};
    const auto callable = session_->capture_callable_seeds.find(id);
    return callable == session_->capture_callable_seeds.end() ? seed->second :
        session_type(seed->second, callable->second);
}
TyRef Inference::constant(NodeId id, double v) { return kinds_ty(constants_.set(ast_.size(), id, v)); }

void Inference::seed_session() {
    // A script unit's invocation bindings: the session's symbols, unchanged.
    if (!session_ && invocation_) for (const auto& entry : *invocation_) {
        if (entry.shadow != kNoNode) continue;  // the entry's own top-level binding replaces it
        external_.emplace(entry.symbol.name, vars_.size());
        // B4b: an invocation binding (`args`) is one allocation site of this unit.
        vars_.push_back(fresh(new_site(), entry.symbol.structure ? entry.symbol.structure :
                                                                   kinds_ty(entry.symbol.kinds)));
    }
    HeapKinds heap;  // B5 (U6): the session heap's initial contents
    if (session_) for (const auto& symbol : session_->symbols) {
        external_.emplace(symbol.name, vars_.size());
        const auto declaration = session_->declarations.find(symbol.name);
        if (declaration != session_->declarations.end()) binding_decls_[vars_.size()] = declaration->second;
        TyRef type = session_type(symbol.structure ? symbol.structure : kinds_ty(symbol.kinds), symbol.callable_shape);
        const auto lambda = session_->lambda_templates.find(symbol.name);
        const auto target = symbol.definition ? plan_.index.find(symbol.definition->name) : plan_.index.end();
        const auto schema = plan_.struct_index.find(symbol.struct_name);
        if (type->kinds != symbol.kinds || lambda != session_->lambda_templates.end() ||
            target != plan_.index.end() || schema != plan_.struct_index.end()) {
            auto adapted = std::make_shared<Ty>(*type);
            adapted->kinds = symbol.kinds;
            if (schema != plan_.struct_index.end()) adapted->struct_id = static_cast<int>(schema->second);
            if (lambda != session_->lambda_templates.end())
                adapted->callables.push_back(static_cast<std::uint32_t>(session_lambda_reference(lambda->second)));
            else if (target != plan_.index.end()) adapted->callables.push_back(target->second - 1u);
            std::sort(adapted->callables.begin(), adapted->callables.end());
            adapted->callables.erase(std::unique(adapted->callables.begin(), adapted->callables.end()),
                                    adapted->callables.end());
            type = std::move(adapted);
        }
        // B5 (U6): an earlier statement's value. Private (proved by the last statement): its own
        // site class, with its contents then; else the session heap (siteless here, sited()).
#if !(defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 4)
        if (symbol.heap_contents && (type->kinds & (kKindArray | kKindObject)) && !type->any) {
            auto own = std::make_shared<Ty>(*type);
            own->elem = symbol.heap_contents->elem;
            own->items = symbol.heap_contents->items;
            own->tuple = symbol.heap_contents->tuple;
            own->fields = symbol.heap_contents->fields;
            own->sites.assign(1, new_site());
            type = std::move(own);
        } else if (symbol.structure && !(symbol.kinds & kKindUnknown))  // Unknown: every use is refused
            heap_parts(symbol.structure, heap);
#else
        if (symbol.structure) heap_parts(symbol.structure, heap);  // B5-U4: every earlier value private
        if (symbol.heap_contents || (type->kinds & (kKindArray | kKindObject))) {
            auto own = std::make_shared<Ty>(*type); own->sites.assign(1, new_site()); type = std::move(own);
        }
#endif
        vars_.push_back(type);
        if (session_locations_) {
            const auto location = session_->global_locations.find(symbol.name);
            if (location != session_->global_locations.end())
                session_location_slot(location->second.key,vars_.size()-1);
        }
    }
    if (!session_) return;
    // B5 (U6): one site class for every earlier value, at every depth. Its contents: the published
    // heap, every recorded structure's parts and every seed of earlier code (captures, parameters,
    // call results), so a read of any earlier value's part sees what any of them may hold.
    heap_site_ = new_site();
    heap_objects_site_ = new_site();
    for (std::uint32_t s : {heap_site_, heap_objects_site_}) {
        nonpositional_.insert(s);
        exposed_.insert(s);  // earlier (not recompiled) code reaches every earlier value (M1, C5)
    }
    if (session_->heap) heap_add(session_->heap, heap.arrays, heap, 0);
    if (session_->heap_objects) heap_add(session_->heap_objects, heap.objects, heap, 0);
    for (const auto& seed : session_->capture_seeds) heap_parts(seed.second, heap);
    for (const auto& seed : session_->parameter_seeds) heap_parts(seed.second, heap);
    for (const auto& seed : session_->call_result_seeds) heap_parts(seed.second, heap);
    if (TyRef contents = heap_type(heap.arrays, heap_site_, heap_objects_site_))
        agg_[heap_site_].values = std::move(contents);
    if (TyRef contents = heap_type(heap.objects, heap_site_, heap_objects_site_))
        agg_[heap_objects_site_].values = std::move(contents);
}
// B5 (U6): an earlier statement's value without a site here: in the heap pool of each container kind
// it may be (an unknown container kind: the arrays' pool).
TyRef Inference::heap_sited(const TyRef& t) {
    auto out = std::make_shared<Ty>(*t);
    if (t->kinds & kKindArray) out->sites.push_back(heap_site_);
    if (t->kinds & kKindObject) out->sites.push_back(heap_objects_site_);
    if (!(t->kinds & (kKindArray | kKindObject))) out->sites.push_back(heap_site_);
    std::sort(out->sites.begin(), out->sites.end());
    out->sites.erase(std::unique(out->sites.begin(), out->sites.end()), out->sites.end());
    return out;
}

// B5 (U6): a value as the next statement records it: every part materialized from its sites
// (stores through every alias joined in), no site numbers (they mean nothing in another unit).
TyRef Inference::session_export(const TyRef& t, int depth) {
    if (!t) return t;
    if (depth > 8) return top_ty();
    const TyRef m = (t->kinds & (kKindArray | kKindObject)) && !t->any ? sited(kNoNode, t) : t;
    auto out = std::make_shared<Ty>(*m);
    out->sites.clear();
    out->elem = session_export(m->elem, depth + 1);
    for (auto& item : out->items) item = session_export(item, depth + 1);
    for (auto& field : out->fields) field.second = session_export(field.second, depth + 1);
    return out;
}

void Inference::session_reach(const TyRef& t_in, int depth, std::map<std::uint32_t, int>& at, bool& shared) {
    if (!t_in) return;
    if (depth > 16 || t_in->any) { shared = true; return; }
    if (t_in->kinds & (kKindFunction | kKindStruct | kKindCollection | kKindUnknown)) shared = true;
    if (!(t_in->kinds & (kKindArray | kKindObject))) return;
    const TyRef t = sited(kNoNode, t_in);
    bool open = false;
    for (std::uint32_t s : t->sites) if (s == kOpenSite) open = true;
    for (std::uint32_t c : classes(t->sites, open)) {
        if (opened_.count(c)) open = true;
        const auto seen = at.find(c);
        if (seen != at.end()) {
            if (seen->second != depth) shared = true;  // one class at two depths
            continue;
        }
        at.emplace(c, depth);
    }
    if (open) shared = true;
    session_reach(t->elem, depth + 1, at, shared);
    for (const auto& item : t->items) session_reach(item, depth + 1, at, shared);
    for (const auto& field : t->fields) session_reach(field.second, depth + 1, at, shared);
}

void Inference::export_session() {
    if (!session_) return;
#if defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 3
    const auto shape = [](const TyRef& t) { return t; };  // B5-U3: the binding's type as written (B4b)
#else
    const auto shape = [this](const TyRef& t) { return session_export(t, 0); };
#endif
    for (const auto& binding : binding_decls_)
        result_.session_decl_shapes[binding.second] = shape(vars_[binding.first]);
    if (session_->coherent_recompile) for (const auto& binding : external_)
        result_.binding_shapes[std::string(binding.first)] = shape(vars_[binding.second]);
    for (const auto& binding : top_level_slots_)
        result_.binding_shapes[std::string(binding.first)] = shape(vars_[binding.second]);
    // B5 (U6): which bindings are private. A binding's value is private when every site class it
    // reaches (at every depth) is reached by no other binding, by no slot that outlives the
    // statement other than a binding (a lambda's captures, a struct field) and by no earlier
    // value (the heap), each class at one depth only, and nothing in it is open, a callable, an
    // instance or a collection (whose holders the analysis does not bound).
    std::map<std::string, std::size_t> names;
    for (const auto& binding : external_) names[std::string(binding.first)] = binding.second;
    for (const auto& binding : top_level_slots_) names[std::string(binding.first)] = binding.second;
    std::set<std::size_t> top;
    for (const auto& name : names) top.insert(name.second);
    std::map<std::uint32_t, int> hidden;
    bool everything = agg_unknown_;
    {
        bool open = false;
        std::vector<std::uint32_t> heap_classes = classes({heap_site_, heap_objects_site_}, open);
        for (std::uint32_t s : heap_classes) hidden.emplace(s, 0);
        std::set<std::size_t> holders;
        for (const auto& lambda : lambdas_)
            for (const auto& scope : lambda.scopes)
                for (const auto& entry : scope) if (!top.count(entry.second)) holders.insert(entry.second);
        for (const StructInfo& s : plan_.structs)
            for (const StructFieldInfo& f : s.fields) holders.insert(field_slot(f));
        for (std::size_t slot : holders) {
            bool ignored = false;
            std::map<std::uint32_t, int> at;
            session_reach(vars_[slot], 0, at, ignored);
            for (const auto& entry : at) hidden.emplace(entry.first, 0);
        }
        everything = everything || open;
    }
    std::map<std::string, std::map<std::uint32_t, int>> reached;
    std::map<std::uint32_t, int> count;
    std::set<std::string> unbounded;
    for (const auto& name : names) {
        bool shared = false;
        auto& at = reached[name.first];
        session_reach(vars_[name.second], 0, at, shared);
        if (shared) unbounded.insert(name.first);
        for (const auto& entry : at) ++count[entry.first];
    }
    if (!everything)
        for (const auto& name : names) {
            const TyRef& type = vars_[name.second];
            if (!type || type->any || !(type->kinds & (kKindArray | kKindObject)) || unbounded.count(name.first))
                continue;
            bool alone = true;
            for (const auto& entry : reached[name.first])
                if (count[entry.first] > 1 || hidden.count(entry.first)) alone = false;
            if (alone) result_.session_private[name.first] = session_export(type, 0);
        }
    // B5 (U6): the heap's contents after this unit, with every part's own sites read.
    HeapKinds heap;
    bool open = false;
    for (std::uint32_t root : {heap_site_, heap_objects_site_}) {
        Ty& pool = root == heap_site_ ? heap.arrays : heap.objects;
        for (std::uint32_t s : classes({root}, open)) {
            if (opened_.count(s)) open = true;
            const auto c = agg_.find(s);
            if (c == agg_.end()) continue;
            heap_add(session_export(c->second.values, 1), pool, heap, 0);
            for (const auto& entry : c->second.named) heap_add(session_export(entry.second, 1), pool, heap, 0);
        }
    }
    if (open || agg_unknown_) heap.arrays.any = heap.objects.any = true;
    for (const auto& name : names)  // every value that stays in the heap (or joins it now)
        if (!result_.session_private.count(name.first)) heap_parts(session_export(vars_[name.second], 0), heap);
    const auto plain = [](const Ty& pool) -> TyRef {
        TyRef t = heap_type(pool, kOpenSite, kOpenSite);
        if (!t) return t;
        auto out = std::make_shared<Ty>(*t);
        out->sites.clear();
        if (!out->callables.empty()) out->callables.clear(), out->historical_callable = true;
        return out;
    };
    result_.session_heap = plain(heap.arrays);
    result_.session_heap_objects = plain(heap.objects);
}
TyRef Inference::session_bottom_arguments(const Node& node) {
    for (std::uint32_t k=0;k<node.count;++k) expr(ast_.list(node)[k]);
    return empty_ty();
}
void Inference::session_call_edge(std::size_t target, std::size_t arity, std::size_t arguments) {
    if (result_.session_proof && arity == arguments)
        result_.session_proof->call_edges[current_unit()].insert(target);
}
TyRef Inference::session_unknown_call(const Node& node, const TyRef& type) {
#if !defined(NIFT_SESSION_ADMISSION_MUTANT) || NIFT_SESSION_ADMISSION_MUTANT != 25
    const auto& callee = ast_.at(node.a);
    if (session_ && type->callables.empty() && (type->any || (type->kinds & kKindUnknown)) &&
        (callee.kind != NodeKind::Ident || find(ast_.text(callee)))) {
        auto unknown = std::make_shared<Ty>(*type); unknown->untracked_callable = true;
        return session_historical_call(node, unknown);
    }
#else
    (void)node; (void)type;
#endif
    return {};
}
TyRef Inference::session_historical_call(const Node& node, const TyRef& callee) {
    if (!session_) return {};
    const auto key = (std::uint64_t(node.begin) << 32) | node.end;
    if (result_.session_proof) {
        result_.session_proof->historical_calls.erase(key); result_.session_proof->historical_owners.erase(key);
        result_.session_proof->unproved_current_calls.erase(key);
    }
    if (callee->untracked_callable) {
        if (!result_.session_proof) result_.session_proof = std::make_shared<SessionInferenceProof>();
        result_.session_proof->unproved_current_calls.insert(key);
        return any_ty();
    }
    if (!callee->historical_callable || !callee->callables.empty() || callee->any ||
        (callee->kinds & kKindUnknown)) return {};
    if (session_->opaque_call_seeds.count(key)) {
        result_.session_proof->historical_calls.insert(key);
        result_.session_proof->historical_owners.emplace(key, current_unit());
        return any_ty();
    }
    const auto fact = session_->call_result_seeds.find(key);
    if (fact == session_->call_result_seeds.end() || !session_proved_shape(fact->second)) {
        if (!result_.session_proof) result_.session_proof = std::make_shared<SessionInferenceProof>();
        result_.session_proof->unproved_current_calls.insert(key);
        return any_ty();
    }
    result_.session_proof->historical_calls.insert(key);
    result_.session_proof->historical_owners.emplace(key, current_unit());
    return session_historical_shape(fact->second);
}
TyRef Inference::session_collection_call(const Node& node, const std::vector<TyRef>& values, int id) {
    const auto method = static_cast<CollectionMethod>(id);
    if ((method < CollectionMethod::Map && method != CollectionMethod::Sort) || values.empty()) return {};
    auto proof = std::make_shared<Ty>(); proof->kinds = kKindFunction;
    for (std::size_t k = 0; k < values.size(); k += method == CollectionMethod::SortBy ? 2 : values.size()) {
        proof->historical_callable = proof->historical_callable ||
            (values[k]->historical_callable && values[k]->callables.empty());
        proof->untracked_callable = proof->untracked_callable || values[k]->untracked_callable;
    }
    return session_historical_call(node, proof);
}
TyRef Inference::session_call(const Node& n) {
    const Node& callee = ast_.at(n.a);
    if (!session_ || callee.kind != NodeKind::Ident) return {};
    for (const auto& symbol : session_->symbols) if (symbol.name == ast_.text(callee) && symbol.callable) {
        if (session_->lambda_templates.count(symbol.name) ||
            (symbol.definition && plan_.index.count(symbol.definition->name))) return {};
        expr(n.a);
        for (std::uint32_t k = 0; k < n.count; ++k) expr(ast_.list(n)[k]);
        if (!result_.session_proof) result_.session_proof = std::make_shared<SessionInferenceProof>();
        result_.session_proof->unproved_current_calls.insert((std::uint64_t(n.begin) << 32) | n.end);
        return any_ty();
    }
    return {};
}
} // namespace nift::rt::infer_detail

namespace nift::rt {
UnitKinds infer_unit_kinds(const Ast& ast, NodeId root, const UnitPlan& plan, std::size_t max_walks,
                           const GlobalPlan* globals, const SessionCompileContext* session,
                           const std::vector<InvocationBinding>* invocation) {
#ifdef NIFT_B4B_FIXPOINT_GATE
    // B4b 27 (tests/runtime2/b4b/fixpoint.py): the same analysis, then the fixed-point check of
    // every converged result; a non-fixed point fails the compile (never repaired).
    infer_detail::Inference inference(ast, root, plan, globals, session, invocation);
    inference.verify_fixed_point();
    UnitKinds out = inference.run(max_walks);
    if (out.converged && (!inference.stale().empty() || !inference.state_changed().empty()))
        fixpoint_gate_fail(ast, inference.stale(), inference.state_changed(), session != nullptr);
    return out;
#else
    return infer_detail::Inference(ast, root, plan, globals, session, invocation).run(max_walks);
#endif
}

} // namespace nift::rt
