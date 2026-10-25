#include "rt/compiler_array_mutation.h"
#include "rt/collection.h"
#include "rt/collection_effects.h"
// rt-lint: cold-file compile-time bare-array mutation lowering
namespace nift::rt::detail {
bool UnitCompiler::array_mutation(const Node& node,std::uint16_t dst,int id) {
    using namespace collection_slot;using M=CollectionMethod;
    const auto method=static_cast<M>(id);const Node& receiver=ast_.at(ast_.at(node.a).a);
    if(receiver.kind!=NodeKind::Ident)return fail(node,"method:"+std::string(collection_names[id]));
    const Binding* found=find_binding(text(receiver));if(!found)return fail(node,
        "method:"+std::string(collection_names[id]));
    const Binding binding=*found;
    if(method==M::Sort&&node.count==1)return comparator_sort(node,dst,binding);
    const std::uint32_t argc=node.count;
    const bool valid=method==M::Push||method==M::Remove?argc==1:
        method==M::Insert?argc==2:method==M::Splice?argc>=2&&argc<=3:argc==0;
    if(!valid) {
        std::string message=std::string(collection_names[id]);
        if(method==M::Push)message+=": expected one argument";
        else if(method==M::Insert)message+=": expected index and value";
        else if(method==M::Remove)message+=": expected index";
        else if(method==M::Splice)message+=": expected start, delete_count, optional replacement array";
        else if(method==M::Sort)message+=": expected zero or one comparator";
        else message+=": expected no arguments";
        return collection_failure(node,dst,std::move(message));
    }
    if(method==M::Splice&&argc==3&&!collection_sort_effects(ast_,root_,ast_.list(node)[2],node_id(receiver),kinds_,
        false).empty())
        return fail(node,"method:splice");
    const std::uint16_t mark=fb_->top(),state=mark;
    for(std::uint16_t k=0;k<Count;++k){std::uint16_t reg=0;if(!temp(reg))return false;fb_->emit(OpCode::LoadNull,reg);}
    auto read=[&](bool take)->bool {
        std::uint16_t value=0;
        if(binding.global&&!binding.cell){if(take){if(!global_take(binding,value,text(receiver)))return false;}
            else if(!global_read_value(binding.reg,value,text(receiver)))return false;}
        else if(binding.cell||binding.upvalue||binding.selector){
            if(!indexed_binding(binding,value))return false;
            if(take&&!binding.selector&&!binding.loop_binding){std::uint16_t cell=0;if(!binding_cell(binding,
                cell))return false;
                fb_->emit(OpCode::CellTake,value,cell);}
        }else value=binding.reg;
        fb_->emit(OpCode::Move,state+Source,value);
        if(take)fb_->emit(OpCode::LoadNull,value);
        return true;
    };
    auto put=[&]()->bool {
        if(binding.global)global_put(binding,state+Source);
        else if(binding.cell||binding.upvalue||binding.selector){if(!store_indexed_binding(binding,
            state+Source))return false;}
        else fb_->emit(OpCode::Move,binding.reg,state+Source);
        fb_->emit(OpCode::LoadNull,state+Source);return true;
    };
    fb_->set_offset(collection_offset(node));
    if(binding.selector||(binding.loop_binding&&(binding.cell||binding.upvalue))) {
        std::uint16_t refreshed=0;
        if(binding.selector) {if(!for_lazy_read(binding,refreshed))return false;}
        else if(!read_binding(binding,refreshed))return false;
        fb_->emit(OpCode::LoadNull,refreshed);
    }
#if defined(NIFT_COLLECTION_MUTANT) && NIFT_COLLECTION_MUTANT == 11
    if(false){
#else
    if(binding.const_array){
#endif
        if(!read(false))return false;
        const auto at=fb_->emit(OpCode::ArrayMutate,state,id,100);fb_->note(at,std::string(text(receiver)));
    }
    const NodeId* args=ast_.list(node);
    if(argc>0&&!expr_to(args[0],state+Item))return false;
    if(argc>1&&!expr_to(args[1],state+Returned))return false;
    if(!read(true))return false;
    fb_->set_offset(collection_offset(node));fb_->emit(OpCode::ArrayMutate,state,id,0);
    if(!put())return false;
    if(method==M::Splice&&argc==3) {
        if(!expr_to(args[2],state+Keys)||!read(true))return false;
        fb_->set_offset(collection_offset(node));fb_->emit(OpCode::ArrayMutate,state,id,1);
        if(!put())return false;
    }
    fb_->emit(OpCode::Move,dst,state+Result);
    for(std::uint16_t k=0;k<Count;++k)fb_->emit(OpCode::LoadNull,state+k);
    fb_->release_to(mark);return builder_ok();
}
} // namespace nift::rt::detail
