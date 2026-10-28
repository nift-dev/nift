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
        t->kinds = kKindAll;
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

TyRef with_text_number(const TyRef& value,bool maybe) {
    if(!maybe||value->legacy_text_number)return value;
    auto next=std::make_shared<Ty>(*value);next->legacy_text_number=true;return next;
}

TyRef unite(const TyRef& a, const TyRef& b, int depth) {
    if (!a || a.get() == empty_ty().get()) return b ? b : empty_ty();
    if (!b || b.get() == empty_ty().get() || a.get() == b.get()) return a;
    if (a->any || b->any || depth > kMaxDepth) {
        if ((a->kinds | b->kinds) & kKindUnknown) return any_ty();
        return top_ty();
    }
    auto t = std::make_shared<Ty>();
    t->kinds = a->kinds | b->kinds;
    t->historical_callable = a->historical_callable || b->historical_callable;
    t->untracked_callable = a->untracked_callable || b->untracked_callable;
    t->legacy_text_number=a->legacy_text_number||b->legacy_text_number;
    t->collection_array = (t->kinds & kKindArray) && (!(a->kinds & kKindArray) || a->collection_array) &&
                          (!(b->kinds & kKindArray) || b->collection_array);
    t->collection_object = (t->kinds & kKindObject) && (!(a->kinds & kKindObject) || a->collection_object) &&
                           (!(b->kinds & kKindObject) || b->collection_object);
    t->struct_id = a->struct_id == b->struct_id ? a->struct_id : -2;
    if (a->elem || b->elem) t->elem = unite(a->elem, b->elem, depth + 1);
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
    if(a&&b&&(a->collection_array!=b->collection_array||a->collection_object!=b->collection_object||
              a->legacy_text_number!=b->legacy_text_number))return false;
    const bool a_empty = !a || (a->kinds == 0 && !a->elem && a->fields.empty() && !a->any);
    const bool b_empty = !b || (b->kinds == 0 && !b->elem && b->fields.empty() && !b->any);
    if (a_empty || b_empty) return a_empty == b_empty;
    if (a->historical_callable != b->historical_callable ||
        a->untracked_callable != b->untracked_callable) return false;
    if (a->kinds != b->kinds || a->any != b->any || a->fields.size() != b->fields.size()) return false;
    if (a->struct_id != b->struct_id || a->callables != b->callables) return false;
    if (a->tuple != b->tuple || a->items.size() != b->items.size()) return false;
    for (std::size_t k = 0; k < a->items.size(); ++k)
        if (!same(a->items[k], b->items[k])) return false;
    if (!same(a->elem, b->elem)) return false;
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

} // namespace nift::rt::infer_detail
