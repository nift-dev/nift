#pragma once
// Immutable session type contracts across shared-compiler units.
#include "rt/infer_types.h"
namespace nift::rt {
infer_detail::TyRef session_global_shape(const infer_detail::TyRef& type, bool number_binding = false);
infer_detail::TyRef session_shape(const infer_detail::TyRef& type);
bool session_shape_within(const infer_detail::TyRef& value, const infer_detail::TyRef& bound);
} // namespace nift::rt
