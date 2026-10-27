#pragma once
// Immutable session type contracts across shared-compiler units.
#include "rt/infer_types.h"
namespace nift::rt {
infer_detail::TyRef session_global_shape(const infer_detail::TyRef& type, bool number_binding = false);
inline KindSet session_binding_kinds(KindSet kinds) {
    return kinds && !(kinds & ~(kKindString | kKindFunction)) ? kinds | kKindString | kKindFunction : kinds;
}
bool session_proved_shape(const infer_detail::TyRef&);
infer_detail::TyRef session_current_shape(const infer_detail::TyRef&);
infer_detail::TyRef session_historical_shape(const infer_detail::TyRef&);
infer_detail::TyRef session_shape(const infer_detail::TyRef& type);
bool session_shape_within(const infer_detail::TyRef& value, const infer_detail::TyRef& bound);
} // namespace nift::rt
