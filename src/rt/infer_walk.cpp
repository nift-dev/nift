#include "rt/infer_walk.h"

#include "rt/number.h"
#include "rt/session_mutation_shape.h"

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

KindSet Inference::value_kinds(const Value& v) {
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
            var = unite(var, kinds_ty(session_ && f.type == 3 ? kKindNumber : value_kinds(f.value)));
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
    if (session_) session_call_edge(f, params.size(), n.count);
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

void Inference::add_element(NodeId receiver_id, const TyRef& value) {
    const auto& receiver = ast_.at(receiver_id);
    if (session_) {
        const auto root = session_mutation_root(ast_, receiver_id);
        if (root == kNoNode) return;
        auto* var = find(ast_.text(ast_.at(root)));
        if (!var || (*var)->any) return;
        const auto slot = static_cast<std::size_t>(var - vars_.data());
        if (root == receiver_id && const_arrays_.count(slot)) return;  // native const guard cannot mutate
        const auto next = session_mutation_shape(ast_, receiver_id, *var, value);
        merge_local(*var, next);
        propagate_session_alias(slot);
        return;
    }
    if (receiver.kind != NodeKind::Ident) return;
    TyRef* var = find(ast_.text(receiver));
    if (!var || (*var)->any) return;
    auto updated = std::make_shared<Ty>(**var);
    updated->elem = unite(updated->elem, value);
    updated->tuple = false;
    updated->items.clear();
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
                if (TyRef* var = find(ast_.text(target))) {
                    if ((*var)->kinds & kKindAtomic) { record(n.a, *var); break; }  // CP23: atomic store
                    merge_local(*var, v); record(n.a, *var); }
            } else if (target.kind == NodeKind::Index) {
                expr(target.b);
                add_element(target.a, v);
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
            if ((*var)->kinds & kKindAtomic) { record(n.a, *var); break; }
            Op op = Op::Add;
            switch (n.op) {
                case Op::SubAssign: op = Op::Sub; break;
                case Op::MulAssign: op = Op::Mul; break;
                case Op::DivAssign: op = Op::Div; break;
                case Op::ModAssign: op = Op::Mod; break;
                default: break;
            }
            KindSet out = arithmetic_kinds(op, (*var)->kinds, v->kinds);
            if (op == Op::Add && ((*var)->kinds & kKindString) && (v->kinds & kKindString)) out |= kKindString;
            merge_local(*var,with_text_number(kinds_ty(out),v->legacy_text_number||(*var)->legacy_text_number));
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
                if (!((*var)->kinds & kKindAtomic))
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
        case NodeKind::For: for_statement(n); break;
        case NodeKind::Try:
            block(n.a);
            scopes_.emplace_back();
            if (n.b != kNoNode) declare(ast_.text(ast_.at(n.b)), n.b, kinds_ty(kKindError));
            block(n.c);
            scopes_.pop_back();
#if defined(NIFT_EXC_MUTANT) && NIFT_EXC_MUTANT == 3
            if (n.b != kNoNode && find(ast_.text(ast_.at(n.b))) == nullptr)
                declare(ast_.text(ast_.at(n.b)), n.b, kinds_ty(kKindError));
#endif
            break;
        case NodeKind::Throw:
            if (n.a != kNoNode) expr(n.a);
            break;
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
                if (e.count == 1) add_element(ast_.at(e.a).a, v);
                expr(ast_.at(e.a).a);
                break;
            }
            expr(n.a);
            break;
        }
        default: break;
    }
}

void Inference::walk_unit(std::size_t unit) {
    if (unit == plan_.functions.size()) walk_main();
    else if (unit < plan_.functions.size()) walk_function(unit);
    else walk_lambda(unit - plan_.functions.size() - 1);
}

void Inference::walk_lambda(std::size_t id) {
    LambdaInfo& info = lambdas_[id];
    const Node& lambda = ast_.at(info.node);
    current_ = static_cast<int>(plan_.functions.size() + 1 + id);
    owner_function_ = info.owner_function;
    scopes_ = info.scopes;
    scopes_.emplace_back();  // parameters and the body share this scope
    const NodeId* params = ast_.list(lambda);
    for (std::uint32_t k = 0; k < lambda.count; ++k)
        declare(ast_.text(ast_.at(params[k])), params[k], record(params[k], info.params[k]));
    if (lambda.flags & kFlagBlockBody) {
        statements(ast_.at(lambda.b));
        const Node& body=ast_.at(lambda.b);
        TyRef collection=empty_ty();
        if(body.count==1) {
            const Node& n=ast_.at(ast_.list(body)[0]);
            if(n.kind==NodeKind::ExprStmt||n.kind==NodeKind::Assign||n.kind==NodeKind::CompoundAssign)
                collection=expr(n.a);
        }
        if(merge(lambdas_[id].collection_returns,collection))
            for(auto caller:lambdas_[id].callers)enqueue(caller);
        if (block_falls_through(ast_, lambda.b)) merge_lambda_return(id, kinds_ty(kKindNull));
    } else {
        const Node& body=ast_.at(lambda.b);
        if(body.kind==NodeKind::Assign||body.kind==NodeKind::CompoundAssign) {
            statement(lambda.b);
            merge_lambda_return(id,expr(body.a));
        } else merge_lambda_return(id, expr(lambda.b));
    }
}

void Inference::walk_main() {
    current_ = -1;
    owner_function_ = -1;
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

// CP25-R: an interpolation's expressions are inferred where they are
// evaluated (left to right), so the compiler knows their kinds.
void Inference::interpolation(const Node& n) {
    const NodeId* parts = ast_.list(n);
    for (std::uint32_t k = 0; k < n.count; ++k)
        if (ast_.at(parts[k]).kind != NodeKind::StringPart) expr(parts[k]);
}

} // namespace nift::rt::infer_detail
