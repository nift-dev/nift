#include "rt/infer_session.h"
#include "rt/compiler.h"

// rt-lint: cold-file incremental namespace facts in the shared kind inference
namespace nift::rt::infer_detail {
TyRef Inference::constant(NodeId id, double v) { return kinds_ty(constants_.set(ast_.size(), id, v)); }

void Inference::seed_session() {
    if (session_) for (const auto& symbol : session_->symbols) {
        external_.emplace(symbol.name, vars_.size());
        const auto declaration = session_->declarations.find(symbol.name);
        if (declaration != session_->declarations.end()) binding_decls_[vars_.size()] = declaration->second;
        auto type = std::make_shared<Ty>(symbol.structure ? *symbol.structure : Ty{});
        type->kinds = symbol.kinds;
        const auto lambda = session_->lambda_templates.find(symbol.name);
        if (lambda != session_->lambda_templates.end())
            type->callables.push_back(static_cast<std::uint32_t>(lambda_unit(lambda->second)));
        else if (symbol.definition) {
            const auto target = plan_.index.find(symbol.definition->name);
            if (target != plan_.index.end()) type->callables.push_back(target->second - 1u);
        }
        vars_.push_back(type);
    }
}
void Inference::export_session() {
    if (!session_) return;
    for (const auto& binding : top_level_slots_)
        result_.binding_shapes[std::string(binding.first)] = vars_[binding.second];
}
TyRef Inference::session_call(const Node& n) {
    const Node& callee = ast_.at(n.a);
    if (!session_ || callee.kind != NodeKind::Ident) return {};
    for (const auto& symbol : session_->symbols) if (symbol.name == ast_.text(callee) && symbol.callable) {
        if (session_->lambda_templates.count(symbol.name) ||
            (symbol.definition && plan_.index.count(symbol.definition->name))) return {};
        expr(n.a);
        for (std::uint32_t k = 0; k < n.count; ++k) expr(ast_.list(n)[k]);
        return kinds_ty(symbol.returns ? symbol.returns : static_cast<KindSet>(kKindAll | kKindUnknown));
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
