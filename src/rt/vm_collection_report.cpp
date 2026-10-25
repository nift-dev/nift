#include "rt/vm_collection_report.h"
#include "rt/error.h"
#include "rt/heap.h"

// rt-lint: cold-file final uncaught collection diagnostic projection
namespace nift::rt {
namespace {
std::uint32_t return_site(const Proto* proto,const Instr* pc,std::uint32_t fallback) {
    for(const Instr* at=pc;at<proto->code.data()+proto->code.size();++at)
        if(at->op==OpCode::Return||at->op==OpCode::ReturnNull)
            return proto->offsets[static_cast<std::size_t>(at-proto->code.data())];
    return fallback;
}
}
void Vm::collection_error_context(VmResult& result,const Fault& fault,const Proto* proto,const Instr* pc) const {
    // Entry and result validation can fail before a callback frame exists
    // or after it has returned. Private collection opcodes retain that context.
    bool collection=pc->op==OpCode::CollectionCall||pc->op==OpCode::CollectionStep||
        pc->op==OpCode::CollectionSort||pc->op==OpCode::ArrayMutate;
    for(const Frame& frame:frames_)collection=collection||(frame.ret-1)->op==OpCode::CollectionCall;
    if(!collection)return; // Ordinary CP13B reporting is unchanged.
    for(auto it=frames_.rbegin();it!=frames_.rend();++it) {
        if((it->ret-1)->op==OpCode::CollectionCall&& !((it->ret-1)->c&kConstantBit))
            result.diagnostic_frames.push_back(
                {nift::detail::DiagnosticFrameKind::Callback,"collection callback",{}, {}});
    }
    if(result.reason!=StopReason::Error||fault.message.rfind("callable recursion depth exceeded",0)==0)return;
    result.message=fault.catchable&&fault.error.is(Kind::Error)?
        fault.error.heap_as<ErrorObject>()->message:fault.message;
    bool named_origin=closure_&&!closure_->is_lambda;
    if(closure_&&closure_->is_lambda&&(closure_->expression_body||proto->collection_expression_view))
        result.message="lambda body error: "+result.message;
    if((pc->flags&kInstrReturnCall)&&closure_&&!closure_->is_lambda) {
        result.message="return: "+result.message;
        if(!result.error_origin)result.offset=return_site(proto,pc,result.offset);
    }
    for(auto it=frames_.rbegin();it!=frames_.rend();++it) {
        const Instr* call=it->ret-1;
        if(!result.error_origin&&!named_origin&&call->op==OpCode::CollectionCall&&(call->c&0x4000))
            result.offset=it->proto->offsets[static_cast<std::size_t>(call-it->proto->code.data())];
        if(it->return_call) {
            result.message="return: "+result.message;
            if(!result.error_origin&&!named_origin)result.offset=return_site(it->proto,call,result.offset);
        }
        if(it->closure&&it->closure->is_lambda&&
           (it->closure->expression_body||it->proto->collection_expression_view))
            result.message="lambda body error: "+result.message;
    }
#if defined(NIFT_COLLECTION_MUTANT) && NIFT_COLLECTION_MUTANT == 10
    result.message="lambda body error: "+result.message;
#endif

}
} // namespace nift::rt
