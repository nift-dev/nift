// Inject a single allocation failure at every allocation in bounded real VM runs.
#include "rt_vm_tool.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/source_map.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
namespace { long countdown=-1,allocations=0; bool enabled=false,hit=false; }
void* operator new(std::size_t n) {
    if(enabled){++allocations;if(countdown==0){countdown=-1;hit=true;throw std::bad_alloc();}
        if(countdown>0)--countdown;}
    if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
// CP14B comparator profile (owner decision 2026-10-25): the same frozen
// libstdc++ check as the production gate (src/rt/compiler_collection_sort.cpp).
// Elsewhere every comparator program must be refused with exactly method:sort;
// that refusal is reported as UNVERIFIED COMPARATOR COVERAGE, never a pass.
#if defined(__GLIBCXX__) && __GLIBCXX__ == 20260321
constexpr bool kComparatorProfileVerified=true;
#else
constexpr bool kComparatorProfileVerified=false;
#endif
int main() {
    using namespace nift::rt;
    const char* cases[]={
        "a:=[1,2]; b:=a; a.push([3,4]); a.splice(1,1,[[9]]); print(a.size()); print(b.size())",
        "a:=[3,2,1]; b:=a; a.sort(); print(a.size()); print(b.size())",
        "a:=range(20); r:=a.map(x=>[x,x]); print(r.size())",
        "a:=range(20); r:=a.filter(x=>x%2==0); print(r.size())",
        "a:=range(20); r:=a.sort_by(x=>0-x); print(r.size())",
        "a:=range(20); r:=a.group_by(x=>x%3); print(r.size())",
        "a:=range(20); r:=a.unique_by(x=>[x%3]); print(r.size())",
        "a:=[5,4,3,2,1]; saved:=[0]; fn(cmp(x,y)){ saved=a; return x<y }; a.sort(cmp); print(a.size())",
        "a:=[{\"k\":5},{\"k\":4},{\"k\":3}]; fn(cmp(x,y)){ throw error(\"stop\") }; try { a.sort(cmp) } catch(e) { print(e.message) }; print(a.size())"
    };
    long total=0;int unverified=0;
    for(const char* source:cases) {
        auto c=rt_vm_tool::compile(source);
        const bool comparator=std::string(source).find("a.sort(cmp)")!=std::string::npos;
        if(comparator&&!kComparatorProfileVerified) {
            if(c.program||c.reason!="method:sort"){std::fprintf(stderr,"FAIL expected the exact method:sort refusal, got %s\n",c.program?"a compiled program":c.reason.c_str());return 1;}
            ++unverified;continue;
        }
        if(!c.program){std::fprintf(stderr,"refusal %s\n",c.reason.c_str());return 1;}
        auto map=SourceMap::single("allocation.f",source);VmOptions options;options.source_map=&map;
        struct Probe {long count;bool hit,ok;};
        auto probe=[&](long index)->Probe {
            int channel[2];if(pipe(channel)!=0)return {0,false,false};
            const pid_t child=fork();
            if(child==0) {
                close(channel[0]);Probe answer{0,false,false};
                const auto baseline=live_heap_bytes()-heap_charged_bytes();
                bool escaped=false;StopReason reason=StopReason::None;
                {
                    Vm vm(*c.program,options);std::string output;
                    countdown=index;hit=false;allocations=0;enabled=true;
                    try {auto result=vm.run(output);enabled=false;reason=result.reason;}
                    catch(const std::bad_alloc&) {enabled=false;escaped=true;}
                    answer.count=allocations;answer.hit=hit;
                }
                answer.ok=(index<0?reason==StopReason::None&&!escaped:
                    hit&&(escaped||reason==StopReason::Internal))&&
                    live_heap_bytes()-heap_charged_bytes()==baseline;
                const auto written=write(channel[1],&answer,sizeof(answer));close(channel[1]);
                _exit(written==sizeof(answer)?0:2);
            }
            close(channel[1]);Probe answer{0,false,false};
            const auto received=read(channel[0],&answer,sizeof(answer));close(channel[0]);
            int status=0;if(child<0||waitpid(child,&status,0)!=child||
                !WIFEXITED(status)||WEXITSTATUS(status)!=0||received!=sizeof(answer))
                return {0,false,false};
            return answer;
        };
        // Every child starts from identical pre-run atom/COW state, including
        // cold intern insertions. A prior injection cannot shift later sites.
        const auto control=probe(-1);
        if(!control.ok||control.count<=0){std::fputs("cold control failed\n",stderr);return 1;}
        for(long index=0;index<control.count;++index) {
            const auto result=probe(index);
            if(!result.ok||!result.hit) {
                std::fprintf(stderr,"allocation %ld/%ld failed fatal-stop or owned-heap check\n",
                    index,control.count);return 1;
            }
            ++total;
        }
    }
    std::printf("PASS %ld allocation sites: fatal failure and owned heap released (thread atom charges separate)\n",total);
    if(unverified)std::printf("SKIP UNVERIFIED COMPARATOR COVERAGE: allocation: %d comparator workload(s) refused with method:sort on an unverified libstdc++ profile\n",unverified);
}
