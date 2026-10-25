// Run the combined caught-unwind hook with operator new forbidden in its scope.
#include "rt_vm_tool.h"
#include "rt/source_map.h"
#include <cstdlib>
#include <cstdio>
#include <new>
namespace { bool forbidden=false; unsigned attempts=0,drains=0; }
void* operator new(std::size_t n) {
    if(forbidden){++attempts;throw std::bad_alloc();}
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
namespace nift::rt {
void collection_drain_allocation_guard(bool enabled){forbidden=enabled;if(enabled)++drains;}
}
int main() {
    using namespace nift::rt;
    const char* cases[]={
        "a := [{\"k\":[5,4]},{\"k\":[3,2]},{\"k\":[1,0]}]; fn(cmp(x,y)) { throw error(\"graph\") }; try { a.sort(cmp) } catch(e) { print(e.message) }; print(a.stringify())",
        "a := [5,4,3,2,1]; n := 0; fn(cmp(x,y)) { n+=1; if(n==2) { throw error(\"stop\") }; return x<y }; try { a.sort(cmp) } catch(e) { print(e.message) }; print(a.stringify())",
        "a := [5,4,3,2,1]; saved := [0]; fn(cmp(x,y)) { saved=a; throw error(\"stop\") }; try { a.sort(cmp) } catch(e) { print(e.message) }; print(saved.stringify()); print(a.stringify())",
        "a := [5,4,3,2,1]; fn(cmp(x,y)) { try { throw error(\"inside\") } catch(e) { }; return x<y }; a.sort(cmp); print(a.stringify())"
    };
    try {
        for(const char* source:cases) {
            auto c=rt_vm_tool::compile(source);
            if(!c.program){std::fprintf(stderr,"refusal: %s\n",c.reason.c_str());return 1;}
            auto map=SourceMap::single("drain.f",source);VmOptions options;options.source_map=&map;
            Vm vm(*c.program,options);std::string out;auto r=vm.run(out);
            if(r.reason!=StopReason::None){std::fprintf(stderr,"VM: %s\n",r.message.c_str());return 1;}
            std::fputs(out.c_str(),stdout);
        }
    } catch(...) {forbidden=false;std::fputs("exception escaped allocation guard\n",stderr);return 1;}
    if(!drains||attempts){std::fprintf(stderr,"drains=%u allocation attempts=%u\n",drains,attempts);return 1;}
    std::printf("PASS %u caught-hook entries, zero allocation attempts\n",drains);
}
