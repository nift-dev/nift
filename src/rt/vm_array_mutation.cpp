#include "rt/vm_array_mutation.h"
#include "rt/collection.h"
#include "rt/collection_plain_sort.h"
#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt/natives/support.h"
#include "rt/string.h"
#include <algorithm>
namespace nift::rt {
bool Vm::op_array_mutate(const Instr& in,Value* R,Fault& fault,const Proto* proto) {
    using namespace collection_slot;using M=CollectionMethod;
    Value* s=R+in.a;const auto m=static_cast<M>(in.b);
    const std::string_view name=collection_names[in.b];
    const auto fail=[&](std::string text){return raise(fault,StopReason::Error,std::move(text));};
    // B0 (b0-safety.md): a statement mutator on a loop binding, a selector or a
    // const binding is lowered without a receiver kind proof; a receiver that is
    // not an array at run time is legacy's (and OpCode::Push's) ordinary error.
#if defined(NIFT_SAFETY_MUTANT) && NIFT_SAFETY_MUTANT == 2
    if(!s[Source].is(Kind::Array))return internal(fault,"array mutation receiver");  // S2
#else
    if(!s[Source].is(Kind::Array)){const Note* note=proto->note(&in-proto->code.data());
        return fail(note&&in.c!=100?"unknown value or malformed expression: "+note->text:
            std::string(name)+": receiver is not an array");}
#endif
    const auto n=array_size(s[Source]);
    if(in.c==100){if(m==M::Pop&&!n)return fail("pop: array is empty");
        const Note* note=proto->note(&in-proto->code.data());
        return fail("cannot mutate const array: "+std::string(note?note->text:""));}
    // B1 (rt/mutation.h): stage 2.2, the object's immutability, before bounds.
    {const Note* note=proto->note(&in-proto->code.data());
        const auto out=mutation::check_mutable(s[Source],note?std::string_view(note->text):std::string_view());
        if(!out.ok())return fail(out.text);}
    if(m==M::Push){const Note* note=proto->note(&in-proto->code.data());if(!push_value(s[Source],s[Item],fault,
        note))return false;
        s[Result]=s[Item];return memory_ok(fault);}
    std::size_t index=0,count=0;
    if(m==M::Insert||m==M::Remove){if(!natives::number_to_size(s[Item],
        index))return fail(std::string(name)+": invalid index");
        if(m==M::Insert?index>n:index>=n)return fail(std::string(name)+": index out of range");}
    // c: 0 the removal, 2 the removal of a splice whose replacement is already
    // evaluated (validated here, before anything changes), 1 the insertion.
    if(m==M::Splice&&in.c!=1){if(!natives::number_to_size(s[Item],index)||!natives::number_to_size(s[Returned],
        count))return fail("splice: invalid index/count");
        if(in.c==2&&!s[Keys].is(Kind::Array))return fail("splice: replacements must be an array");
        index=std::min(index,n);count=std::min(count,n-index);
        s[Index]=Value::integer(index);s[Length]=Value::integer(count);}
    if(m==M::Splice&&in.c==1){if(!s[Keys].is(Kind::Array))return fail("splice: replacements must be an array");
        index=static_cast<std::size_t>(s[Index].as_int());if(index>n)return internal(fault,
        "splice receiver changed during replacement");}
    if(m==M::Pop&&!n)return fail("pop: array is empty");
    if(m==M::Pop)s[Result]=array_view(s[Source]).back();
    if(m==M::Remove)s[Result]=array_view(s[Source])[index];
    if(m==M::Splice&&in.c!=1){s[Result]=make_array(count);for(std::size_t k=0;k<count;++k)array_push(s[Result],
        array_view(s[Source])[index+k]);}
    natives::Context context;context.heap_limit=heap_limit_;
    if(m==M::Sort)return collection_plain_sort(s,context,fault)&&memory_ok(fault);
    const std::size_t growth=m==M::Insert?1:m==M::Splice&&in.c==1?array_size(s[Keys]):0;
    // Stage 2.5 (no cycle check: arrays are copied, so acyclic by construction).
    if(!mutation::reserve(heap_limit_,mutation::array_reserve_bytes(s[Source],growth)))
        return internal(fault,"memory budget exceeded");
    using E=mutation::ArrayEdit;
    switch(m) {  // stage 3 + 4: one commit
        case M::Pop:mutation::commit_array(s[Source],E::Pop,0,0,Value());break;
        case M::Remove:mutation::commit_array(s[Source],E::Remove,index,0,Value());break;
        case M::Insert:mutation::commit_array(s[Source],E::Insert,index,0,s[Returned]);s[Result]=s[Returned];break;
        case M::Clear:mutation::commit_array(s[Source],E::Clear,0,0,Value());s[Result]=Value();break;
        case M::Reverse:mutation::commit_array(s[Source],E::Reverse,0,0,Value());s[Result]=s[Source];break;
        case M::Splice:
            if(in.c!=1)mutation::commit_array(s[Source],E::SpliceRemove,index,count,Value());
            else mutation::commit_array(s[Source],E::SpliceInsert,index,0,s[Keys]);
            break;
        default:return internal(fault,"array mutation method");
    }
    return memory_ok(fault);
}
} // namespace nift::rt
