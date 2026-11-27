#include "rt/infer_merge.h"

namespace nift::rt::infer_detail {

void Inference::enqueue(std::size_t unit) {
    ++result_.work.reader_checks;  // B4b 28: every check, queued or not
    if (session_ && unit > plan_.functions.size()) {
        const auto index = unit - plan_.functions.size() - 1;
        if (index < lambdas_.size() && lambdas_[index].scopes.empty() &&
            session_->template_definitions.count(lambdas_[index].node)) return;
    }
    if (unit >= queued_.size()) queued_.resize(unit + 1, false);
    if (!queued_[unit]) queue_.push_back(unit), ++result_.work.enqueues;
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
    slot_changed(&into);  // B4b 26: every unit that read the slot (a global's, a field's too) walks again
    self_changed_ = true;
    if (session_)  // a session's earlier statements' code is not walked here (U6, B5): everything
        for (std::size_t u = 0; u <= plan_.functions.size() + lambdas_.size(); ++u) enqueue(u);
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

} // namespace nift::rt::infer_detail
