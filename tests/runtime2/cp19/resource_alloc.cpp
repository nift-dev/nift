#include "rt/session.h"
#include "rt/aggregate.h"
#include "rt_test.h"
#include "rt/handles/handle_box.h"
#include <filesystem>
#include <fstream>
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
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
    const auto path=root/"p";{std::ofstream file(path);file<<"original";}
    const std::string seed="f:=file(\""+path.generic_string()+"\");f.open(\"rw\")";
    const std::string probe="f.write(\"dirty\");tick:=1";
    long total=0,before=0,after=0,success=0;
    {
        VmSession session;std::string output;auto r=session.execute(seed,"seed",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None);
        allocations=0;counting=true;r=session.execute(probe,"probe",output);counting=false;total=allocations;
        CHECK(r.admitted && r.execution.reason==StopReason::None);
        CHECK(session.finish(true).reason==StopReason::None);
    }
    for(long n=1;n<=total+1;++n) {
        VmSession session;std::string output;auto r=session.execute(seed,"seed",output);
        CHECK(r.admitted && r.execution.reason==StopReason::None);
        auto original=session.value("f");auto* box=original.heap_as<HandleBoxObject>();
        auto* table=static_cast<handles::HandleTable*>(box->table);
        fail_at=n;allocations=0;counting=true;r=session.execute(probe,"probe",output);
        counting=false;const bool failed=fail_at==-1;fail_at=-1;
        CHECK(r.legacy_executions==0 && output.empty());
        if(failed && !r.admitted) {
            ++before;CHECK(r.execution.reason==StopReason::Internal && r.vm_executions==0);
            CHECK(session.value("f").heap()==original.heap());CHECK(!session.contains("tick"));
            r=session.execute("f.close()","recover",output);CHECK(r.admitted && r.execution.reason==StopReason::None);
        } else if(failed && r.execution.reason==StopReason::Internal) {
            ++after;CHECK(r.vm_executions==1 && !session.contains("f"));CHECK(table->stats().live==0);
        } else {
            ++success;CHECK(r.admitted && r.execution.reason==StopReason::None && r.vm_executions==1);
            r=session.execute("f.revert();f.close()","finish",output);
            CHECK(r.admitted && r.execution.reason==StopReason::None);
        }
        original=Value();session.close();
        std::ifstream input(path);std::string contents((std::istreambuf_iterator<char>(input)),{});
        CHECK(contents=="original");
    }
    CHECK(before>0 && after>0 && success>0);
    std::cout<<"RESOURCE-ALLOCATION-SWEEP points="<<total<<" before="<<before<<" admitted="<<after<<" success="<<success<<'\n';
    return rt_test::finish("CP19 resource allocation containment");
}
