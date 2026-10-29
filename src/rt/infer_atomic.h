#pragma once
// CP23 result type of an increment/decrement: atomic<int> operations yield an
// int, ordinary numeric locations keep legacy's numeric/text-number kind.
#include "rt/infer_types.h"

namespace nift::rt::infer_detail {

TyRef incdec_result(const TyRef& operand);

}  // namespace nift::rt::infer_detail
