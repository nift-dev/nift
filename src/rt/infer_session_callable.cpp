#include "rt/infer_session_callable.h"
#include "rt/compiler.h"
#include <algorithm>
// rt-lint: cold-file overlay current callable targets on immutable structural bounds
namespace nift::rt::infer_detail {
std::size_t Inference::session_lambda_reference(NodeId id) {
    // A reference imports a callable, not the caller's lexical environment.
    // Walking its actual template later installs the original capture scope.
    auto scopes = std::move(scopes_);
    const auto current = current_, owner = owner_function_;
    scopes_.clear(); current_ = owner_function_ = -1;
    const auto unit = lambda_unit(id);
    scopes_ = std::move(scopes); current_ = current; owner_function_ = owner;
    return unit;
}
TyRef Inference::session_type(const TyRef& bound, const std::shared_ptr<const SessionCallableShape>& shape) {
    if (!shape || !bound) return bound;
    auto out = std::make_shared<Ty>(*bound);
    for (const auto& target : shape->targets) {
        if (target.definition->lambda) {
            const auto node = session_->callable_templates.find(target.definition.get());
            if (node != session_->callable_templates.end())
                out->callables.push_back(static_cast<std::uint32_t>(session_lambda_reference(node->second)));
        } else {
            const auto node = plan_.index.find(target.definition->name);
            if (node != plan_.index.end()) out->callables.push_back(node->second - 1u);
        }
    }
    std::sort(out->callables.begin(), out->callables.end());
    out->callables.erase(std::unique(out->callables.begin(), out->callables.end()), out->callables.end());
    for (auto& field : out->fields) {
        const auto found = shape->fields.find(field.first);
        if (found != shape->fields.end()) field.second = session_type(field.second, found->second);
    }
    out->elem = session_type(bound->elem, shape->element);
    for (auto& item : out->items) item = session_type(item, shape->element);
    return out;
}
} // namespace nift::rt::infer_detail
