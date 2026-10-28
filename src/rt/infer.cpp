#include "rt/infer.h"
#include "rt/infer_unit.h"
#include "rt/compiler.h"
#include "rt/session_shape.h"
#include "rt/text_type.h"
#include "rt/infer_builtins.h"
#include "rt/collection.h"
#include "rt/literal.h"
#include "rt/number.h"
#include <memory>
#include <string>
// rt-lint: cold-file compile-time analysis, once per unit
namespace nift::rt {
namespace infer_detail {
Inference::Inference(const Ast& ast, NodeId root, const UnitPlan& plan, const GlobalPlan* globals,
                     const SessionCompileContext* session)
    : ast_(ast), root_(root), plan_(plan), globals_(globals), session_(session) {
    if (session && (!session->call_result_seeds.empty() || !session->opaque_call_seeds.empty()))
        result_.session_proof = std::make_shared<SessionInferenceProof>();
    result_.kinds.assign(ast.size(), static_cast<KindSet>(kKindAll | kKindUnknown));
    const Node& program = ast.at(root);
    const NodeId* top = ast.list(program);
    for (std::uint32_t k = 0; k < program.count; ++k)
        if (ast.at(top[k]).kind == NodeKind::Declare && ast.at(top[k]).a != kNoNode &&
            ast.at(ast.at(top[k]).a).kind == NodeKind::Ident)
            top_level_decls_.insert(top[k]);
    const std::size_t n = plan.functions.size();
    params_.resize(n);
    returns_.assign(n, empty_ty());
    for (std::size_t f = 0; f < n; ++f) {
        const auto& fn = ast.at(plan.functions[f]);
        params_[f].assign(fn.count, empty_ty());
        if (session_) for (std::uint32_t k = 0; k < fn.count; ++k) {
            const auto seed = session_->parameter_seeds.find(ast.list(fn)[k]);
            if (seed != session_->parameter_seeds.end()) params_[f][k] = seed->second;
        }
    }
}
UnitKinds Inference::run(std::size_t max_walks) {
    const std::size_t n = plan_.functions.size();
    callers_.assign(n, {});
    queued_.assign(n + 1, false);
    session_begin_locations();
    seed_session();
    seed_struct_fields();  // CP12b structs: each field starts as its initialiser
    enqueue(n);  // main first: its calls seed the parameters
    for (std::size_t f = n; f-- > 0;) enqueue(f);
    for (std::size_t budget = max_walks ? max_walks : 64 * (n + 1) + 1024; !queue_.empty(); --budget) {
        if (budget == 0) return std::move(result_);  // converged stays false
        const std::size_t unit = queue_.front();
        queue_.pop_front();
        queued_[unit] = self_changed_ = false;
        walk_unit(unit);
        if (session_locations_ && !session_locations_converged()) return std::move(result_);
        ++result_.walks;
        if (self_changed_) enqueue(unit);
    }
    export_session();
    result_.converged = true;
    return std::move(result_);
}

void Inference::enqueue(std::size_t unit) {
    if (session_ && unit > plan_.functions.size()) {
        const auto index = unit - plan_.functions.size() - 1;
        if (index < lambdas_.size() && lambdas_[index].scopes.empty() &&
            session_->template_definitions.count(lambdas_[index].node)) return;
    }
    if (unit >= queued_.size()) queued_.resize(unit + 1, false);
    if (!queued_[unit]) queue_.push_back(unit);
    queued_[unit] = true;
}

std::size_t Inference::current_unit() const {
    return current_ < 0 ? plan_.functions.size() : static_cast<std::size_t>(current_);
}

bool Inference::merge(TyRef& into, const TyRef& value) {
    TyRef next = unite(into, value);
    if (same(next, into)) return false;
    into = std::move(next);
    return true;
}

void Inference::merge_local(TyRef& into, const TyRef& value) {
    if (!merge(into, value)) return;
    if (session_locations_) session_sync_locations();
    self_changed_ = true;  // a global's slot is shared by every unit using it (CP12b):
    if (session_) {
        for (std::size_t u = 0; u <= plan_.functions.size() + lambdas_.size(); ++u) enqueue(u);
    } else for (std::size_t u = 0; globals_.shared(vars_, &into) && u <= plan_.functions.size(); ++u) enqueue(u);
}

void Inference::merge_return(const TyRef& value) {  // the callers read the return
    if (current_ >= 0 && static_cast<std::size_t>(current_) < plan_.functions.size()) {
        if (merge(returns_[static_cast<std::size_t>(current_)], value))
            for (std::size_t caller : callers_[static_cast<std::size_t>(current_)]) enqueue(caller);
    } else if (current_ > 0 && static_cast<std::size_t>(current_) > plan_.functions.size()) {
        merge_lambda_return(static_cast<std::size_t>(current_) - plan_.functions.size() - 1, value);
    }
}

void Inference::merge_lambda_return(std::size_t lambda, const TyRef& value) {
    LambdaInfo& info = lambdas_[lambda];
    if (!merge(info.returns, value)) return;
    for (std::size_t caller : info.callers) enqueue(caller);
}

void Inference::declare(std::string_view name, NodeId at, const TyRef& value) {
    const std::size_t slot = persistent_slot(persist_index_, vars_, ast_.size(), at);
    binding_decls_[slot]=ast_.at(at).kind==NodeKind::Declare?ast_.at(at).a:at;
    if (session_ && ((ast_.at(at).kind == NodeKind::Declare && (ast_.at(at).flags & kFlagConst)) ||
        session_->const_captures.count(binding_decls_[slot]))) const_arrays_.insert(slot);
    if (session_ && (top_level_decls_.count(at) || (value->kinds & (kKindArray | kKindObject)))) {
        const auto type = ast_.at(at).kind == NodeKind::Declare ?
            infer_text_type(ast_.text(ast_.at(ast_.at(at).b))) : TextType{};
        merge_local(vars_[slot], session_global_shape(value,
            type.kind == TextType::Kind::Known && type.known == 3));
    } else merge_local(vars_[slot], value);
    if (session_locations_) {
        const auto locations = session_->capture_locations.find(binding_decls_[slot]);
        if (locations != session_->capture_locations.end())
            for (const auto& location : locations->second) session_location_slot(location.key,slot);
    }
    scopes_.back().emplace_back(name, slot);
    if (current_ == -1 && top_level_decls_.count(at)) top_level_slots_[name] = slot;
}

TyRef* Inference::find(std::string_view name) {
    for (auto scope = scopes_.rbegin(); scope != scopes_.rend(); ++scope)
        for (auto entry = scope->rbegin(); entry != scope->rend(); ++entry)
            if (entry->first == name) return &vars_[entry->second];
    // A function's free name resolved to a top-level binding (rt/globals.h).
    // A lambda uses its enclosing named function's free-name map.
    const int owner = current_ >= 0 && static_cast<std::size_t>(current_) < plan_.functions.size()
                          ? current_
                          : owner_function_;
    const std::size_t slot = globals_.find(owner, name, persist_index_, vars_, ast_.size());
    if (slot != GlobalSlots::kNone) return &vars_[slot];
    const auto external = external_.find(name);
    if (external != external_.end() && (!plan_.index.count(name) ||
        (session_ && session_->phased_functions.count(std::string(name))))) return &vars_[external->second];
    if (current_ < 0 || static_cast<std::size_t>(current_) <= plan_.functions.size()) return nullptr;
    const auto top = top_level_slots_.find(name);
    return top == top_level_slots_.end() ? nullptr : &vars_[top->second];
}



int Inference::function_index(const Node& callee) const {
    if (callee.kind != NodeKind::Ident) return -1;
    const auto fn = plan_.index.find(ast_.text(callee));
    return fn == plan_.index.end() ? -1 : static_cast<int>(fn->second) - 1;
}

TyRef Inference::expr(NodeId id) {
    const Node& n = ast_.at(id);
    if (const auto seed = session_expression_seed(id)) return record(id, seed);
    TyRef t;
    switch (n.kind) {
        case NodeKind::Number: t=number_literal(id);break;
        case NodeKind::String: t = kinds_ty(kKindString); break;
        case NodeKind::True: case NodeKind::False: t = kinds_ty(kKindBool); break;
        case NodeKind::Null: t = kinds_ty(kKindNull); break;
        case NodeKind::Ident: {
            const TyRef* v = find(ast_.text(n));
            if (v) {
                t = *v;
            } else {
                const int f = function_index(n);
                if (f < 0) {
                    t = any_ty();
                } else {
                    auto fn = std::make_shared<Ty>();
                    fn->kinds = kKindFunction;
                    fn->callables.push_back(static_cast<std::uint32_t>(f));
                    t = fn;
                }
            }
            break;
        }
        case NodeKind::Lambda: {
            auto fn = std::make_shared<Ty>();
            fn->kinds = kKindFunction;
            fn->callables.push_back(static_cast<std::uint32_t>(lambda_unit(id)));
            t = fn;
            break;
        }
        case NodeKind::Unary: {
            const TyRef o = expr(n.a);
            t = kinds_ty(n.op == Op::Not ? kKindBool
                                         : static_cast<KindSet>(o->kinds & (kKindNumber | kKindUnknown)));
            if (n.op == Op::Neg && constants_.has(n.a)) t = constant(id, -constants_.at(n.a));
            t=with_text_number(t,o->legacy_text_number&&n.op!=Op::Not);
            break;
        }
        case NodeKind::Binary: {
            const TyRef l = expr(n.a), r = expr(n.b);
            switch (n.op) {
                case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
                    t = kinds_ty(arithmetic_kinds(n.op, l->kinds, r->kinds));
                    if (double v = 0; constants_.has(n.a) && constants_.has(n.b) &&
                                      fold_constant(n.op, constants_.at(n.a), constants_.at(n.b), v))
                        t = constant(id, v);  // literal arithmetic: its exact kind (review round 2)
                    break;
                default: t = kinds_ty(kKindBool); break;
            }
            t=with_text_number(t,(t->kinds&kKindNumber)&&(l->legacy_text_number||r->legacy_text_number));
            break;
        }
        case NodeKind::Logical: expr(n.a); expr(n.b); t = kinds_ty(kKindBool); break;
        case NodeKind::Coalesce: {
            const TyRef l = expr(n.a), r = expr(n.b);
            if (l->kinds & kKindNull) {
                auto without = std::make_shared<Ty>(*l);
                without->kinds = static_cast<KindSet>(l->kinds & ~kKindNull);
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
            const TyRef error_receiver = expr(n.a);
            if ((error_receiver->kinds & kKindError) &&
                (error_receiver->kinds & ~(kKindError | kKindNull)) == 0) {
                const std::string_view member = ast_.text(ast_.at(n.b));
                if (member == "message" || member == "code" || member == "category" || member == "source")
                    t = kinds_ty(kKindString);
                else if (member == "line" || member == "column")
                    t = kinds_ty(kKindInt);
                else if (member == "cause")
                    t = kinds_ty(static_cast<KindSet>(kKindError | kKindNull));
                else
                    t = any_ty();
                break;
            }
            if (!plan_.structs.empty()) {  // CP12b: a struct field
                const int s = receiver_struct(n.a);
                if (s >= 0) {
                    const TyRef* field = struct_field(s, ast_.text(ast_.at(n.b)));
                    t = field ? *field : any_ty();
                    break;
                }
            }
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
            const long position = k.kind == NodeKind::Number ? literal_position(ast_.text(k)) : -1;
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
        case NodeKind::Assign: case NodeKind::CompoundAssign:
            statement(id); t = expr(n.a); break;
        case NodeKind::IncDec: { t=with_text_number(kinds_ty(kKindNumber),expr(n.a)->legacy_text_number);break; }
        case NodeKind::Call: t = call(n); break;
        case NodeKind::Await: t = awaited(expr(n.a)); break;  // CP22
        default: t = any_ty(); break;  // not compiled (the compiler refuses it)
    }
    return record(id, t ? t : empty_ty());
}

TyRef Inference::call(const Node& n) {
    const Node& callee = ast_.at(n.a);
    if (const auto external = session_call(n)) return external;
    const NodeId* args = ast_.list(n);
    // CP13B: the reserved Error constructor.
    if (callee.kind == NodeKind::Ident && ast_.text(callee) == "error" && find(ast_.text(callee)) == nullptr &&
        plan_.index.find(ast_.text(callee)) == plan_.index.end()) {
        for (std::uint32_t k = 0; k < n.count; ++k) expr(args[k]);
        return kinds_ty(kKindError);
    }
    // CP13B: Error JSON presentation methods (ErrorJson), not natives.
    if (callee.kind == NodeKind::Member) {
        const TyRef recv = expr(callee.a);
        if (session_ && !recv->any && recv->kinds == 0) return session_bottom_arguments(n);
        const std::string_view name = ast_.text(ast_.at(callee.b));
        if ((recv->kinds & kKindError) && (recv->kinds & ~(kKindError | kKindNull)) == 0 &&
            (name == "stringify" || name == "prettify" || name == "highlight") && n.count == 0)
            return kinds_ty(kKindString);
    }
    if (callee.kind == NodeKind::Member && !plan_.structs.empty()) {  // CP12b: a struct method
        const int s = receiver_struct(callee.a);
        if (s >= 0) {
            const StructMethodInfo* m = plan_.structs[static_cast<std::size_t>(s)].method(ast_.text(ast_.at(callee.b)));
            if (m != nullptr) return method_call(m->function, n);
            for (std::uint32_t k = 0; k < n.count; ++k) expr(args[k]);
            return any_ty();
        }
    }
    if (callee.kind == NodeKind::Ident && !plan_.structs.empty()) {
        const auto s = plan_.struct_index.find(ast_.text(callee));
        if (s != plan_.struct_index.end()) {  // construction (and its constructor)
            const StructInfo& info = plan_.structs[s->second];
            if (info.ctor >= 0) method_call(info.methods[static_cast<std::size_t>(info.ctor)].function, n);
            else for (std::uint32_t k = 0; k < n.count; ++k) expr(args[k]);
            auto instance = std::make_shared<Ty>();
            instance->kinds = kKindObject;
            instance->struct_id = static_cast<int>(s->second);
            return instance;
        }
        const int own = current_struct();  // an implicit call of a method of the struct
        if (own >= 0 && function_index(callee) < 0)
            if (const StructMethodInfo* m = plan_.structs[static_cast<std::size_t>(own)].method(ast_.text(callee)))
                return method_call(m->function, n);
    }
    if (callee.kind == NodeKind::Member) {  // length()/size(), CP14 methods, or a callable field
        const TyRef recv = expr(callee.a);
        const std::string key(ast_.text(ast_.at(callee.b)));
        const TyRef field = record(n.a, element_of(recv, &key, true));
        std::vector<TyRef> values(n.count);
        for (std::uint32_t k = 0; k < n.count; ++k) values[k] = expr(args[k]);
        if (const auto historical = session_historical_call(n, field)) return historical;
        if (!field->callables.empty()) {
            bool grew = false;
            const TyRef result = call_callables(field, values, grew);
            return result;
        }
        const int collection = collection_method(key);
        if (collection >= 0 && recv->kinds == kKindArray) {
            const auto result = collection_result(n, recv, values, collection);
            if (session_) if (const auto historical = session_collection_call(n, values, collection)) return historical;
            return result;
        }
        TyRef result=method_result(ast_, n, recv, values, session_ != nullptr);
        if((result->kinds&kKindNumber)&&key!="size"&&key!="length") {
            result=with_text_number(result,true);
        }
        return result;
    }
    if (plan_.coroutines && callee.kind == NodeKind::Ident && n.count > 0) if (TyRef t = thread_call(n)) return t;
    const TyRef callee_ty = expr(n.a);
    std::vector<TyRef> values(n.count);
    for (std::uint32_t k = 0; k < n.count; ++k) values[k] = expr(args[k]);
    if (session_ && !callee_ty->any && callee_ty->kinds == 0) return empty_ty();
    if (session_ && !callee_ty->any && !(callee_ty->kinds & (kKindFunction | kKindUnknown)) &&
        callee.kind == NodeKind::Ident && find(ast_.text(callee))) return empty_ty();
    if (const auto unknown = session_unknown_call(n, callee_ty)) return unknown;
    if (const auto historical = session_historical_call(n, callee_ty)) return historical;
    if (!callee_ty->callables.empty()) {
        bool grew = false;
        return call_callables(callee_ty, values, grew);
    }
    // print, a CP14 free built-in, or something the compiler refuses
    TyRef result=callee.kind == NodeKind::Ident ? function_result(ast_, n, values) : any_ty();
    if(result->kinds&kKindNumber) {
        result=with_text_number(result,true);
    }
    return result;
}

} // namespace infer_detail

} // namespace nift::rt
