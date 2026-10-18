#include "rt/infer.h"

#include "rt/infer_types.h"

#include "rt/literal.h"
#include "rt/native.h"

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt {

namespace {

using namespace infer_detail;

class Inference {
public:
    Inference(const Ast& ast, NodeId root, const UnitPlan& plan) : ast_(ast), root_(root), plan_(plan) {
        result_.kinds.assign(ast.size(), static_cast<std::uint8_t>(kKindAll | kKindUnknown));
        const std::size_t n = plan.functions.size();
        params_.resize(n);
        returns_.assign(n, empty_ty());
        for (std::size_t f = 0; f < n; ++f) params_[f].assign(ast.at(plan.functions[f]).count, empty_ty());
    }

    UnitKinds run() {
        // Monotone unions over a finite (depth-capped) lattice: terminates.
        // The final pass (no change) leaves every node consistent.
        for (int pass = 0; pass < 200; ++pass) {
            changed_ = false;
            walk_main();
            for (std::size_t f = 0; f < plan_.functions.size(); ++f) walk_function(f);
            if (!changed_) return std::move(result_);
        }
        // Not converged (cannot happen with the depth cap): know nothing.
        result_.kinds.assign(ast_.size(), static_cast<std::uint8_t>(kKindAll | kKindUnknown));
        return std::move(result_);
    }

private:
    using Scope = std::vector<std::pair<std::string_view, std::size_t>>;

    void merge(TyRef& into, const TyRef& value) {
        TyRef next = unite(into, value);
        if (!same(next, into)) {
            into = std::move(next);
            changed_ = true;
        }
    }

    void declare(std::string_view name, NodeId at, const TyRef& value) {
        if (persist_index_.empty()) persist_index_.assign(ast_.size(), static_cast<std::size_t>(-1));
        std::size_t& slot = persist_index_[at];
        if (slot == static_cast<std::size_t>(-1)) {
            slot = vars_.size();
            vars_.push_back(empty_ty());
        }
        merge(vars_[slot], value);
        scopes_.back().emplace_back(name, slot);
    }

    TyRef* find(std::string_view name) {
        for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
            for (auto entry = scope->rbegin(); entry != scope->rend(); ++entry)
                if (entry->first == name) return &vars_[entry->second];
        return nullptr;
    }

    TyRef record(NodeId id, TyRef t) {
        result_.kinds[id] = t->kinds;
        return t;
    }

    int function_index(const Node& callee) const {
        if (callee.kind != NodeKind::Ident) return -1;
        const auto fn = plan_.index.find(ast_.text(callee));
        return fn == plan_.index.end() ? -1 : static_cast<int>(fn->second) - 1;
    }

    TyRef expr(NodeId id) {
        const Node& n = ast_.at(id);
        TyRef t;
        switch (n.kind) {
            case NodeKind::Number: {
                Value v;
                if (!decode_number_literal(ast_.text(n), v)) t = kinds_ty(kKindNumber);
                else if (v.is(Kind::Int) || std::trunc(v.as_double()) == v.as_double()) t = kinds_ty(kKindInt);
                else t = kinds_ty(kKindFrac);
                break;
            }
            case NodeKind::String: t = kinds_ty(kKindString); break;
            case NodeKind::True: case NodeKind::False: t = kinds_ty(kKindBool); break;
            case NodeKind::Null: t = kinds_ty(kKindNull); break;
            case NodeKind::Ident: {
                const TyRef* v = find(ast_.text(n));
                t = v ? *v : any_ty();
                break;
            }
            case NodeKind::Unary: {
                const TyRef o = expr(n.a);
                t = kinds_ty(n.op == Op::Not ? kKindBool
                                             : static_cast<std::uint8_t>(o->kinds & (kKindNumber | kKindUnknown)));
                break;
            }
            case NodeKind::Binary: {
                const TyRef l = expr(n.a), r = expr(n.b);
                switch (n.op) {
                    case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
                        t = kinds_ty(arithmetic_kinds(n.op, l->kinds, r->kinds));
                        break;
                    default: t = kinds_ty(kKindBool); break;
                }
                break;
            }
            case NodeKind::Logical: expr(n.a); expr(n.b); t = kinds_ty(kKindBool); break;
            case NodeKind::Coalesce: {
                const TyRef l = expr(n.a), r = expr(n.b);
                if (l->kinds & kKindNull) {
                    auto without = std::make_shared<Ty>(*l);
                    without->kinds = static_cast<std::uint8_t>(l->kinds & ~kKindNull);
                    t = unite(TyRef(without), r);
                } else {
                    t = l;
                }
                break;
            }
            case NodeKind::Ternary: {
                expr(n.a);
                const TyRef b = expr(n.b), c = expr(n.c);
                t = unite(b, c);
                break;
            }
            case NodeKind::Member: {
                const TyRef o = expr(n.a);
                const std::string key(ast_.text(ast_.at(n.b)));
                t = element_of(o, &key, true);
                break;
            }
            case NodeKind::Index: {
                const TyRef o = expr(n.a);
                expr(n.b);
                const Node& k = ast_.at(n.b);
                std::string key;
                const bool literal = k.kind == NodeKind::String && k.count == 0 && ast_.text(k).size() >= 2;
                if (literal) key = std::string(ast_.text(k).substr(1, ast_.text(k).size() - 2));
                long position = -1;
                const std::string_view digits = ast_.text(k);
                if (k.kind == NodeKind::Number && !digits.empty() && digits.size() < 9 &&
                    digits.find_first_not_of("0123456789") == std::string_view::npos)
                    position = std::stol(std::string(digits));
                t = element_of(o, literal ? &key : nullptr, false, position);
                break;
            }
            case NodeKind::Array: {
                const NodeId* items = ast_.list(n);
                auto a = std::make_shared<Ty>();
                a->kinds = kKindArray;
                TyRef elem = empty_ty();
                a->tuple = true;
                for (std::uint32_t k = 0; k < n.count; ++k) {
                    const TyRef item = expr(items[k]);
                    a->items.push_back(item);
                    elem = unite(elem, item);
                }
                a->elem = elem;
                t = a;
                break;
            }
            case NodeKind::Object: {
                const NodeId* props = ast_.list(n);
                auto o = std::make_shared<Ty>();
                o->kinds = kKindObject;
                for (std::uint32_t k = 0; k < n.count; ++k) {
                    const Node& prop = ast_.at(props[k]);
                    if (prop.a == kNoNode || prop.b == kNoNode) { t = any_ty(); break; }
                    const std::string_view quoted = ast_.text(ast_.at(prop.a));
                    const std::string key(quoted.size() >= 2 ? quoted.substr(1, quoted.size() - 2) : quoted);
                    o->fields[key] = unite(o->fields.count(key) ? o->fields[key] : empty_ty(), expr(prop.b));
                }
                if (!t) t = o;
                break;
            }
            case NodeKind::Call: t = call(n); break;
            default: t = any_ty(); break;  // not compiled (the compiler refuses it)
        }
        return record(id, t ? t : empty_ty());
    }

    TyRef call(const Node& n) {
        const Node& callee = ast_.at(n.a);
        const NodeId* args = ast_.list(n);
        if (callee.kind == NodeKind::Member) {  // length() / size() (other methods are refused)
            expr(callee.a);
            for (std::uint32_t k = 0; k < n.count; ++k) expr(args[k]);
            return kinds_ty(kKindInt);
        }
        const int f = function_index(callee);
        std::vector<TyRef> values(n.count);
        for (std::uint32_t k = 0; k < n.count; ++k) values[k] = expr(args[k]);
        if (f < 0) {
            NativeInfo native{};
            if (callee.kind == NodeKind::Ident && lookup_native(ast_.text(callee), native)) return kinds_ty(kKindNull);
            return any_ty();
        }
        auto& params = params_[static_cast<std::size_t>(f)];
        for (std::uint32_t k = 0; k < n.count && k < params.size(); ++k) merge(params[k], values[k]);
        return returns_[static_cast<std::size_t>(f)];
    }

    // Can control reach the end of `block` (a Block node)?
    bool falls_through(NodeId block) const {
        const Node& b = ast_.at(block);
        if (b.count == 0) return true;
        const Node& last = ast_.at(ast_.list(b)[b.count - 1]);
        if (last.kind == NodeKind::Return) return false;
        if (last.kind == NodeKind::If) return if_falls(ast_.list(b)[b.count - 1]);
        return true;
    }

    bool if_falls(NodeId id) const {
        const Node& n = ast_.at(id);
        if (n.c == kNoNode) return true;
        const Node& other = ast_.at(n.c);
        return falls_through(n.b) || (other.kind == NodeKind::If ? if_falls(n.c) : falls_through(n.c));
    }

    void block(NodeId id) {
        scopes_.emplace_back();
        statements(ast_.at(id));
        scopes_.pop_back();
    }

    void statements(const Node& b) {
        const NodeId* list = ast_.list(b);
        for (std::uint32_t k = 0; k < b.count; ++k) statement(list[k]);
    }

    void add_element(const Node& receiver, const TyRef& value) {
        if (receiver.kind != NodeKind::Ident) return;
        TyRef* var = find(ast_.text(receiver));
        if (!var || (*var)->any) return;
        auto updated = std::make_shared<Ty>(**var);
        updated->elem = unite((*var)->elem, value);
        updated->tuple = false;
        updated->items.clear();
        // The variable's type loses its positions for good (merge would keep
        // the old tuple): replace it, monotonically larger.
        TyRef next = unite(TyRef(updated), TyRef(updated));
        if (!same(next, *var)) { *var = next; changed_ = true; }
    }

    void statement(NodeId id) {
        const Node& n = ast_.at(id);
        switch (n.kind) {
            case NodeKind::Declare: {
                const TyRef v = expr(n.b);
                const Node& target = ast_.at(n.a);
                if (target.kind == NodeKind::Ident) declare(ast_.text(target), id, v);
                break;
            }
            case NodeKind::Assign: {
                const TyRef v = expr(n.b);
                const Node& target = ast_.at(n.a);
                if (target.kind == NodeKind::Ident) {
                    if (TyRef* var = find(ast_.text(target))) merge(*var, v);
                } else if (target.kind == NodeKind::Index) {
                    expr(target.b);
                    add_element(ast_.at(target.a), v);
                    expr(target.a);
                }
                break;
            }
            case NodeKind::CompoundAssign: {
                const TyRef v = expr(n.b);
                const Node& target = ast_.at(n.a);
                if (target.kind != NodeKind::Ident) break;
                TyRef* var = find(ast_.text(target));
                if (!var) break;
                Op op = Op::Add;
                switch (n.op) {
                    case Op::SubAssign: op = Op::Sub; break;
                    case Op::MulAssign: op = Op::Mul; break;
                    case Op::DivAssign: op = Op::Div; break;
                    case Op::ModAssign: op = Op::Mod; break;
                    default: break;
                }
                std::uint8_t out = arithmetic_kinds(op, (*var)->kinds, v->kinds);
                if (op == Op::Add && ((*var)->kinds & kKindString) && (v->kinds & kKindString)) out |= kKindString;
                merge(*var, kinds_ty(out));
                record(n.a, *var);
                break;
            }
            case NodeKind::IncDec: {
                const Node& target = ast_.at(n.a);
                if (target.kind == NodeKind::Ident)
                    if (TyRef* var = find(ast_.text(target))) {
                        merge(*var, kinds_ty(((*var)->kinds & kKindNumber) ? kKindInt : 0));
                        record(n.a, *var);
                    }
                break;
            }
            case NodeKind::If: {
                expr(n.a);
                block(n.b);
                if (n.c != kNoNode) {
                    if (ast_.at(n.c).kind == NodeKind::If) statement(n.c);
                    else block(n.c);
                }
                break;
            }
            case NodeKind::While: expr(n.a); block(n.b); break;
            case NodeKind::Return:
                if (!(n.flags & kFlagBare) && n.a != kNoNode) {
                    const TyRef v = expr(n.a);
                    if (current_ >= 0) merge(returns_[static_cast<std::size_t>(current_)], v);
                } else if (current_ >= 0) {
                    merge(returns_[static_cast<std::size_t>(current_)], kinds_ty(kKindNull));
                }
                break;
            case NodeKind::ExprStmt: {
                const Node& e = ast_.at(n.a);
                if (e.kind == NodeKind::Call && ast_.at(e.a).kind == NodeKind::Member &&
                    ast_.text(ast_.at(ast_.at(e.a).b)) == "push") {  // rt-lint: allow-string-compare compile time
                    const NodeId* args = ast_.list(e);
                    TyRef v = empty_ty();
                    for (std::uint32_t k = 0; k < e.count; ++k) v = expr(args[k]);
                    expr(ast_.at(e.a).a);
                    if (e.count == 1) add_element(ast_.at(ast_.at(e.a).a), v);
                    break;
                }
                expr(n.a);
                break;
            }
            default: break;
        }
    }

    void walk_main() {
        current_ = -1;
        scopes_.assign(1, Scope());
        const Node& program = ast_.at(root_);
        const NodeId* list = ast_.list(program);
        for (std::uint32_t k = 0; k < program.count; ++k)
            if (ast_.at(list[k]).kind != NodeKind::FnDecl) statement(list[k]);
    }

    void walk_function(std::size_t f) {
        current_ = static_cast<int>(f);
        const Node& fn = ast_.at(plan_.functions[f]);
        scopes_.assign(1, Scope());
        const NodeId* params = ast_.list(fn);
        for (std::uint32_t k = 0; k < fn.count; ++k) declare(ast_.text(ast_.at(params[k])), params[k], params_[f][k]);
        statements(ast_.at(fn.b));
        if (falls_through(fn.b)) merge(returns_[f], kinds_ty(kKindNull));
    }

    const Ast& ast_;
    NodeId root_;
    const UnitPlan& plan_;
    UnitKinds result_;
    std::vector<std::vector<TyRef>> params_;
    std::vector<TyRef> returns_;
    std::vector<TyRef> vars_;
    std::vector<std::size_t> persist_index_;
    std::vector<Scope> scopes_;
    int current_ = -1;
    bool changed_ = false;
};

} // namespace

UnitKinds infer_unit_kinds(const Ast& ast, NodeId root, const UnitPlan& plan) {
    return Inference(ast, root, plan).run();
}

} // namespace nift::rt
