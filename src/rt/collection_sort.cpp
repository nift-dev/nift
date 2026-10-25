#include "rt/collection_sort.h"
#include "rt/natives/support.h"
#include "rt/aggregate.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/string.h"
#include <algorithm>
#include <cstring>
#include <new>
namespace nift::rt {
namespace {
using namespace collection_slot;
struct State {
    std::uint64_t magic=0x4350313442534f52ull;
    std::size_t site=0;
    std::size_t n=0,half=0,lo=0,hi=0,chunk=0,step=7,at=0;
    std::size_t i=0,j=0,k=0,end=0,mid=0,start=0;
    unsigned stage=0,pass=0,op=0,phase=0;
    bool srcbuf=false,dstbuf=false,pending=false,done=false;
};
State& state(Value* s){return *reinterpret_cast<State*>(s[HashTable].heap_as<BytesObject>()->data.data());}
std::vector<Value>& raw(Value& v){return v.heap_as<ArrayObject>()->items;}
Value moved_empty(Value* s,const Value& v) {
    if(v.is(Kind::Object))return s[sort_slot::EmptyObject];
    if(v.is(Kind::Array))return s[sort_slot::EmptyArray];
    if(v.is(Kind::String))return s[sort_slot::EmptyString];
    // RuntimeValue's number/bool/null move leaves its scalar representation.
    return v;
}
void move(Value* s,std::vector<Value>& src,std::size_t i,std::vector<Value>& dst,std::size_t j) {
    if(&src==&dst&&i==j)return;
    Value empty=moved_empty(s,src[i]);
    dst[j]=std::move(src[i]);src[i]=std::move(empty);
}
void save(Value* s,std::size_t i) {
    auto& a=raw(s[Source]);Value empty=moved_empty(s,a[i]);s[Item]=std::move(a[i]);a[i]=std::move(empty);
}
void restore(Value* s,std::size_t i){raw(s[Source])[i]=std::move(s[Item]);}
bool request(Value* s,State& t,const Value& l,const Value& r) {
    s[sort_slot::Left]=l;s[sort_slot::Right]=r;s[More]=Value::boolean(true);t.pending=true;return true;
}
// Generate the same fixed chunk/pass operations as libstdc++'s full-buffer
// __stable_sort_adaptive. Comparison-dependent work stays in resume below.
bool next(Value* s,State& t) {
    for(;;) {
        if(t.stage<2) {
            t.lo=t.stage? (t.n+1)/2:0;t.hi=t.stage?t.n:(t.n+1)/2;
            if(t.pass==0) {
                if(t.chunk<t.hi-t.lo){t.start=t.lo+t.chunk;t.end=std::min(t.start+7,t.hi);t.chunk+=7;t.op=1;
                    t.i=t.start+1;t.phase=0;return true;}
                t.pass=1;t.step=7;t.at=0;
            }
            if(t.step<t.hi-t.lo) {
                const std::size_t length=t.hi-t.lo;
                if(t.at<length) {
                    t.srcbuf=t.pass==2;t.dstbuf=!t.srcbuf;
                    const std::size_t origin=t.srcbuf?0:t.lo;
                    t.i=origin+t.at;t.mid=std::min(t.i+t.step,origin+length);
                    t.j=t.mid;t.end=std::min(t.i+2*t.step,origin+length);
                    t.k=(t.dstbuf?0:t.lo)+t.at;t.at+=2*t.step;t.op=2;t.phase=0;return true;
                }
                t.step*=2;t.at=0;t.pass=t.pass==1?2:1;continue;
            }
            // libstdc++ always completes the paired buffer -> array pass,
            // even when that pass's step already spans the entire half.
            if(t.pass==2&&t.at==0) {
                t.srcbuf=true;t.dstbuf=false;t.i=0;t.mid=std::min(t.step,t.hi-t.lo);t.j=t.mid;t.end=t.hi-t.lo;t.k=t.lo;
                t.op=2;t.phase=0;t.pass=3;return true;
            }
            ++t.stage;t.chunk=0;t.pass=0;continue;
        }
        if(t.stage==2) {
            auto& a=raw(s[Source]);auto& b=raw(s[Keys]);const auto mid=(t.n+1)/2;
            if(mid<=t.n-mid) {
                for(std::size_t q=0;q<mid;++q)move(s,a,q,b,q);
                t.i=0;t.j=mid;t.k=0;t.mid=mid;t.end=t.n;t.op=3;
            } else {
                for(std::size_t q=0;q<t.n-mid;++q)move(s,a,mid+q,b,q);
                // Reverse cursors are one past the element, avoiding -1.
                t.i=mid;t.j=t.n-mid;t.k=t.n;t.op=4;
            }
            t.stage=3;t.phase=0;return true;
        }
        t.done=true;t.op=0;s[More]=Value::boolean(false);return false;
    }
}
}
bool collection_sort_init(Value* s,const natives::Context& context,Fault& fault,std::size_t site) {
    if(!s[Source].is(Kind::Array))return raise(fault,StopReason::Internal,"sort receiver");
    const auto n=array_size(s[Source]);
    if(n>static_cast<std::size_t>(-1)/(4*sizeof(Value))||!natives::reserve_result(context,
        sizeof(State)+(2*n+(n+1)/2)*sizeof(Value)+512))
        return raise(fault,StopReason::Internal,"memory budget exceeded");
    // Allocation is allowed here and before a request, never in drain.
    array_mut(s[Source]);
    s[HashTable]=make_bytes({});auto& bytes=s[HashTable].heap_as<BytesObject>()->data;bytes.resize(sizeof(State));
    new(bytes.data()) State();heap_retally(s[HashTable].heap());
    s[Keys]=make_array((n+1)/2);raw(s[Keys]).resize((n+1)/2);
    s[Directions]=make_array(n);raw(s[Directions]).resize(n);
    mark_may_hold_identity(s[Directions].heap());
    mark_may_hold_identity(s[Keys].heap());
    // Keep each input graph alive through drain: releasing a dying broad
    // aggregate may otherwise grow the runtime destruction worklist. This
    // scratch holds elements, never the source array (which would force COW).
    s[sort_slot::KeepAlive]=make_array(n);
    for(const Value& value:array_items(s[Source]))array_push(s[sort_slot::KeepAlive],value);
    s[sort_slot::EmptyObject]=make_object();s[sort_slot::EmptyArray]=make_array();
        s[sort_slot::EmptyString]=make_string("");
    auto& t=state(s);t.n=n;t.site=site;
    if(n<2){t.done=true;s[More]=Value::boolean(false);return true;}
    return collection_sort_resume(s,false,false);
}
bool collection_sort_active(const Value* s,std::size_t site) {
    if(!s[HashTable].is(Kind::Bytes))return false;
    const auto& bytes=s[HashTable].heap_as<BytesObject>()->data;
    if(bytes.size()!=sizeof(State))return false;
    const auto& t=*reinterpret_cast<const State*>(bytes.data());
    return t.magic==0x4350313442534f52ull&&t.site==site&&!t.done;
}
bool collection_sort_resume(Value* s,bool answer,bool draining) {
    auto& t=state(s);
    if(t.done){s[More]=Value::boolean(false);return true;}
    if(!s[Source].is(Kind::Array)||array_size(s[Source])!=t.n)return false;
    // A callback may keep an ordinary D-040 value copy of the receiver. Detach
    // into the already reserved spare without allocating or changing that copy.
    // refs includes Source only: callers transfer the receiver in and out.
    if(s[Source].heap()->refcount>1) {
        if(!s[Directions].is(Kind::Array)||array_size(s[Directions])!=t.n)return false;
        auto& spare=raw(s[Directions]);const auto& a=raw(s[Source]);
        for(std::size_t i=0;i<t.n;++i)spare[i]=a[i];
        s[Source]=std::move(s[Directions]);
    }
    const bool responding=t.pending;t.pending=false;
    s[sort_slot::Left]=Value();s[sort_slot::Right]=Value();
    bool supplied=responding;
    for(;;) {
        if(!t.op&&!next(s,t))return true;
        auto& a=raw(s[Source]);auto& b=raw(s[Keys]);
        if(t.op==1) {
            if(t.i>=t.end){t.op=0;continue;}
            if(t.phase==0) {
                t.phase=1;return request(s,t,a[t.i],a[t.start]);
            }
            if(t.phase==1) {
                if(!supplied)return false;
                supplied=false;
                if(answer){save(s,t.i);for(auto q=t.i;q>t.start;--q)move(s,a,q-1,a,q);restore(s,t.start);++t.i;
                    t.phase=0;continue;}
                save(s,t.i);t.j=t.i-1;t.k=t.i;t.phase=2;
                return request(s,t,s[Item],a[t.j]);
            }
            if(!supplied)return false;
                supplied=false;
            if(answer){move(s,a,t.j,a,t.k);t.k=t.j;if(t.j==0)return false;--t.j;return request(s,t,s[Item],a[t.j]);}
            restore(s,t.k);++t.i;t.phase=0;continue;
        }
        if(t.op==2) {
            auto& src=t.srcbuf?b:a;auto& dst=t.dstbuf?b:a;
            if(t.phase) {
                if(!supplied)return false;
                supplied=false;t.phase=0;
                if(answer)move(s,src,t.j++,dst,t.k++);else move(s,src,t.i++,dst,t.k++);
            }
            if(t.i<t.mid&&t.j<t.end){t.phase=1;return request(s,t,src[t.j],src[t.i]);}
            while(t.i<t.mid)move(s,src,t.i++,dst,t.k++);
            while(t.j<t.end)move(s,src,t.j++,dst,t.k++);
            t.op=0;continue;
        }
        if(t.op==3) {
            if(t.phase){if(!supplied)return false;
                supplied=false;t.phase=0;if(answer)move(s,a,t.j++,a,t.k++);else move(s,b,t.i++,a,t.k++);}
            if(t.i<t.mid&&t.j<t.end){t.phase=1;return request(s,t,a[t.j],b[t.i]);}
            while(t.i<t.mid)move(s,b,t.i++,a,t.k++);
            t.op=0;continue;
        }
        if(t.op==4) {
            if(t.phase){if(!supplied)return false;
                supplied=false;t.phase=0;--t.k;if(answer)move(s,a,--t.i,a,t.k);else move(s,b,--t.j,a,t.k);}
            if(t.i&&t.j){t.phase=1;return request(s,t,b[t.j-1],a[t.i-1]);}
            while(t.j)move(s,b,--t.j,a,--t.k);
            t.op=0;continue;
        }
        (void)draining;return false;
    }
}
}
