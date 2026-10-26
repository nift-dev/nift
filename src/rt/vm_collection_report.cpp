#include "rt/vm_collection_report.h"
#include "rt/error.h"
#include "rt/heap.h"
#include <sstream>

// rt-lint: cold-file final uncaught collection diagnostic projection
namespace nift::rt {
namespace {
std::uint32_t return_site(const Proto* proto,const Instr* pc,std::uint32_t fallback) {
    for(const Instr* at=pc;at<proto->code.data()+proto->code.size();++at)
        if(at->op==OpCode::Return||at->op==OpCode::ReturnNull||at->op==OpCode::TopReturn)
            return proto->offsets[static_cast<std::size_t>(at-proto->code.data())];
    return fallback;
}
bool source_view(const FunctionObject* function,const Proto* proto,const Proto* main,const Instr* entry) {
    if(!function)return proto!=main;
    if(!function->is_lambda)return true;
    if(function->selector_call_site)return false;
    return !entry||entry->op!=OpCode::CollectionCall||!(entry->c&0x2000);
}
bool block_view(VmResult& result,const Note* note,const SourceMap* map) {
    constexpr char prefix[]="collection comparator block ";
    if(!note||!map||result.diagnostic_frames.empty()||note->text.rfind(prefix,0)!=0)return false;
    std::istringstream encoded(note->text.substr(sizeof(prefix)-1));
    std::uint32_t begin=0,end=0;
    if(!(encoded>>begin>>end)||result.offset<begin||result.offset>=end)return false;
    const auto body=map->at(begin),failure=map->at(result.offset);
    if(body.path!=failure.path||failure.line<body.line||
       (failure.line==body.line&&failure.column<body.column))return false;
    auto& display=result.diagnostic_frames.front().site;
    display.source=failure.path;display.line=failure.line-body.line+1;
    display.column=failure.column-(failure.line==body.line?body.column-1:0);
    result.report_origin = {display.source, static_cast<std::uint32_t>(display.line),
                            static_cast<std::uint32_t>(display.column)};
    return true;
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
        if((it->ret-1)->op==OpCode::CollectionCall)
            result.diagnostic_frames.push_back(
                {nift::detail::DiagnosticFrameKind::Callback,"collection callback",{}, {}});
    }
    if(result.reason!=StopReason::Error||fault.message.rfind("callable recursion depth exceeded",0)==0)return;
    result.message=fault.catchable&&fault.error.is(Kind::Error)?
        fault.error.heap_as<ErrorObject>()->message:fault.message;
    bool own_origin=source_view(closure_,proto,protos_.front(),frames_.empty()?nullptr:frames_.back().ret-1);
    bool missing_view=closure_&&closure_->selector_call_site;
    if(closure_&&closure_->is_lambda&&(closure_->expression_body||proto->collection_expression_view))
        result.message="lambda body error: "+result.message;
    if((pc->flags&kInstrReturnCall)&&
       !(closure_&&closure_->is_lambda&&(closure_->expression_body||proto->collection_expression_view))) {
        result.message="return: "+result.message;
        if(!result.error_origin)result.offset=return_site(proto,pc,result.offset);
    }
    for(auto it=frames_.rbegin();it!=frames_.rend();++it) {
        const Instr* call=it->ret-1;bool moved=false;
        const bool display=!result.diagnostic_frames.empty()&&
            !result.diagnostic_frames.front().site.source.empty();
        if(!result.error_origin&&!display&&call->op==OpCode::CollectionCall) {
            const auto at=static_cast<std::size_t>(call-it->proto->code.data());
            if(block_view(result,it->proto->note(at),
                it->proto->session_source ? it->proto->session_source.get() : options_.source_map)) {
                own_origin=true;result.offset=it->proto->offsets[at];
            }else if(!own_origin&&((call->c&0x4000)||missing_view)) {
                result.offset=it->proto->offsets[at];moved=true;
            }
        }
        if(it->return_call) {
            result.message="return: "+result.message;
            if(!result.error_origin&&!own_origin){result.offset=return_site(it->proto,call,result.offset);moved=true;}
        }
        if (moved && it->proto->session_source)
            result.report_origin = it->proto->session_source->at(result.offset);
        if(moved) {
            auto parent=it;++parent;
            own_origin=source_view(it->closure,it->proto,protos_.front(),
                parent==frames_.rend()?nullptr:parent->ret-1);
            missing_view=it->closure&&it->closure->selector_call_site;
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
