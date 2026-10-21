#include "rt/infer_walk.h"

#include "rt/number.h"

#include <memory>

// rt-lint: cold-file compile-time analysis, once per unit

namespace nift::rt::infer_detail {

int Inference::current_struct() const {
    return current_ >= 0 && plan_.is_method(static_cast<std::size_t>(current_))
               ? plan_.method_of[static_cast<std::size_t>(current_)]
               : -1;
}

std::size_t Inference::field_slot(const StructFieldInfo& field) {
    const std::size_t slot = persistent_slot(persist_index_, vars_, ast_.size(), field.node);
    globals_.share(slot);
    return slot;
}

std::uint8_t Inference::value_kinds(const Value& v) {
    if (v.is_null()) return kKindNull;
    if (v.is(Kind::Bool)) return kKindBool;
    if (v.is(Kind::String)) return kKindString;
    if (v.is_number()) return number_is_integer(v) ? kKindInt : kKindFrac;
    return kKindAll;
}

void Inference::seed_struct_fields() {
    for (const StructInfo& s : plan_.structs)
        for (const StructFieldInfo& f : s.fields) {
            TyRef& var = vars_[field_slot(f)];
            var = unite(var, kinds_ty(value_kinds(f.value)));
        }
}

int Inference::receiver_struct(NodeId object) {
    const Node& o = ast_.at(object);
    if (o.kind == NodeKind::Ident && ast_.text(o) == "this")  // rt-lint: allow-string-compare compile time
        return record(object, any_ty()), current_struct();
    const TyRef t = expr(object);
    return t->struct_id >= 0 ? t->struct_id : -1;
}

TyRef* Inference::struct_field(int s, std::string_view name) {
    if (s < 0) return nullptr;
    const StructFieldInfo* f = plan_.structs[static_cast<std::size_t>(s)].field(name);
    return f ? &vars_[field_slot(*f)] : nullptr;
}

TyRef Inference::method_call(std::size_t f, const Node& n) {
    const NodeId* args = ast_.list(n);
    auto& params = params_[f];
    bool grew = false;
    for (std::uint32_t k = 0; k < n.count; ++k) {
        const TyRef v = expr(args[k]);
        if (k < params.size()) grew = merge(params[k], v) || grew;
    }
    if (grew) enqueue(f);
    callers_[f].insert(current_unit());
    return returns_[f];
}

void Inference::block(NodeId id) {
    scopes_.emplace_back();
    statements(ast_.at(id));
    scopes_.pop_back();
}

void Inference::statements(const Node& b) {
    const NodeId* list = ast_.list(b);
    for (std::uint32_t k = 0; k < b.count; ++k) statement(list[k]);
}

void Inference::add_element(const Node& receiver, const TyRef& value) {
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
    if (!same(next, *var)) { *var = next; self_changed_ = true; }
}

void Inference::statement(NodeId id) {
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
                if (TyRef* var = find(ast_.text(target))) { merge_local(*var, v); record(n.a, *var); }
            } else if (target.kind == NodeKind::Index) {
                expr(target.b);
                add_element(ast_.at(target.a), v);
                expr(target.a);
            } else if (target.kind == NodeKind::Member) {  // CP12b: a struct field
                if (TyRef* field = struct_field(receiver_struct(target.a), ast_.text(ast_.at(target.b)))) {
                    merge_local(*field, v);
                    record(n.a, *field);
                }
            }
            break;
        }
        case NodeKind::CompoundAssign: {
            const TyRef v = expr(n.b);
            const Node& target = ast_.at(n.a);
            TyRef* var = nullptr;
            if (target.kind == NodeKind::Member)  // CP12b: a struct field
                var = struct_field(receiver_struct(target.a), ast_.text(ast_.at(target.b)));
            else if (target.kind == NodeKind::Ident)
                var = find(ast_.text(target));
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
            merge_local(*var, kinds_ty(out));
            record(n.a, *var);
            break;
        }
        case NodeKind::IncDec: {
            const Node& target = ast_.at(n.a);
            TyRef* var = nullptr;
            if (target.kind == NodeKind::Member)  // CP12b: a struct field
                var = struct_field(receiver_struct(target.a), ast_.text(ast_.at(target.b)));
            else if (target.kind == NodeKind::Ident)
                var = find(ast_.text(target));
            if (var) {
                merge_local(*var, kinds_ty(((*var)->kinds & kKindNumber) ? kKindInt : 0));
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
            if (!(n.flags & kFlagBare) && n.a != kNoNode) merge_return(expr(n.a));
            else merge_return(kinds_ty(kKindNull));
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

void Inference::walk_main() {
    current_ = -1;
    scopes_.assign(1, Scope());
    const Node& program = ast_.at(root_);
    const NodeId* list = ast_.list(program);
    for (std::uint32_t k = 0; k < program.count; ++k)
        if (ast_.at(list[k]).kind != NodeKind::FnDecl) statement(list[k]);
}

void Inference::walk_function(std::size_t f) {
    current_ = static_cast<int>(f);
    const Node& fn = ast_.at(plan_.functions[f]);
    scopes_.assign(1, Scope());
    if (const int s = current_struct(); s >= 0) {  // CP12b: a method sees its struct's fields
        for (const StructFieldInfo& field : plan_.structs[static_cast<std::size_t>(s)].fields)
            scopes_.back().emplace_back(field.name, field_slot(field));
        scopes_.emplace_back();  // parameters and locals take precedence
    }
    const NodeId* params = ast_.list(fn);
    for (std::uint32_t k = 0; k < fn.count; ++k)  // record: the parameter's binding kinds
        declare(ast_.text(ast_.at(params[k])), params[k], record(params[k], params_[f][k]));
    statements(ast_.at(fn.b));
    if (block_falls_through(ast_, fn.b)) merge_return(kinds_ty(kKindNull));
}

} // namespace nift::rt::infer_detail
