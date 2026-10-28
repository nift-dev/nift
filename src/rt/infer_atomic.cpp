#include "rt/infer_atomic.h"

namespace nift::rt::infer_detail {

TyRef incdec_result(const TyRef& operand) {
    if (operand->kinds & kKindAtomic) return kinds_ty(kKindInt);
    return with_text_number(kinds_ty(kKindNumber), operand->legacy_text_number);
}

}  // namespace nift::rt::infer_detail
