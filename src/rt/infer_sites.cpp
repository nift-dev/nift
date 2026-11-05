#include "rt/infer_sites.h"

#include <algorithm>

// B4b (docs/runtime-2/checkpoints/b4b-element-types.md): element and member
// types of arrays and objects per allocation site. Contents are monotone
// unions over the whole program (flow-insensitive), merged by the stores of
// every alias, so a read describes what the slot may hold now.
// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt::infer_detail {

namespace {
constexpr KindSet kAggregate = kKindArray | kKindObject;

TyRef with_site(const TyRef& t, std::uint32_t site) {
    auto out = std::make_shared<Ty>(*t);
    if (!std::binary_search(out->sites.begin(), out->sites.end(), site))
        out->sites.insert(std::upper_bound(out->sites.begin(), out->sites.end(), site), site);
    return out;
}
} // namespace

bool Inference::has_site(const TyRef& t) const {
    for (std::uint32_t s : t->sites)
        if (s == kOpenSite || agg_sites_.count(s)) return true;
    return false;
}

std::uint32_t Inference::new_site() {
    const std::uint32_t site = next_site_++;
    agg_sites_.insert(site);
    return site;
}

std::uint32_t Inference::derived(std::uint32_t site) {
    const auto found = derived_.find(site);
    if (found != derived_.end()) return found->second;
    const std::uint32_t inner = new_site();
    derived_.emplace(site, inner);
    return inner;
}

TyRef Inference::fresh(NodeId site, const TyRef& t) {
    if (!t || t->any || !(t->kinds & kAggregate) || has_site(t)) return t;
    agg_sites_.insert(site);
    return with_site(t, site);
}

// The value read at a node: its sites' contents joined in; a value without a
// site here comes from a holder the inference does not track (an earlier
// session statement, a struct field initialiser, a native's part): anything.
TyRef Inference::sited(NodeId id, const TyRef& t) {
    if (!sites_ready_) {  // C4: the callee of every call (a direct call is not a use as a value)
        sites_ready_ = true;
        for (NodeId id = 0; id < ast_.size(); ++id)
            if (ast_.at(id).kind == NodeKind::Call) callee_nodes_.insert(ast_.at(id).a);
    }
    if (!t || t->any || !(t->kinds & kAggregate)) return t;
    // A persistent session's earlier values keep their recorded structure:
    // the session's shape contracts and coherent recompilation (CP19) keep
    // it true while the reference guard is on (b4b-element-types.md, U6).
    if (session_ && !has_site(t)) return t;
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 22
    return materialize(has_site(t) ? t : fresh(id, t));
#else
    (void)id;
    return materialize(has_site(t) ? t : with_site(t, kOpenSite));
#endif
}

TyRef Inference::materialize(const TyRef& t) {
    if (!t || t->any || !(t->kinds & kAggregate)) return t;
    std::vector<std::uint32_t> sites;
    bool open = agg_unknown_;
    for (std::uint32_t s : t->sites) {
        if (s == kOpenSite) open = true;
        else if (agg_sites_.count(s)) sites.push_back(s);
    }
    if (sites.empty() && !open) return t;
    auto out = std::make_shared<Ty>(*t);
    if (open) {
        out->elem = top_ty();
        out->fields.clear();
        out->tuple = false;
        out->items.clear();
        return out;
    }
    bool positional = true, changed = false;
    for (std::uint32_t s : sites) {
        if (nonpositional_.count(s)) positional = false;
        const auto c = agg_.find(s);
        if (c == agg_.end()) continue;
        if (c->second.values) out->elem = unite(out->elem, c->second.values), changed = true;
        for (const auto& [key, value] : c->second.named) {
            TyRef& field = out->fields[key];
            field = unite(field, value);
            changed = true;
        }
    }
    if (!positional && out->tuple) out->tuple = false, out->items.clear(), changed = true;
    // Parts the inference built without a site (a native's result inside a
    // fresh container, `group_by`'s arrays): one derived site per container
    // site, so every read of that part meets the same stores.
    const auto attach = [&](TyRef& part) {
        if (!part || part->any || !(part->kinds & kAggregate) || has_site(part)) return;
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 22
        return;  // A22: an inner part keeps no site (with sited() below: each read is a fresh container)
#endif
        TyRef next = part;
        for (std::uint32_t s : sites) next = with_site(next, derived(s));
        part = next;
        changed = true;
    };
    attach(out->elem);
    for (auto& entry : out->fields) attach(entry.second);
    for (auto& item : out->items) attach(item);
    return changed ? TyRef(out) : t;
}

// C5: a call into code compiled by an earlier session statement may store
// anything into any container this statement reaches.
TyRef Inference::session_effects(const TyRef& result) {
#if !(defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 21)
    if (result && !agg_unknown_) agg_unknown_ = true, site_grew();
#endif
    return result;
}

void Inference::site_grew() {
    self_changed_ = true;
    for (std::size_t u = 0; u <= plan_.functions.size() + lambdas_.size(); ++u) enqueue(u);
}

// A store into `container` (any alias of it): `value` joins the contents of
// each of its sites (under `key` for an object's literal key, else the union
// read by every key and position) and their positional facts are dropped.
// A container without a known site may be any container: every array and
// object may then hold anything (agg_unknown_).
void Inference::site_store(const TyRef& container, const TyRef& value, const std::string* key) {
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 2
    return;  // A2: a store reaches only the binding stored through (the stale per-binding type)
#endif
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 19
    if (current_ >= 0) return;  // A19: stores inside functions and lambdas do not reach the caller's sites
#endif
    if (!container) return;
    if (session_ && !container->any && !has_site(container)) return;  // an earlier statement's value: CP19 (U6)
    bool grew = false, tracked = false;
    const bool aggregate = container->any || (container->kinds & (kAggregate | kKindUnknown));
    if (!aggregate) return;
    for (std::uint32_t s : container->any ? std::vector<std::uint32_t>{} : container->sites) {
        if (s == kOpenSite || !agg_sites_.count(s)) continue;
        tracked = true;
        SiteContents& c = agg_[s];
        if (value && (value->kinds || value->any))
            grew = merge(key != nullptr && (container->kinds & kKindObject) ? c.named[*key] : c.values, value) || grew;
        grew = nonpositional_.insert(s).second || grew;
    }
    const bool open = container->any || std::find(container->sites.begin(), container->sites.end(), kOpenSite) !=
                                            container->sites.end();
    if ((open || !tracked) && !agg_unknown_) agg_unknown_ = grew = true;
    if (grew) site_grew();
}

const std::string* Inference::store_key(NodeId index, std::string& storage) const {
    const Node& k = ast_.at(index);
    if (k.kind != NodeKind::String || k.count != 0 || ast_.text(k).size() < 2) return nullptr;
    storage = std::string(ast_.text(k).substr(1, ast_.text(k).size() - 2));
    return &storage;
}

// A compound assignment or increment of an element or member: the new value
// (the operator's result kinds) is stored into the container's sites.
void Inference::element_update(NodeId target_id, const TyRef& value, Op op) {
    const Node& target = ast_.at(target_id);
    if (target.kind != NodeKind::Index && target.kind != NodeKind::Member) return;
    const TyRef old = expr(target_id);
    KindSet out = (old->kinds & kKindNumber) ? kKindInt : 0;
    if (value) {
        Op arithmetic = Op::Add;
        switch (op) {
            case Op::SubAssign: arithmetic = Op::Sub; break;
            case Op::MulAssign: arithmetic = Op::Mul; break;
            case Op::DivAssign: arithmetic = Op::Div; break;
            case Op::ModAssign: arithmetic = Op::Mod; break;
            default: break;
        }
        out = arithmetic_kinds(arithmetic, old->kinds, value->kinds);
        if (arithmetic == Op::Add && (old->kinds & kKindString) && (value->kinds & kKindString)) out |= kKindString;
    }
    std::string storage;
    const std::string* key = nullptr;
    if (target.kind == NodeKind::Index) {
        key = store_key(target.b, storage);
    } else {
        storage = std::string(ast_.text(ast_.at(target.b)));
        key = &storage;
    }
    site_store(expr(target.a), kinds_ty(out), key);
}

// C2: a method that may mutate, called on a receiver that is not exactly an
// array (a union, or unknown): its stored arguments join every site the
// receiver may carry (or every container, when it carries none).
void Inference::dynamic_mutation(const TyRef& recv, std::string_view name, const std::vector<TyRef>& values) {
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 24
    return;  // A24: a mutator on a receiver that is not exactly an array is not modelled
#endif
    if (!recv->any && !(recv->kinds & (kAggregate | kKindUnknown))) return;
    const auto arg = [&](std::size_t k) { return k < values.size() ? values[k] : empty_ty(); };
    bool mutator = false;
    TyRef stored = empty_ty();
    if (name == "set") mutator = true, stored = arg(1);  // rt-lint: allow-string-compare compile time
    if (name == "add") mutator = true, stored = arg(0);  // rt-lint: allow-string-compare compile time
    for (int m = 0; m < static_cast<int>(CollectionMethod::Map); ++m) {
        if (collection_names[m] != name) continue;
        switch (collection_effect(static_cast<CollectionMethod>(m))) {
            case SiteEffect::None: break;
            case SiteEffect::StoreArg0: mutator = true, stored = unite(stored, arg(0)); break;
            case SiteEffect::StoreArg1: mutator = true, stored = unite(stored, arg(1)); break;
            case SiteEffect::StoreSplice: mutator = true, stored = unite(stored, element_of(arg(2), nullptr, false));
                break;
            default: mutator = true; break;  // moves, removes or clears: positions only
        }
    }
    if (mutator) site_store(recv, stored, nullptr);
}

// C4: a function value passed to code the inference does not walk (a native
// that is not a collection method) may be called with anything: its
// parameters are anything.
void Inference::escape_callables(const std::vector<TyRef>& values) {
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 23
    return;  // A23: an escaping function keeps the parameter types of its known calls
#endif
    const std::size_t n = plan_.functions.size();
    for (const TyRef& v : values)
        for (std::uint32_t unit : v->callables) {
            if (!escaped_units_.insert(unit).second) continue;
            bool grew = false;
            if (unit < n) for (TyRef& p : params_[unit]) grew = merge(p, top_ty()) || grew;
            else if (unit > n && unit - n - 1 < lambdas_.size())
                for (TyRef& p : lambdas_[unit - n - 1].params) grew = merge(p, top_ty()) || grew;
            if (grew) enqueue(unit);
        }
}

// C4: a call whose targets the inference does not know: any function used as
// a value may be the callee, with any arguments.
void Inference::untracked_call() {
    auto as_value = std::make_shared<Ty>();
    for (std::size_t unit : value_units_) as_value->callables.push_back(static_cast<std::uint32_t>(unit));
    escape_callables({as_value});
}

} // namespace nift::rt::infer_detail
