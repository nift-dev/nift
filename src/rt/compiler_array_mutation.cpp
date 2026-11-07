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
    // B2 (b2-refrep.md): the receiver is read, never taken: it stays in its
    // holder while the instruction mutates the shared array in place, so a
    // refused mutation (an ordinary error, a caught cycle) leaves every
    // holder intact. The cycle check is the instruction's own stage 2.4.
    auto read=[&]()->bool {
        std::uint16_t value=0;
        if(binding.global&&!binding.cell){if(!global_read_value(binding.reg,value,text(receiver)))return false;}
        else if(binding.cell||binding.upvalue){if(!indexed_binding(binding,value))return false;}
        else value=binding.reg;
        fb_->emit(OpCode::Move,state+Source,value);
        return true;
    };
    // A captured loop binding writes the element back (its cell's own store;
    // the array itself was changed in place).
    auto put=[&]()->bool {
        if(binding.loop_binding&&(binding.cell||binding.upvalue)){
            if(!store_indexed_binding(binding,state+Source))return false;}
        fb_->emit(OpCode::LoadNull,state+Source);return true;
    };
    fb_->set_offset(collection_offset(node));
    if(binding.loop_binding&&(binding.cell||binding.upvalue)) {
        std::uint16_t refreshed=0;
        if(!read_binding(binding,refreshed))return false;
        fb_->emit(OpCode::LoadNull,refreshed);
    }
#if defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 7  // R-B7 (b5 mutants): args a binding-only check again
    if(binding.const_array&&binding.invocation){
        if(!read())return false;
        const auto at=fb_->emit(OpCode::ArrayMutate,state,id,100);fb_->note(at,std::string(text(receiver)));
    }
#endif
    // B5 (D-NEXT-refsem-b, owner Q1/Q2): no static const-array mutation check. `const` is
    // binding-only; `args` is immutable as an object (kImmutable, checked by the protocol
    // through every alias).
    const NodeId* args=ast_.list(node);
    // H-A (combined-b3-b5.md): the receiver is evaluated first and its value retained in
    // state+Source (a counted reference) while the arguments run, as legacy's canonical
    // evaluator does: an argument that rebinds the receiver's name, or drops every other
    // reference to the array, does not change which array is mutated.
#if !(defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 13)
    if(!read())return false;
#endif
    if(argc>0&&!expr_to(args[0],state+Item))return false;
    if(argc>1&&!expr_to(args[1],state+Returned))return false;
    // D-NEXT-refsem #4, review round 4 (coordinator ruling): every splice
    // evaluates all its arguments (the replacement included), then validates
    // (bounds, replacement kind, cycle: the instruction's stage 2.4), then commits
    // with one store. A throwing argument leaves the receiver unchanged,
    // captured or not (legacy removes first: an approved difference).
    const bool splice3=method==M::Splice&&argc==3;
    const bool cell=(binding.cell||binding.upvalue)&&!binding.global;
#if defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 22
    const bool late=splice3&&cell;  // R22: a captured splice stores its removal before the replacement
#elif defined(NIFT_REFSEM_MUTANT) && NIFT_REFSEM_MUTANT == 23
    const bool late=splice3&&!cell;  // R23: only a captured splice evaluates its arguments first
#else
    const bool late=false&&cell;
#endif
    if(splice3&&!late&&!expr_to(args[2],state+Keys))return false;
#if defined(NIFT_B5_MUTANT) && NIFT_B5_MUTANT == 13
    if(!read())return false;  // M13: the receiver is read after its arguments again (H-A)
#endif
    // The call text: the VM's wrong-receiver message (B0, vm_array_mutation.cpp).
    fb_->set_offset(collection_offset(node));
    fb_->note(fb_->emit(OpCode::ArrayMutate,state,id,splice3&&!late?2:0),std::string(text(node)));
    if(splice3) {
        if(late&&(!put()||!expr_to(args[2],state+Keys)||!read()))return false;
        fb_->set_offset(collection_offset(node));fb_->emit(OpCode::ArrayMutate,state,id,1);
    }
    if(!put())return false;
    fb_->emit(OpCode::Move,dst,state+Result);
    for(std::uint16_t k=0;k<Count;++k)fb_->emit(OpCode::LoadNull,state+k);
    fb_->release_to(mark);return builder_ok();
}
} // namespace nift::rt::detail
