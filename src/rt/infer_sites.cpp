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
constexpr std::size_t kMaxSites = 16;  // H2: classes a value carries before they are merged

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
    ++site_version_;
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
    if (agg_sites_.insert(site).second) ++site_version_;
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

TyRef Inference::materialize_now(const TyRef& t, std::vector<std::uint32_t>& read) {
    bool open = agg_unknown_;
    const std::vector<std::uint32_t> sites = classes(t->sites, open);
    for (std::uint32_t s : sites) {
        if (opened_.count(s)) open = true;
        read.push_back(s);
    }
    if (sites.empty() && !open) return t;
    auto out = std::make_shared<Ty>(*t);
    out->sites = with_classes(t->sites, sites);
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
    prune_parts(out, changed, read);
    return changed || out->sites != t->sites ? TyRef(out) : t;
}

// ---- H2: site classes (union-find), at most kMaxSites per value ----
// Merging two sites' contents loses precision only: every read of either sees
// the stores of both. A value carrying more than kMaxSites classes, or one
// collapsed class and others, is merged into one collapsed class, so types stay
// small and the classes stabilise (the approved design's "more than 16 sites").
std::uint32_t Inference::rep(std::uint32_t site) {
    auto found = parent_.find(site);
    if (found == parent_.end()) return site;
    const std::uint32_t root = rep(found->second);
    found->second = root;
    return root;
}

void Inference::unite_sites(std::uint32_t a, std::uint32_t b) {
    a = rep(a), b = rep(b);
    if (a == b) return;
    ++site_version_;
    if (b < a) std::swap(a, b);  // the smaller id is the root (stable)
    parent_[b] = a;
    ++result_.work.site_unions;
    // B4b 22 (review of 16f24f36): whether each side was opened *before* this union. Only a
    // class opened before it may skip its readers: they were walked again when it opened and
    // read anything since. A class this union opens (the root inheriting `b`'s flag) changes
    // every read of it, so the readers that read it while it was precise walk again.
    const bool a_was_open = opened_.count(a) != 0, b_was_open = opened_.count(b) != 0;
    bool grew = false, b_grew = false;  // whether each side's class changed: its readers re-walk then
    SiteContents moved, prior;
    if (const auto from = agg_.find(b); from != agg_.end()) moved = from->second, agg_.erase(from);
    if (const auto into = agg_.find(a); into != agg_.end()) prior = into->second;
    SiteContents& into = agg_[a];
    grew = merge(into.values, moved.values) || grew;
    for (const auto& [key, value] : moved.named) grew = merge(into.named[key], value) || grew;
    grew = merge(into.keys, moved.keys) || grew;
    b_grew = merge(moved.values, prior.values) || b_grew;
    for (const auto& [key, value] : prior.named) b_grew = merge(moved.named[key], value) || b_grew;
    b_grew = merge(moved.keys, prior.keys) || b_grew;
    for (auto* set : {&nonpositional_, &opened_})  // flags that change what a read sees
        if (set->count(a) && !set->count(b)) b_grew = true;
    if (b_was_open) b_grew = false;  // F1: `b` was opened before: its readers read anything already
    const auto flag = [&](std::unordered_set<std::uint32_t>& set) {
        if (set.count(b) && set.insert(a).second) grew = true;
    };
    flag(nonpositional_);
    flag(collapsed_);
    flag(exposed_);
    flag(opened_);
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 35
    (void)a_was_open;
    if (opened_.count(a)) grew = false;  // A35: also a class opened just now (16f24f36: its precise readers stay stale)
#else
    if (a_was_open) grew = false;  // F1: opened before this union: growing it changes no read
#endif
    const auto da = derived_.find(a), db = derived_.find(b);  // the inner parts' sites join too
    if (db != derived_.end()) {
        const std::uint32_t inner = db->second;
        derived_.erase(db);
        if (da != derived_.end()) unite_sites(da->second, inner);
        else derived_.emplace(a, inner);
    }
    // `b`'s readers now read `a`'s class: they re-walk if it holds more.
    const auto rb = readers_.find(b);
    if (rb != readers_.end()) {
        UnitSet readers = std::move(rb->second);
        readers_.erase(rb);
        if (b_grew) for (std::size_t u : readers.list) enqueue(u);
        // F1: the smaller list joins the larger (the root is the smaller id,
        // so a class grown site by site in decreasing order would otherwise
        // copy its whole list at every merge: quadratic in the sites).
        UnitSet& into_readers = readers_[a];
        if (into_readers.size() < readers.size()) std::swap(into_readers, readers);
        result_.work.reader_moves += readers.size();
        for (std::size_t u : readers.list) into_readers.insert(u);
    }
    if (grew) site_changed(a);
}

std::vector<std::uint32_t> Inference::classes(const std::vector<std::uint32_t>& sites, bool& open) {
    result_.work.site_items += sites.size();
    std::vector<std::uint32_t> out;
    bool collapsed = false;
    for (std::uint32_t s : sites) {
        if (s == kOpenSite) { open = true; continue; }
        if (!agg_sites_.count(s)) continue;
        const std::uint32_t r = rep(s);
        if (std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
        collapsed = collapsed || collapsed_.count(r);
    }
#if !(defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 26)
    if (out.size() > kMaxSites || (collapsed && out.size() > 1)) {
        for (std::size_t k = 1; k < out.size(); ++k) unite_sites(out[0], out[k]);
        const std::uint32_t r = rep(out[0]);
        collapsed_.insert(r);
        out.assign(1, r);
#if !(defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 31)
        // F1: too many known origins is analysis imprecision: the class's
        // contents read as anything from now on (a method on them is Dynamic,
        // under the B4a precondition, or refused). Monotone: a class never
        // leaves opened_, and every site merged into it later reads anything.
        if (opened_.insert(r).second) site_changed(r);
#endif
    }
#endif
    std::sort(out.begin(), out.end());
    return out;
}

TyRef Inference::canonical(const TyRef& t) {
    if (!t || t->any || t->sites.size() <= 1) return t;
    bool open = false;
    std::vector<std::uint32_t> sites = with_classes(t->sites, classes(t->sites, open));
    if (sites == t->sites) return t;
    auto out = std::make_shared<Ty>(*t);
    out->sites = std::move(sites);
    return out;
}

// A value's site list with its array/object sites replaced by their classes;
// map/set/stack sites (D-NEXT-refsem) and kOpenSite are kept as they are.
std::vector<std::uint32_t> Inference::with_classes(const std::vector<std::uint32_t>& raw,
                                                   const std::vector<std::uint32_t>& classes) const {
    std::vector<std::uint32_t> out = classes;
    for (std::uint32_t s : raw)
        if (s != kOpenSite && !agg_sites_.count(s)) out.push_back(s);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    if (std::find(raw.begin(), raw.end(), kOpenSite) != raw.end()) out.push_back(kOpenSite);
    return out;
}

void Inference::site_changed(std::uint32_t r) {
    ++site_version_;
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 27
    return;  // A27: a grown site's readers are not re-walked (a stale read survives the fixpoint)
#endif
    self_changed_ = true;
    const auto found = readers_.find(r);
    if (found == readers_.end()) return;
    for (std::size_t u : found->second.list) enqueue(u);
}

void Inference::site_grew() {
    ++site_version_;
    self_changed_ = true;
    for (std::size_t u = 0; u <= plan_.functions.size() + lambdas_.size(); ++u) enqueue(u);
}

// A store into `container` (any alias of it): `value` joins the contents of
// each of its sites (under `key` for an object's literal key, else the union
// read by every key and position) and their positional facts are dropped.
// A container without a known site may be any container: every array and
// object may then hold anything (agg_unknown_).
void Inference::site_store(const TyRef& container, const TyRef& value_in, const std::string* key) {
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 2
    return;  // A2: a store reaches only the binding stored through (the stale per-binding type)
#endif
#if defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 19
    if (current_ >= 0) return;  // A19: stores inside functions and lambdas do not reach the caller's sites
#endif
    if (!container) return;
    const TyRef value = canonical(value_in);
    // M1 (C5): a store into an earlier statement's value (it has no site here)
    // makes the stored containers reachable by earlier code.
    if (session_ && !container->any && !has_site(container)) {  // CP19 (U6) for the rest
#if !(defined(NIFT_B4B_MUTANT) && NIFT_B4B_MUTANT == 29)
        expose(value);
#endif
        return;
    }
    const bool aggregate = container->any || (container->kinds & (kAggregate | kKindUnknown));
    if (!aggregate) return;
    bool open = container->any, tracked = false;
    for (std::uint32_t s : container->any ? std::vector<std::uint32_t>{} : classes(container->sites, open)) {
        tracked = true;
        SiteContents& c = agg_[s];
        bool grew = false;
        if (value && (value->kinds || value->any)) {
            TyRef& slot = key != nullptr && (container->kinds & kKindObject) ? c.named[*key] : c.values;
            TyRef next = pruned(canonical(unite(slot, value)));  // H2: the contents' own sites as classes
            grew = !same(next, slot);
            if (grew) slot = std::move(next);
        }
        grew = nonpositional_.insert(s).second || grew;
        if (exposed_.count(s)) expose(value);  // M1: stored later into a container earlier code reaches
        // F1: the contents of an opened class are kept (exposure reaches them,
        // C5) but read by nobody: they read as anything, so nothing re-walks.
        if (grew && !opened_.count(s)) site_changed(s);
    }
    if ((open || !tracked) && !agg_unknown_) agg_unknown_ = true, site_grew();
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

} // namespace nift::rt::infer_detail
