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
    if(!s[Source].is(Kind::Array))return internal(fault,"array mutation receiver");
    const auto n=array_size(s[Source]);
    if(in.c==100){if(m==M::Pop&&!n)return fail("pop: array is empty");
        const Note* note=proto->note(&in-proto->code.data());
        return fail("cannot mutate const array: "+std::string(note?note->text:""));}
    if(m==M::Push){const Note* note=proto->note(&in-proto->code.data());if(!push_value(s[Source],s[Item],fault,
        note))return false;
        s[Result]=s[Item];return memory_ok(fault);}
    std::size_t index=0,count=0;
    if(m==M::Insert||m==M::Remove){if(!natives::number_to_size(s[Item],
        index))return fail(std::string(name)+": invalid index");
        if(m==M::Insert?index>n:index>=n)return fail(std::string(name)+": index out of range");}
    if(m==M::Splice&&in.c==0){if(!natives::number_to_size(s[Item],index)||!natives::number_to_size(s[Returned],
        count))return fail("splice: invalid index/count");
        index=std::min(index,n);count=std::min(count,n-index);
        s[Index]=Value::integer(index);s[Length]=Value::integer(count);}
    if(m==M::Splice&&in.c==1){if(!s[Keys].is(Kind::Array))return fail("splice: replacements must be an array");
        index=static_cast<std::size_t>(s[Index].as_int());if(index>n)return internal(fault,
        "splice receiver changed during replacement");}
    if(m==M::Pop&&!n)return fail("pop: array is empty");
    if(m==M::Pop)s[Result]=array_items(s[Source]).back();
    if(m==M::Remove)s[Result]=array_items(s[Source])[index];
    if(m==M::Splice&&in.c==0){s[Result]=make_array(count);for(std::size_t k=0;k<count;++k)array_push(s[Result],
        array_items(s[Source])[index+k]);}
    natives::Context context;context.heap_limit=heap_limit_;
    if(m==M::Sort)return collection_plain_sort(s,context,fault)&&memory_ok(fault);
    const std::size_t growth=m==M::Insert?1:m==M::Splice&&in.c==1?array_size(s[Keys]):0;
    const auto capacity=s[Source].heap_as<ArrayObject>()->items.capacity();
    const std::size_t clone=s[Source].refcount()>1?n:0;
    if(growth>std::size_t(-1)/(4*sizeof(Value))-n)return internal(fault,"memory budget exceeded");
    const std::size_t reserve=clone+(growth&&n+growth>capacity?2*(n+growth):0);
    if(reserve&&!natives::reserve_result(context,reserve*sizeof(Value)+128))
        return internal(fault,"memory budget exceeded");
    auto& items=array_mut(s[Source]);
    switch(m) {
        case M::Pop:items.pop_back();break;
        case M::Remove:items.erase(items.begin()+index);break;
        case M::Insert:items.insert(items.begin()+index,s[Returned]);s[Result]=s[Returned];break;
        case M::Clear:items.clear();s[Result]=Value();break;
        case M::Reverse:std::reverse(items.begin(),items.end());s[Result]=s[Source];break;
        case M::Splice:if(in.c==0)items.erase(items.begin()+index,items.begin()+index+count);else{
            const auto& replacement=array_items(s[Keys]);items.insert(items.begin()+index,replacement.begin(),
            replacement.end());}break;

        default:return internal(fault,"array mutation method");
    }
    heap_retally(s[Source].heap());return memory_ok(fault);
}
} // namespace nift::rt
