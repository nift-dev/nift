#include "rt/vm_collection_sort.h"
#include "rt/collection_sort.h"
#include "rt/natives/support.h"
#include "rt/aggregate.h"
#include "rt/cell.h"
#include "rt/heap.h"
#include "rt/mutation.h"
#include "rt/vm_coro.h"
#include <atomic>
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
// B2 (owner requirement E): the sort-protection registry (rt/vm.h).
namespace {
// Process-wide counts, for tests that check a release across VMs (a parallel
// task's isolate VM): protections currently set, and protections ever set.
std::atomic<std::size_t> live_protections{0}, started_protections{0};
template <class List> void release_at(List& list,std::size_t k) noexcept {
    clear_sort_protected(list[k].receiver.heap());
    list.erase(list.begin()+static_cast<std::ptrdiff_t>(k));
    live_protections.fetch_sub(1,std::memory_order_relaxed);
}
}
std::size_t Vm::live_sort_protections() noexcept {return live_protections.load(std::memory_order_relaxed);}
std::size_t Vm::started_sort_protections() noexcept {return started_protections.load(std::memory_order_relaxed);}
const Value* Vm::sort_protected(std::size_t k) const noexcept {
    return k<sort_protection_.size()?&sort_protection_[k].receiver:nullptr;
}
std::uint32_t Vm::running_task() const noexcept {return coro_?coro_->current:0;}
void Vm::protect_sort(const Value& receiver,std::size_t slot,std::size_t site) {
    sort_protection_.push_back(SortProtection{receiver,running_task(),slot,site});  // may throw: before the bit
    live_protections.fetch_add(1,std::memory_order_relaxed);
    started_protections.fetch_add(1,std::memory_order_relaxed);
#if !(defined(NIFT_SORT_MUTANT) && NIFT_SORT_MUTANT == 5)  // S5: the receiver is never protected
    mark_sort_protected(receiver.heap());
#endif
}
void Vm::release_sort(std::size_t slot) noexcept {
#if !(defined(NIFT_SORT_MUTANT) && NIFT_SORT_MUTANT == 1)  // S1: completion keeps the protection
    const std::uint32_t task=running_task();
    for(std::size_t k=sort_protection_.size();k--;)
        if(sort_protection_[k].task==task&&sort_protection_[k].slot==slot){release_at(sort_protection_,k);return;}
#else
    (void)slot;
#endif
}
// After an unwind: every protection of the running task whose controller is
// no longer active (drained to completion, or its registers unwound).
void Vm::sweep_sort_protection() noexcept {
#if !(defined(NIFT_SORT_MUTANT) && NIFT_SORT_MUTANT == 2)  // S2: an unwind keeps the protection
    const std::uint32_t task=running_task();
    for(std::size_t k=sort_protection_.size();k--;) {
        const SortProtection& p=sort_protection_[k];
        if(p.task!=task)continue;
        const bool live=p.slot+sort_slot::Count<=stack_.size()&&collection_sort_active(stack_.data()+p.slot,p.site);
        if(!live)release_at(sort_protection_,k);
    }
#endif
}
// Every exit of a run (a stop, the end of a session statement, an exception,
// a parallel task's isolate run) and the destructor. A cooperative task never
// holds one across its teardown: a coroutine program with a comparator sort is
// refused under the cooperative model (coro-collection-callback).
void Vm::release_sort_protection() noexcept {
#if !(defined(NIFT_SORT_MUTANT) && NIFT_SORT_MUTANT == 4)  // S4: run exit / destruction keeps the protection
    for(std::size_t k=sort_protection_.size();k--;)release_at(sort_protection_,k);
#endif
}
bool Vm::op_collection_sort(const Instr& in,Value* R,Fault& fault,const Proto* proto) {
    Value* place=source(stack_.data(),R,in);if(!place)return internal(fault,"sort location");
    Value* s=R+in.a;s[Source]=std::move(*place);
    natives::Context context;context.heap_limit=heap_limit_;
    bool ok=true;
    if((in.c&15)==0) {  // B1: stage 2.2 before the sort starts (rt/mutation.h)
        const Note* note=proto->note(&in-proto->code.data());
        const auto out=mutation::check_mutable(s[Source],note?std::string_view(note->text):std::string_view());
        const auto site=static_cast<std::size_t>(&in-proto->code.data());
        if(!out.ok())ok=raise_refusal(fault,out);
        else{protect_sort(s[Source],static_cast<std::size_t>(s-stack_.data()),site);
            ok=collection_sort_init(s,context,fault,site);}}
    else if(!s[Returned].is(Kind::Bool))ok=raise(fault,StopReason::Error,"sort: comparator must return bool");
    else if(!collection_sort_resume(s,s[Returned].as_bool(),false))ok=internal(fault,"sort controller");
    *place=std::move(s[Source]);
    if(!ok)return false;
    if(!s[More].as_bool())release_sort(static_cast<std::size_t>(s-stack_.data()));  // completed
    s[Returned]=Value();
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
