#include "rt/compiler_unit_helpers.h"

namespace nift::rt::detail {

bool UnitCompiler::binding_error_certain(const Binding& b, KindSet value) const {
    const auto single = [](KindSet k) { return k != 0 && (k & (k - 1)) == 0; };
    return binding_error_possible(b, value) && single(b.decl_kinds) && single(value) &&
           !((b.decl_kinds | value) & kKindUnknown);
}

bool UnitCompiler::loop_type_error_ambiguous() const {
    if (in_function_ && !in_lambda_ && !plan_.is_method(function_) && loop_called_[function_]) return true;
    return loop_reachable() && !loop_region() && !(in_function_ && plan_.is_method(function_));
}

bool UnitCompiler::return_error_attribution() const {
    // CP22 keeps its own future/task diagnostics: no return-site rewriting in
    // a coroutine unit.
    return (fb_->flags() & kInstrReturnCall) != 0 && !loop_region() && !plan_.coroutines &&
           !(in_function_ && !in_lambda_ && !plan_.is_method(function_) && loop_called_[function_]);
}

KindSet UnitCompiler::kinds(NodeId id) const {
    const KindSet k = kinds_.of(id);
    if (const KindSet proof = guarded_kind(id)) return proof;
    if (k == 0 && in_lambda_) return static_cast<KindSet>(kKindAll | kKindUnknown);
    return (k & kKindUnknown) && !probe_ ? static_cast<KindSet>(kKindAll | kKindUnknown) : k;
}

bool UnitCompiler::unknown_defers(KindSet k) const {
    if (!(k & kKindUnknown)) return false;
    if (!probe_) unknown_hit_ = true;
    return probe_;
}

bool UnitCompiler::field_in_loop_code() const {
    return loop_region() || (in_function_ && !in_lambda_ && !plan_.is_method(function_) && loop_called_[function_]);
}

int UnitCompiler::current_struct() const {
    if (lambda_struct_override_ >= 0) return lambda_struct_override_;
    return in_function_ && plan_.is_method(function_) ? plan_.method_of[function_] : -1;
}


} // namespace nift::rt::detail
