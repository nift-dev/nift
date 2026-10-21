#include "rt/infer_types.h"

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

TyRef kinds_ty(std::uint8_t kinds) {
    auto t = std::make_shared<Ty>();
    t->kinds = kinds;
    return t;
}

namespace {
constexpr int kMaxDepth = 6;
} // namespace

TyRef unite(const TyRef& a, const TyRef& b, int depth) {
    if (!a || a.get() == empty_ty().get()) return b ? b : empty_ty();
    if (!b || b.get() == empty_ty().get() || a.get() == b.get()) return a;
    if (a->any || b->any || depth > kMaxDepth) {
        if ((a->kinds | b->kinds) & kKindUnknown) return any_ty();
        return top_ty();
    }
    auto t = std::make_shared<Ty>();
    t->kinds = a->kinds | b->kinds;
    t->struct_id = a->struct_id == b->struct_id ? a->struct_id : -2;
    if (a->elem || b->elem) t->elem = unite(a->elem, b->elem, depth + 1);
    if (a->tuple && b->tuple && a->items.size() == b->items.size()) {
        t->tuple = true;
        for (std::size_t k = 0; k < a->items.size(); ++k)
            t->items.push_back(unite(a->items[k], b->items[k], depth + 1));
    }
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
    const bool a_empty = !a || (a->kinds == 0 && !a->elem && a->fields.empty() && !a->any);
    const bool b_empty = !b || (b->kinds == 0 && !b->elem && b->fields.empty() && !b->any);
    if (a_empty || b_empty) return a_empty == b_empty;
    if (a->kinds != b->kinds || a->any != b->any || a->fields.size() != b->fields.size()) return false;
    if (a->struct_id != b->struct_id) return false;
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
    if (last.kind == NodeKind::Return) return false;
    if (last.kind == NodeKind::If) return if_falls(ast, ast.list(b)[b.count - 1]);
    return true;
}

long literal_position(std::string_view digits) {
    if (digits.empty() || digits.size() >= 9 || digits.find_first_not_of("0123456789") != std::string_view::npos)
        return -1;
    return std::stol(std::string(digits));
}

std::uint8_t Constants::set(std::size_t nodes, NodeId id, double v) {
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

std::uint8_t arithmetic_kinds(Op op, std::uint8_t l, std::uint8_t r) {
    std::uint8_t out = 0;
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
    return static_cast<std::uint8_t>(out | ((l | r) & kKindUnknown));
}

} // namespace nift::rt::infer_detail
