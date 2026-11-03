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
    if(in.c==100){if(m==M::Pop&&!array_size(s[Source]))return fail("pop: array is empty");
        const Note* note=proto->note(&in-proto->code.data());
        return fail("cannot mutate const array: "+std::string(note?note->text:""));}
    // B1: push is mutation::push (stages 2.1-2.2 there, the same text).
    const auto cycle=[&](const Value& stored){return mutation_failed(fault,proto,in,mutation::detail::cycle_failure(0),
        s[Source].heap(),stored,"");};
    if(m==M::Push){const Note* note=proto->note(&in-proto->code.data());bool refused_cycle=false;
        if(!push_value(s[Source],s[Item],fault,note,&cycle_graph_,&refused_cycle))
            return refused_cycle&&cycle(s[Item]);
        s[Result]=s[Item];return memory_ok(fault);}
    // B1 (rt/mutation.h): stage 2.2, the object's immutability, before bounds.
    if(mutation::refuses_mutation(s[Source])){const Note* note=proto->note(&in-proto->code.data());
        return fail(mutation::check_mutable(s[Source],note?std::string_view(note->text):std::string_view()).text);}
    // B2 (owner B): the arguments are converted with legacy's texts, then the
    // protocol's entry point runs the remaining stages (bounds, cycle for each
    // stored value, budget) and the one commit: there is no second
    // implementation of an array mutator here.
    const Note* note=proto->note(&in-proto->code.data());
    mutation::Env env=mutation_env(note);
    const auto refused=[&](const mutation::Outcome& out,const Value& stored){
        return mutation_failed(fault,proto,in,out,s[Source].heap(),stored,"");};
    std::size_t index=0,count=0;
    if(m==M::Insert||m==M::Remove){if(!natives::number_to_size(s[Item],
        index))return fail(std::string(name)+": invalid index");}
    mutation::Outcome out;
    switch(m) {
        case M::Insert:
            out=mutation::insert(s[Source],index,s[Returned],env);
            if(out.ok())s[Result]=s[Returned];
            break;
        case M::Remove:out=mutation::remove(s[Source],index,&s[Result],env);break;
        case M::Pop:out=mutation::pop(s[Source],&s[Result],env);break;
        case M::Clear:out=mutation::clear(s[Source],env);if(out.ok())s[Result]=Value();break;
        case M::Reverse:out=mutation::reverse(s[Source],env);if(out.ok())s[Result]=s[Source];break;
        case M::Sort:{natives::Context context;context.heap_limit=heap_limit_;  // the only user of a Context
            return collection_plain_sort(s,context,fault)&&memory_ok(fault);}
        case M::Splice:
            // c: 0 the removal, 2 the removal of a splice whose replacement is
            // already evaluated (validated there, before anything changes), 1
            // the insertion (the index the removal recorded).
            if(in.c!=1){if(!natives::number_to_size(s[Item],index)||!natives::number_to_size(s[Returned],
                count))return fail("splice: invalid index/count");
                const auto n=array_size(s[Source]);index=std::min(index,n);count=std::min(count,n-index);
                s[Index]=Value::integer(index);s[Length]=Value::integer(count);
                // a.splice(i, k, a): the replacement is the receiver before the removal.
                if(in.c==2&&s[Keys].is_heap()&&s[Keys].heap()==s[Source].heap())s[Keys]=array_shallow_clone(s[Keys]);
                out=mutation::Protocol::splice_remove(s[Source],index,count,in.c==2?&s[Keys]:nullptr,&s[Result],env);
            } else {
                index=static_cast<std::size_t>(s[Index].as_int());
                if(index>array_size(s[Source]))return internal(fault,"splice receiver changed during replacement");
                out=mutation::Protocol::splice_insert(s[Source],index,s[Keys],env);
            }
            break;
        default:return internal(fault,"array mutation method");
    }
    if(!out.ok())return refused(out,m==M::Insert?s[Returned]:s[Keys]);
    return memory_ok(fault);
}
} // namespace nift::rt
