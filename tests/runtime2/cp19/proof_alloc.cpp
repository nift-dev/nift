#include "rt/session.h"
#include "rt/aggregate.h"
#include "rt_test.h"
#include <cstdlib>
#include <new>
#include <iostream>
namespace {
thread_local long fail_at=-1, allocations=0;
thread_local bool counting=false;
void* allocate(std::size_t size) {
    if(counting) {
        ++allocations;
        if(fail_at>0 && --fail_at==0) { fail_at=-1;  throw std::bad_alloc(); }
    }
    if(void* ptr=std::malloc(size?size:1))return ptr;
    throw std::bad_alloc();
}
}
void* operator new(std::size_t n){return allocate(n);}
void* operator new[](std::size_t n){return allocate(n);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {try{return allocate(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&) noexcept {try{return allocate(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&) noexcept{std::free(p);}
void operator delete[](void* p,const std::nothrow_t&) noexcept{std::free(p);}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete[](void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
void operator delete[](void* p,std::size_t) noexcept{std::free(p);}
using namespace nift::rt;
int main() {
    const std::string seed="a:=[1];fn(g(p)){return p(0)+a[0]};warm:=g(v=>v+1)";
    const std::string probe="a.push(\"bad\");print(\"ONCE\")";
    long total=0;
    {
        VmSession session;std::string output;
        auto r=session.execute(seed,"seed",output);CHECK(r.admitted && r.execution.reason==StopReason::None);
        allocations=0;counting=true;r=session.execute(probe,"probe",output);counting=false;total=allocations;
        CHECK(r.admitted && r.execution.reason==StopReason::None && output=="ONCE\n");
    }
    long before=0,after=0,successful=0,recovered=0;
    for(long n=1;n<=total+1;++n) {
        VmSession session;std::string output;
        auto r=session.execute(seed,"seed",output);CHECK(r.admitted && r.execution.reason==StopReason::None);
        const auto original=session.value("g");
        fail_at=n;allocations=0;counting=true;
        r=session.execute(probe,"probe",output);
        counting=false;const bool failed=fail_at==-1;fail_at=-1;
        CHECK(r.legacy_executions==0);
        CHECK(output.empty() || output=="ONCE\n");
        if(failed && !r.admitted) {
            ++before;CHECK(r.vm_executions==0 && r.execution.reason==StopReason::Internal);
            CHECK(output.empty() && session.contains("a") && array_size(session.value("a"))==1);
            CHECK(session.value("g").heap()==original.heap());
            r=session.evaluate("g(v=>v+2)","recover",output);
            CHECK(r.admitted && r.execution.reason==StopReason::None && r.value.as_int()==3);
        } else if(failed && r.execution.reason==StopReason::None) {
            // std::stable_partition's optional nothrow scratch buffer can fail safely.
            ++recovered;CHECK(r.vm_executions==1 && output=="ONCE\n");
            CHECK(array_size(session.value("a"))==2 && session.value("g").heap()==original.heap());
            r=session.evaluate("g(v=>v+2)","recover",output);
            CHECK(r.admitted && r.execution.reason==StopReason::None && r.value.as_int()==3);
        } else if(failed) {
            ++after;
            CHECK(r.vm_executions==1 && r.execution.reason==StopReason::Internal);
            CHECK(!session.contains("a"));  // admitted exception containment closes the damaged VM
        } else {
            ++successful;CHECK(r.admitted && r.execution.reason==StopReason::None && output=="ONCE\n");
        }
    }
    CHECK(before>0 && after>0 && successful>0);
    std::cout<<"ALLOCATION-SWEEP points="<<total<<" before="<<before<<" admitted="<<after
             <<" recovered="<<recovered<<" success="<<successful<<'\n';
    return rt_test::finish("CP19 summary allocation containment");
}
