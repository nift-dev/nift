#include "rt/infer_session.h"
#include "rt/compiler.h"
#include "rt/session_shape.h"
#include "rt/collection.h"
#include <algorithm>

// rt-lint: cold-file incremental namespace facts in the shared kind inference
namespace nift::rt::infer_detail {
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
    if (session_) for (const auto& symbol : session_->symbols) {
        external_.emplace(symbol.name, vars_.size());
        if (symbol.const_array) const_arrays_.insert(vars_.size());
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
        vars_.push_back(type);
        if (session_locations_) {
            const auto location = session_->global_locations.find(symbol.name);
            if (location != session_->global_locations.end())
                session_location_slot(location->second.key,vars_.size()-1);
        }
    }
}
void Inference::export_session() {
    if (!session_) return;
    for (const auto& binding : binding_decls_)
        result_.session_decl_shapes[binding.second] = vars_[binding.first];
    if (session_->coherent_recompile) for (const auto& binding : external_)
        result_.binding_shapes[std::string(binding.first)] = vars_[binding.second];
    for (const auto& binding : top_level_slots_)
        result_.binding_shapes[std::string(binding.first)] = vars_[binding.second];
}
TyRef Inference::session_bottom_arguments(const Node& node) {
    for (std::uint32_t k=0;k<node.count;++k) expr(ast_.list(node)[k]);
    return empty_ty();
}
void Inference::session_call_edge(std::size_t target, std::size_t arity, std::size_t arguments) {
    if (result_.session_proof && arity == arguments)
        result_.session_proof->call_edges[current_unit()].insert(target);
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
                           const GlobalPlan* globals, const SessionCompileContext* session) {
    return infer_detail::Inference(ast, root, plan, globals, session).run(max_walks);
}

} // namespace nift::rt
