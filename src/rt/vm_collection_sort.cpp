#include "rt/vm_collection_sort.h"
#include "rt/collection_sort.h"
#include "rt/natives/support.h"
#include "rt/aggregate.h"
#include "rt/cell.h"
#include "rt/heap.h"
#include <new>
namespace nift::rt {
#ifdef NIFT_COLLECTION_ALLOC_TEST
void collection_drain_allocation_guard(bool);
struct DrainAllocationGuard {
    DrainAllocationGuard(){collection_drain_allocation_guard(true);}
    ~DrainAllocationGuard(){collection_drain_allocation_guard(false);}
};
#endif
namespace {
using namespace collection_slot;
// c: low nibble init/resume, 0x10 global direct slot, 0x20 cell.
Value* source(Value* stack,Value* R,const Instr& in) {
    if(!(in.c&0x20))return (in.c&0x10?stack:R)+in.b;
    Value anchor=cell_anchor(R[in.a+Selector]);
    auto* cell=cell_of(anchor);
    // Selector writeback can allocate. Those locations remain a compile-time
    // refusal until a no-allocation path is established; never guess in drain.
    return cell&&cell->binding_type!=kSelectorCell?&cell->value:nullptr;
}
}
bool Vm::op_collection_sort(const Instr& in,Value* R,Fault& fault,const Proto* proto) {
    Value* place=source(stack_.data(),R,in);if(!place)return internal(fault,"sort location");
    Value* s=R+in.a;s[Source]=std::move(*place);
    natives::Context context;context.heap_limit=heap_limit_;
    bool ok=true;
    if((in.c&15)==0)ok=collection_sort_init(s,context,fault,static_cast<std::size_t>(&in-proto->code.data()));
    else if(!s[Returned].is(Kind::Bool))ok=raise(fault,StopReason::Error,"sort: comparator must return bool");
    else if(!collection_sort_resume(s,s[Returned].as_bool(),false))ok=internal(fault,"sort controller");
    *place=std::move(s[Source]);
    if(!ok)return false;
    s[Returned]=Value();
    if(s[More].as_bool()&&s[Directions].is_null()) {
        const auto n=array_size(*place);
        if(!natives::reserve_result(context,sizeof(ArrayObject)+n*sizeof(Value)))return internal(fault,
            "memory budget exceeded");
        s[Directions]=make_array(n);s[Directions].heap_as<ArrayObject>()->items.resize(n);
        mark_may_hold_identity(s[Directions].heap());
    }
    return memory_ok(fault);
}
bool Vm::drain_collection_boundaries(const Fault&,std::size_t retained_depth,
                                     const Proto* proto,std::size_t base,const Instr* pc) {
#ifdef NIFT_COLLECTION_ALLOC_TEST
    DrainAllocationGuard allocation_guard;
#endif
#if defined(NIFT_COLLECTION_MUTANT) && NIFT_COLLECTION_MUTANT == 1
    return false;
#elif defined(NIFT_COLLECTION_MUTANT) && NIFT_COLLECTION_MUTANT == 2
    throw std::bad_alloc();
#elif defined(NIFT_COLLECTION_MUTANT) && NIFT_COLLECTION_MUTANT == 9
    return true;
#endif
    auto frame=[&](const Proto* p,std::size_t b,const Instr* active) {
        Value* R=stack_.data()+b;
        // The controller's initial instruction is immutable call-site data.
        // Hidden registers identify the particular activation, including
        // recursive uses of the same site. Reverse order is inside-out.
        for(std::size_t j=p->code.size();j--;) {
            const Instr& in=p->code[j];
            if(in.op!=OpCode::CollectionSort||(in.c&15)!=0)continue;
            const auto active_at=static_cast<std::size_t>(active-p->code.data());
            if(active_at<j)continue;
            std::size_t end=j+1;
            for(;end<p->code.size();++end) {
                const Instr& last=p->code[end];
                if(last.op==OpCode::CollectionSort&&last.a==in.a&&(last.c&15)==1)break;
            }
            if(end==p->code.size()||active_at>end+1)continue;
            Value* s=R+in.a;
            if(!collection_sort_active(s,j))continue;
            if(!s[HashTable].is(Kind::Bytes)||!s[More].is(Kind::Bool)||!s[More].as_bool())continue;
            Value* place=source(stack_.data(),R,in);if(!place)return false;
            s[Source]=std::move(*place);
            bool ok=true;
            do {if(!collection_sort_resume(s,false,true)){ok=false;break;}}while(s[More].as_bool());
            *place=std::move(s[Source]);
            if(!ok)return false;
        }
        return true;
    };
    std::size_t depth=frames_.size();
    if(depth>=retained_depth&&!frame(proto,base,pc))return false;
    while(depth>retained_depth) {
        const Frame& saved=frames_[--depth];
        if(!frame(saved.proto,saved.base,saved.ret-1))return false;
    }
    return true;
}
}
