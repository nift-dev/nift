#include "rt/compiler_collection_metadata.h"
#include "rt/collection.h"
#include "rt/string.h"
// rt-lint: cold-file compile-time callback source-view routing
namespace nift::rt::detail {
std::uint32_t UnitCompiler::collection_offset(const Node& node) const {
    const Node* outer=print_arg_;
    while(outer&&(outer->kind==NodeKind::Call||outer->kind==NodeKind::Member||outer->kind==NodeKind::Index))
        outer=&ast_.at(outer->a);
    if(!loop_reachable()&&outer&&outer->kind==NodeKind::Array&&node.begin>=outer->begin&&node.end<=outer->end)
        return outer->begin;
    return node.begin;
}
bool UnitCompiler::collection_failure(const Node& node,std::uint16_t dst,std::string message) {
    const auto mark=fb_->top();std::uint16_t receiver=0,error=0,constant=0;
    if(!temp(receiver)||!expr_to(ast_.at(node.a).a,receiver)||!temp(error)||
       !fb_->constant(make_string(message),constant))return false;
    fb_->emit(OpCode::LoadK,error,constant&0x7fffu);fb_->set_offset(collection_offset(node));
    fb_->emit(OpCode::CollectionStep,error,static_cast<unsigned>(CollectionMethod::Map),99);
    // Compile ignored operands after the terminal guard to retain whole-unit
    // callable/Host/suspension admission without evaluating them at runtime.
    for(std::uint32_t k=0;k<node.count;++k) {
        std::uint16_t ignored=0;
        if(!temp(ignored)||!expr_to(ast_.list(node)[k],ignored))return false;
    }
    fb_->emit(OpCode::LoadNull,dst);fb_->release_to(mark);return builder_ok();
}

std::uint16_t UnitCompiler::collection_diagnostic_flags(const Node& node,NodeId callback,bool comparator) const {
    if(ast_.at(callback).kind!=NodeKind::Lambda)return 0;
    bool text_site=comparator||collection_offset(node)!=node.begin;
    for(NodeId id=0;id<ast_.size();++id) {
        const Node& parent=ast_.at(id);
        if((parent.kind==NodeKind::Member||parent.kind==NodeKind::Index)&&parent.a==node_id(node))text_site=true;
    }
    return 0x2000|(text_site?0x4000:0);
}
}
