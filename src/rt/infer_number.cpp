#include "rt/infer_number.h"
#include "rt/literal.h"
#include "rt/number.h"
// rt-lint: cold-file compile-time numeric storage provenance
namespace nift::rt::infer_detail {
TyRef Inference::number_literal(NodeId id) {
    Value v;const Node& n=ast_.at(id);
    if(!decode_number_literal(ast_.text(n),v))return with_text_number(kinds_ty(kKindNumber),true);
    if(v.is(Kind::Decimal))
        return with_text_number(kinds_ty(number_is_integer(v)?kKindInt:kKindFrac),true);
    const TyRef result=constant(id,v.is(Kind::Int)?static_cast<double>(v.as_int()):v.as_double());
    return with_text_number(result,v.is(Kind::Int)&&
                            (v.as_int()>=9007199254740992ll||v.as_int()<=-9007199254740992ll));
}
}
