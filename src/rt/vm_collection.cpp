#include "rt/vm_collection.h"
#include "rt/collection.h"
#include "rt/heap.h"
#include "rt/string.h"
namespace nift::rt {
bool Vm::collection(const Instr& in,Value* R,Fault& fault,const Proto* proto) {
    switch(in.op) {
        case OpCode::CollectionStep:return op_collection_step(in,R,fault);
        case OpCode::CollectionSort:return op_collection_sort(in,R,fault,proto);
        case OpCode::ArrayMutate:return op_array_mutate(in,R,fault,proto);
        default:return internal(fault,"collection opcode");
    }
}
bool Vm::op_collection_step(const Instr& in,Value* R,Fault& fault) {
    if(in.c==99) {
        if(!R[in.a].is(Kind::String))return internal(fault,"collection error-only entry");
        return raise(fault,StopReason::Error,std::string(string_view_of(R[in.a])));
    }
    if(in.b>=static_cast<std::uint16_t>(CollectionMethod::CountMethods))return internal(fault,"collection opcode");
    natives::Context context;context.heap_limit=heap_limit_;
    return collection_step(static_cast<CollectionMethod>(in.b),in.c,R+in.a,context,fault)&&memory_ok(fault);
}
bool Vm::op_collection_call(const Instr& in,Value*& R,const Value*& K,const Proto*& proto,
                            std::size_t& base,const Instr*& pc,Fault& fault) {
    const Value& value=R[in.b];
    const auto* function=value.is(Kind::Function)?value.heap_as<FunctionObject>():nullptr;
    if(!function||!function->code)return raise(fault,StopReason::Error,"transformation: callback must be callable");
    const auto* callee=callable_code(function, proto);
    if(!(in.c&kConstantBit)&&callee->collection_entry)callee=callee->collection_entry;
    if((in.c&0x1fffu)!=callee->nparams)return raise(fault,StopReason::Error,"callback argument count mismatch");
    const std::size_t callee_base=base+in.a;
    if((frames_.size()>=next_frame_check_||callee_base+callee->nregs+1u>stack_.size())&&
       !call_slow(callee,function->is_lambda?nullptr:function->name.c_str(),callee_base,fault))return false;
    if (callee->session_source)
        for (const auto type : callee->session_param_types)
            if (type != 0xffff) stack_[callee_base + type] = Value::integer(-1);
    frames_.push_back(Frame{pc+1,base,proto,closure_,(in.flags&kInstrReturnCall)!=0});
    closure_=function;base=callee_base;proto=callee;R=stack_.data()+base;K=proto->constants.data();
        pc=proto->code.data();
    return true;
}
} // namespace nift::rt
