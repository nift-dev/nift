#include "rt/compiler_collection.h"
#include "rt/collection.h"
#include "rt/aggregate.h"
#include "rt/literal.h"
#include "rt/string.h"

// rt-lint: cold-file compile-time collection loop lowering
namespace nift::rt::detail {
int UnitCompiler::collection_method_call(const Node& node,std::uint16_t dst) {
    const Node& callee=ast_.at(node.a);
    const int method=collection_method(text(ast_.at(callee.b)));
    if(method<0||kinds(callee.a)!=kKindArray)return -1;
    return collection_call(node,dst,method)?1:0;
}
bool UnitCompiler::collection_call(const Node& node,std::uint16_t dst,int method_id) {
    using namespace collection_slot;
    const auto method=static_cast<CollectionMethod>(method_id);
    const Node& callee=ast_.at(node.a);
    if(method<CollectionMethod::Map)return array_mutation(node,dst,method_id);
    if(!kinds_within(callee.a,kKindArray,callee,"method"))return false;
    const bool reduce=method==CollectionMethod::Reduce;
    const bool sort_by=method==CollectionMethod::SortBy;
    const bool once=(method>=CollectionMethod::Map&&method<=CollectionMethod::Count)||method==CollectionMethod::GroupBy;
    if((!sort_by&&node.count!=(reduce?2u:1u)) || (sort_by&&(node.count==0||(node.count!=1&&node.count%2)))) {
        std::string message=std::string(collection_names[method_id]);
        if(sort_by)message+=": expected selector or selector/direction pairs";
        else if(once||reduce)message+=": invalid arguments";
        else message+=method==CollectionMethod::Partition?": expected one predicate":": expected one selector";
        return collection_failure(node,dst,std::move(message));
    }
    const NodeId* args=ast_.list(node);
    if(sort_by&&node.count>1)for(std::uint32_t k=1;k<node.count;k+=2) {
        const Node& direction=ast_.at(args[k]);
        if(direction.kind!=NodeKind::String)
            return collection_failure(node,dst,"sort_by: direction must be quoted 'asc' or 'desc'");
        Value decoded;
        if(!decode_string_literal(text(direction),decoded))return fail(node,"method:sort_by");
        const auto value=string_view_of(decoded);
        if(value!="asc"&&value!="desc")
            return collection_failure(node,dst,"sort_by: direction must be 'asc' or 'desc'");
    }
    for(std::uint32_t k=0;k<(sort_by?node.count:1u);k+=(sort_by&&node.count>1?2:1))
        if(!collection_callback_shape(node,args[k],reduce?2:1))return false;
    const std::uint16_t mark=fb_->top(),state=mark;
    for(std::uint16_t k=0;k<Count;++k){std::uint16_t reg=0;if(!temp(reg))return false;fb_->emit(OpCode::LoadNull,reg);}
    if(!expr_to(callee.a,state+Source))return false;
    if(reduce&&!expr_to(args[1],state+Result))return false;
    if(once&&!expr_to(args[0],state+Callback))return false;
    const std::uint32_t selectors=sort_by?(node.count==1?1:node.count/2):1;
    if(sort_by) {
        Value directions=make_array(selectors);
        for(std::uint32_t k=0;k<selectors;++k){
            bool desc=false;
            if(node.count>1){const Node& direction=ast_.at(args[k*2+1]);
                if(direction.kind!=NodeKind::String)return fail(node,"method:sort_by");
                Value decoded;if(!decode_string_literal(text(direction),decoded))return fail(node,"method:sort_by");
                desc=string_view_of(decoded)=="desc";
            }
            array_push(directions,Value::boolean(desc));
        }
        std::uint16_t constant=0;if(!fb_->constant(directions,constant))return false;
        fb_->emit(OpCode::LoadK,state+Directions,constant&0x7fffu);
    }
    fb_->set_offset(collection_offset(node));
    fb_->emit(OpCode::CollectionStep,state,method_id,0);
    const auto loop=fb_->here();
#if defined(NIFT_COLLECTION_MUTANT) && NIFT_COLLECTION_MUTANT == 3
    if(!expr_to(callee.a,state+Source))return false;
#endif
    fb_->emit(OpCode::CollectionStep,state,method_id,1);
    const auto done=fb_->emit(OpCode::JmpFalse,state+More);
    for(std::uint32_t k=0;k<selectors;++k){
#if defined(NIFT_COLLECTION_MUTANT) && NIFT_COLLECTION_MUTANT == 4
        if(once&&!expr_to(args[0],state+Callback))return false;
#endif
        if(!once&&!expr_to(args[sort_by&&node.count>1?k*2:0],state+Callback))return false;
        const std::uint16_t window=fb_->top();
        std::uint16_t reg=0;if(!temp(reg))return false;
        fb_->emit(OpCode::Move,reg,reduce?state+Result:state+Item);
        if(reduce){if(!temp(reg))return false;fb_->emit(OpCode::Move,reg,state+Item);}
        fb_->set_offset(collection_offset(node));
        const auto at=fb_->emit(OpCode::CollectionCall,window,state+Callback,
            (reduce?2:1)|collection_diagnostic_flags(node,args[sort_by&&node.count>1?k*2:0]));
        fb_->note(at,"collection callback");
        fb_->emit(OpCode::Move,state+Returned,window);
        fb_->release_to(window);
        fb_->emit(OpCode::CollectionStep,state,method_id,sort_by?4:2);
    }
    if(sort_by)fb_->emit(OpCode::CollectionStep,state,method_id,5);
    const auto back=fb_->emit(OpCode::Jmp);if(!fb_->patch(back,loop)||!fb_->patch(done,fb_->here()))return builder_ok();
    fb_->emit(OpCode::CollectionStep,state,method_id,3);
    fb_->emit(OpCode::Move,dst,state+Result);
    for(std::uint16_t k=0;k<Count;++k)fb_->emit(OpCode::LoadNull,state+k);
    fb_->release_to(mark);
    return builder_ok();
}
} // namespace nift::rt::detail
