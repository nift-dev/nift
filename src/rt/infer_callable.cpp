#include "rt/infer_callable.h"
#include "rt/compiler.h"
// rt-lint: cold-file callable inference worklist and lexical environments
namespace nift::rt::infer_detail {
std::size_t Inference::lambda_unit(NodeId node) {
    const auto found = lambda_unit_.find(node);
    if (found != lambda_unit_.end()) {
        auto& info = lambdas_[found->second - plan_.functions.size() - 1];
        if (!scopes_.empty() && info.scopes != scopes_) {
            info.scopes = scopes_;
            info.owner_function = current_ >= 0 && static_cast<std::size_t>(current_) < plan_.functions.size()
                ? current_ : owner_function_;
            enqueue(found->second);
        }
        return found->second;
    }
    const std::size_t id = lambdas_.size();
    const std::size_t unit = plan_.functions.size() + 1 + id;
    LambdaInfo info;
    info.node = node;
    info.owner_function = current_ >= 0 && static_cast<std::size_t>(current_) < plan_.functions.size()
                              ? current_
                              : owner_function_;
    info.params.assign(ast_.at(node).count, empty_ty());
    if (session_) for (std::uint32_t k = 0; k < ast_.at(node).count; ++k) {
        const auto seed = session_->parameter_seeds.find(ast_.list(ast_.at(node))[k]);
        if (seed != session_->parameter_seeds.end()) info.params[k] = seed->second;
    }
    info.returns = empty_ty();
    info.collection_returns = empty_ty();
    info.scopes = scopes_;
    lambdas_.push_back(std::move(info));
    lambda_unit_.emplace(node, unit);
    enqueue(unit);
    return unit;
}

TyRef Inference::call_callables(const TyRef& callee, const std::vector<TyRef>& values, bool& grew) {
    TyRef out = empty_ty();
    for (std::uint32_t unit : callee->callables) {
        if (static_cast<std::size_t>(unit) < plan_.functions.size()) {
            auto& params = params_[unit];
            if (session_) session_call_edge(unit, params.size(), values.size());
            for (std::size_t k = 0; k < values.size() && k < params.size(); ++k)
                grew = merge(params[k], values[k]) || grew;
            callers_[unit].insert(current_unit());
            out = unite(out, returns_[unit]);
        } else if (static_cast<std::size_t>(unit) > plan_.functions.size()) {
            const std::size_t id = static_cast<std::size_t>(unit) - plan_.functions.size() - 1;
            LambdaInfo& info = lambdas_[id];
            if (session_) session_call_edge(unit, info.params.size(), values.size());
            for (std::size_t k = 0; k < values.size() && k < info.params.size(); ++k)
                grew = merge(info.params[k], values[k]) || grew;
            info.callers.insert(current_unit());
            out = unite(out, info.returns);
        }
        if (grew) enqueue(unit);
    }
    return out;
}

} // namespace nift::rt::infer_detail
