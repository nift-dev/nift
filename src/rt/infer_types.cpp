#include "rt/infer_types.h"

#include <algorithm>
#include <cmath>

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt::infer_detail {

const TyRef& empty_ty() {
    static const TyRef e = std::make_shared<const Ty>();
    return e;
}

// Every kind at every depth ("top"), e.g. beyond the depth cap.
const TyRef& top_ty() {
    static const TyRef a = [] {
        auto t = std::make_shared<Ty>();
        t->kinds = kKindAll | kKindTagged;  // D-063, D-NEXT-refsem: a collection or instance may be anywhere
        t->any = true;
        return TyRef(t);
    }();
    return a;
}

// The value of something the compiler refuses anyway (an unresolved name,
// an unsupported expression): kKindUnknown makes every check defer to that
// refusal instead of reporting a type reason first.
const TyRef& any_ty() {
    static const TyRef a = [] {
        auto t = std::make_shared<Ty>();
        t->kinds = kKindAll | kKindUnknown;
        t->any = true;
        return TyRef(t);
    }();
    return a;
}

TyRef kinds_ty(KindSet kinds) {
    auto t = std::make_shared<Ty>();
    t->kinds = kinds;
    return t;
}

namespace {
constexpr int kMaxDepth = 6;
} // namespace

TyRef collection_ty(KindSet kinds, std::uint32_t site) {
    auto t = std::make_shared<Ty>();
    t->kinds = kinds;
    t->sites.push_back(site);
    return t;
}

TyRef with_text_number(const TyRef& value,bool maybe) {
    if(!maybe||value->legacy_text_number)return value;
    auto next=std::make_shared<Ty>(*value);next->legacy_text_number=true;return next;
}

namespace {
// F1: whether unite(a, b, depth) would build a type equal to `a` (same()):
// then unite returns `a` itself and allocates nothing. Every read of a
// container joins its sites' contents again (Inference::materialize), and
// after the first walks they are mostly included already.
bool covers(const TyRef& a, const TyRef& b, int depth) {
    if (!b || b.get() == empty_ty().get() || a.get() == b.get()) return true;
    if (!a || a.get() == empty_ty().get()) return false;
    if (a->any || b->any || depth > kMaxDepth) return a.get() == unite(a, b, depth).get();  // B4b 27
    if ((a->kinds | b->kinds) != a->kinds) return false;
    if ((b->historical_callable && !a->historical_callable) || (b->untracked_callable && !a->untracked_callable) ||
        (b->legacy_text_number && !a->legacy_text_number))
        return false;
    if (a->struct_id != b->struct_id && a->struct_id != -2) return false;
    for (std::uint32_t site : b->sites)
        if (!std::binary_search(a->sites.begin(), a->sites.end(), site)) return false;
    constexpr KindSet tracked = kKindArray | kKindObject | kKindCollection;
    if ((((a->kinds & tracked) && a->sites.empty() && !b->sites.empty()) ||
         ((b->kinds & tracked) && b->sites.empty() && !a->sites.empty())) &&
        (a->sites.empty() || a->sites.back() != kOpenSite))
        return false;
    for (std::uint32_t u : b->callables)
        if (std::find(a->callables.begin(), a->callables.end(), u) == a->callables.end()) return false;
    if (!covers(a->elem, b->elem, depth + 1) || !covers(a->key, b->key, depth + 1)) return false;
    if (a->tuple) {
        if (!b->tuple || a->items.size() != b->items.size()) return false;
        for (std::size_t k = 0; k < a->items.size(); ++k)
            if (!covers(a->items[k], b->items[k], depth + 1)) return false;
    } else if (!a->items.empty()) {
        return false;
    }
    for (const auto& [key, ty] : b->fields) {
        const auto it = a->fields.find(key);
        if (it == a->fields.end() || !covers(it->second, ty, depth + 1)) return false;
    }
    return true;
}
} // namespace

thread_local std::size_t* unite_work = nullptr;

// B4b 27 (review of 4c9632b8, L-1): the join when the result is open (`any`: its kinds at the top,
// every kind at every depth below): an open operand, an unknown one (`base` any_ty), or beyond
// the depth cap (`base` top_ty). The result has the top-level kinds and the flags of both
// operands (and `base`'s kinds), so it is an upper bound of both. It used to be any_ty or
// top_ty themselves, which are not: an open object joined with another became top (every kind
// at the top), so `e := env()` widened from object to everything on its second walk (the five
// `presentation_env_*` non-fixed points), and a moved-from number (legacy_text_number) or a
// collection joined with an unknown value lost the flag or the kind, then got it back from the
// next walk's operand (a binding or return that changed on every walk, never a fixed point;
// e.g. `return x.nope()`). Admission is unchanged by the flags of an unknown or top value (every
// check refuses those, or treats them as anything). An operand that covers the result is
// returned itself (no growth: the fixed point of a re-walk), and `base` when it is the result.
TyRef open_union(const TyRef& a, const TyRef& b, const TyRef& base) {
    const KindSet kinds = a->kinds | b->kinds | (base ? base->kinds : 0);
    const bool historical = a->historical_callable || b->historical_callable;
    const bool untracked = a->untracked_callable || b->untracked_callable;
    const bool text = a->legacy_text_number || b->legacy_text_number;
    const auto is = [&](const TyRef& t) {
        return t->any && t->kinds == kinds && t->historical_callable == historical &&
               t->untracked_callable == untracked && t->legacy_text_number == text;
    };
    if (base && is(base)) return base;
    if (is(top_ty())) return top_ty();
    if (is(a)) return a;
    if (is(b)) return b;
    auto t = std::make_shared<Ty>();
    t->kinds = kinds;
    t->any = true;
    t->historical_callable = historical;
    t->untracked_callable = untracked;
    t->legacy_text_number = text;
    return t;
}

TyRef unite(const TyRef& a, const TyRef& b, int depth) {
    if (!a || a.get() == empty_ty().get()) return b ? b : empty_ty();
    if (!b || b.get() == empty_ty().get() || a.get() == b.get()) return a;
    if (a->any || b->any || depth > kMaxDepth) {  // B4b 27: the join (open_union)
        if ((a->kinds | b->kinds) & kKindUnknown) return open_union(a, b, any_ty());
        if (depth > kMaxDepth) return open_union(a, b, top_ty());
        return open_union(a, b, nullptr);
    }
    if (unite_work)  // B4b 23: one type plus the entries of both operands
        *unite_work += 1 + a->sites.size() + b->sites.size() + a->fields.size() + b->fields.size() +
                       a->items.size() + b->items.size() + a->callables.size() + b->callables.size();
    if (covers(a, b, depth)) return a;
    auto t = std::make_shared<Ty>();
    t->kinds = a->kinds | b->kinds;
    t->historical_callable = a->historical_callable || b->historical_callable;
    t->untracked_callable = a->untracked_callable || b->untracked_callable;
    t->legacy_text_number=a->legacy_text_number||b->legacy_text_number;
    t->struct_id = a->struct_id == b->struct_id ? a->struct_id : -2;
    t->sites = a->sites;  // D-NEXT-refsem: sorted union
    for (std::uint32_t site : b->sites)
        if (!std::binary_search(t->sites.begin(), t->sites.end(), site))
            t->sites.insert(std::upper_bound(t->sites.begin(), t->sites.end(), site), site);
    // B4b: a container without any site joined with one that has sites stays
    // of unknown origin (never "only the other's sites").
    constexpr KindSet tracked = kKindArray | kKindObject | kKindCollection;
    if (((a->kinds & tracked) && a->sites.empty() && !b->sites.empty()) ||
        ((b->kinds & tracked) && b->sites.empty() && !a->sites.empty()))
        if (t->sites.empty() || t->sites.back() != kOpenSite) t->sites.push_back(kOpenSite);
    if (a->elem || b->elem) t->elem = unite(a->elem, b->elem, depth + 1);
    if (a->key || b->key) t->key = unite(a->key, b->key, depth + 1);
    if (a->tuple && b->tuple && a->items.size() == b->items.size()) {
        t->tuple = true;
        for (std::size_t k = 0; k < a->items.size(); ++k)
            t->items.push_back(unite(a->items[k], b->items[k], depth + 1));
    }
    t->callables = a->callables;
    for (std::uint32_t u : b->callables)
        if (std::find(t->callables.begin(), t->callables.end(), u) == t->callables.end()) t->callables.push_back(u);
    t->fields = a->fields;
    for (const auto& [key, ty] : b->fields) {
        auto it = t->fields.find(key);
        if (it == t->fields.end()) t->fields.emplace(key, ty);
        else it->second = unite(it->second, ty, depth + 1);
    }
    return t;
}

bool same(const TyRef& a, const TyRef& b) {
    if (a.get() == b.get()) return true;
    if(a&&b&&a->legacy_text_number!=b->legacy_text_number)return false;
    const bool a_empty = !a || (a->kinds == 0 && !a->elem && !a->key && a->fields.empty() && !a->any);
    const bool b_empty = !b || (b->kinds == 0 && !b->elem && !b->key && b->fields.empty() && !b->any);
    if (a_empty || b_empty) return a_empty == b_empty;
    if (a->historical_callable != b->historical_callable ||
        a->untracked_callable != b->untracked_callable) return false;
    if (a->kinds != b->kinds || a->any != b->any || a->fields.size() != b->fields.size()) return false;
    if (a->struct_id != b->struct_id || a->callables != b->callables || a->sites != b->sites) return false;
    if (a->tuple != b->tuple || a->items.size() != b->items.size()) return false;
    for (std::size_t k = 0; k < a->items.size(); ++k)
        if (!same(a->items[k], b->items[k])) return false;
    if (!same(a->elem, b->elem)) return false;
    if (!same(a->key, b->key)) return false;
    for (const auto& [key, ty] : a->fields) {
        auto it = b->fields.find(key);
        if (it == b->fields.end() || !same(ty, it->second)) return false;
    }
    return true;
}

// The type of `value[key]` (key: a literal string, or null when unknown;
// position: an integer literal index, or -1).
TyRef element_of(const TyRef& t, const std::string* key, bool by_member, long position) {
    if (!t) return empty_ty();
    if (t->any) return (t->kinds & kKindUnknown) ? any_ty() : top_ty();
    TyRef out = empty_ty();
    if (!by_member && (t->kinds & kKindArray) && t->tuple && position >= 0) {
        const auto at = static_cast<std::size_t>(position);
        if (at < t->items.size()) out = unite(out, t->items[at]);
    } else if (!by_member && (t->kinds & kKindArray) && t->elem) {
        out = unite(out, t->elem);
    }
    // D-063, D-NEXT-refsem: a collection's contents are shared by every alias
    // and are tracked per collection kind by the inference (Inference::
    // contents), never in the value's own type: here, anything.
    if (!by_member && (t->kinds & kKindCollection) && !(t->kinds & kKindArray)) out = unite(out, top_ty());
    if (t->kinds & kKindObject) {
        if(t->elem)out=unite(out,t->elem);
        if (key) {
            auto it = t->fields.find(*key);
            if (it != t->fields.end()) out = unite(out, it->second);
        } else {
            for (const auto& entry : t->fields) out = unite(out, entry.second);
        }
    }
    return out;
}

namespace {
bool if_falls(const Ast& ast, NodeId id) {
    const Node& n = ast.at(id);
    if (n.c == kNoNode) return true;
    const Node& other = ast.at(n.c);
    return block_falls_through(ast, n.b) || (other.kind == NodeKind::If ? if_falls(ast, n.c)
                                                                         : block_falls_through(ast, n.c));
}
} // namespace

bool block_falls_through(const Ast& ast, NodeId block) {
    const Node& b = ast.at(block);
    if (b.count == 0) return true;
    const Node& last = ast.at(ast.list(b)[b.count - 1]);
    if (last.kind == NodeKind::Return || last.kind == NodeKind::Throw) return false;
    if (last.kind == NodeKind::If) return if_falls(ast, ast.list(b)[b.count - 1]);
    return true;
}

long literal_position(std::string_view digits) {
    if (digits.empty() || digits.size() >= 9 || digits.find_first_not_of("0123456789") != std::string_view::npos)
        return -1;
    return std::stol(std::string(digits));
}

KindSet Constants::set(std::size_t nodes, NodeId id, double v) {
    if (values.empty()) values.assign(nodes, std::nan(""));
    values[id] = v;
    return std::trunc(v) == v ? kKindInt : kKindFrac;
}

bool Constants::has(NodeId id) const { return !values.empty() && !std::isnan(values[id]); }

bool fold_constant(Op op, double l, double r, double& out) {
    switch (op) {
        case Op::Add: out = l + r; break;
        case Op::Sub: out = l - r; break;
        case Op::Mul: out = l * r; break;
        case Op::Div: if (r == 0) return false; out = l / r; break;
        case Op::Mod:
            if (r == 0 || std::trunc(l) != l || std::trunc(r) != r) return false;
            out = std::fmod(l, r);
            break;
        default: return false;
    }
    return std::isfinite(out);
}

KindSet arithmetic_kinds(Op op, KindSet l, KindSet r) {
    KindSet out = 0;
    if (op == Op::Add && l == kKindArray && r == kKindArray) out |= kKindArray;
    if (op == Op::Add && ((l & kKindString) || (r & kKindString))) out |= kKindString;
    if ((l & kKindNumber) && (r & kKindNumber)) {
        const bool frac = (l & kKindFrac) || (r & kKindFrac);
        // A fraction plus or minus an integer, or divided by one, stays a
        // fraction (review round 2: precise binding types for
        // context:assignment-error; magnitudes below 2^52, where doubles hold
        // fractions at all).
        const bool frac_int = (l & kKindNumber) == kKindFrac && (r & kKindNumber) == kKindInt;
        const bool int_frac = (l & kKindNumber) == kKindInt && (r & kKindNumber) == kKindFrac;
        switch (op) {
            case Op::Div: out |= frac_int ? kKindFrac : kKindNumber; break;
            case Op::Mod: out |= kKindInt; break;
            case Op::Add: case Op::Sub:
                out |= (frac_int || int_frac) ? kKindFrac : frac ? kKindNumber : kKindInt;
                break;
            default: out |= frac ? kKindNumber : kKindInt; break;
        }
    }
    return static_cast<KindSet>(out | ((l | r) & kKindUnknown));
}

TyRef async_result(std::uint32_t flags, const TyRef& returns) {
    if (!(flags & kFlagAsync)) return returns;
    auto future = std::make_shared<Ty>();
    future->kinds = kKindFuture;
    future->elem = returns ? returns : empty_ty();
    return future;
}

TyRef awaited(const TyRef& t) {
    if (!t || t->any || (t->kinds & kKindUnknown)) return any_ty();
    if (!(t->kinds & kKindFuture) || !t->elem) return empty_ty();  // only an error, never a value
    return t->elem;
}

bool holds_future(const TyRef& t, int depth) {
    if (!t) return false;
    if (t->kinds & kKindTask) return true;
    if (depth > kMaxDepth + 2) return false;
    if (holds_future(t->elem, depth + 1)) return true;
    for (const TyRef& item : t->items)
        if (holds_future(item, depth + 1)) return true;
    for (const auto& field : t->fields)
        if (holds_future(field.second, depth + 1)) return true;
    return false;
}

bool holds_collection(const TyRef& t, int depth) {
    if (!t) return false;
    if (t->any || (t->kinds & (kKindTagged | kKindUnknown)) || t->struct_id != -1) return true;
    if (depth > kMaxDepth + 2) return true;  // beyond the cap: may be anything
    if (holds_collection(t->elem, depth + 1) || holds_collection(t->key, depth + 1)) return true;
    for (const TyRef& item : t->items)
        if (holds_collection(item, depth + 1)) return true;
    for (const auto& field : t->fields)
        if (holds_collection(field.second, depth + 1)) return true;
    return false;
}

} // namespace nift::rt::infer_detail
