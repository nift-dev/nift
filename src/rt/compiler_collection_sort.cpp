#include "rt/compiler_collection_sort.h"
#include "rt/collection_sort.h"
#include "rt/collection_effects.h"
namespace nift::rt::detail {
bool UnitCompiler::comparator_sort(const Node& node,std::uint16_t dst,const Binding& binding) {
    using namespace collection_slot;
#if !defined(__GLIBCXX__) || __GLIBCXX__ != 20260321
    return fail(node,"method:sort");
#endif
    const Node& receiver=ast_.at(ast_.at(node.a).a);
    if(binding.selector||binding.loop_binding)return fail(node,"method:sort");
    const NodeId receiver_id=node_id(receiver);
    const auto elements=kinds_.collection_elements.find(receiver_id);
    if(elements==kinds_.collection_elements.end()||
       (elements->second&(kKindUnknown|kKindBytes|kKindFunction|kKindError))||
       kinds_.collection_text_numbers.count(receiver_id))return fail(node,"method:sort");
    const NodeId callback=ast_.list(node)[0];
    if(!collection_sort_effects(ast_,root_,callback,node_id(receiver),kinds_).empty())return fail(node,"method:sort");
    const auto mark=fb_->top(),state=mark;
    for(unsigned k=0;k<sort_slot::Count;++k){std::uint16_t reg=0;if(!temp(reg))return false;
    fb_->emit(OpCode::LoadNull,reg);}
    std::uint16_t place=binding.reg,flags=binding.global?0x10:0;
    if(binding.cell||binding.upvalue) {
        if(!binding_cell(binding,place))return false;
        fb_->emit(OpCode::Move,state+Selector,place);flags=0x20;place=0;
    }
    fb_->set_offset(collection_offset(node));
    if(binding.const_array) {
        if(!expr_to(node_id(receiver),state+Source))return false;
        const auto at=fb_->emit(OpCode::ArrayMutate,state,static_cast<unsigned>(CollectionMethod::Sort),100);
        fb_->note(at,std::string(text(receiver)));
    }
    fb_->emit(OpCode::CollectionSort,state,place,flags);
    const auto loop=fb_->here();
    const auto done=fb_->emit(OpCode::JmpFalse,state+More);
    if(!expr_to(callback,state+Callback))return false;
    const auto window=fb_->top();std::uint16_t reg=0;
    if(!temp(reg))return false;
    fb_->emit(OpCode::Move,reg,state+sort_slot::Left);
    if(!temp(reg))return false;
    fb_->emit(OpCode::Move,reg,state+sort_slot::Right);
    fb_->set_offset(collection_offset(node));
    const auto call=fb_->emit(OpCode::CollectionCall,window,state+Callback,
        kConstantBit|2|collection_diagnostic_flags(node,callback,true));
    fb_->note(call,"collection comparator");
    fb_->emit(OpCode::Move,state+Returned,window);fb_->release_to(window);
    fb_->emit(OpCode::CollectionSort,state,place,flags|1);
    const auto back=fb_->emit(OpCode::Jmp);
    if(!fb_->patch(back,loop)||!fb_->patch(done,fb_->here()))return builder_ok();
    if(!expr_to(node_id(receiver),dst))return false;
    for(unsigned k=0;k<sort_slot::Count;++k)fb_->emit(OpCode::LoadNull,state+k);
    fb_->release_to(mark);program_->has_collection_boundaries=true;
    return builder_ok();
}
}
