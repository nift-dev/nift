#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <valgrind/callgrind.h>
namespace cp51 {
enum Id {Factory,Instance,Metadata,Capture,Registration,Arguments,Frame,Body,Key,Decoration,Sorting,Result,Cleanup,ReturnKey,Destruction,Count};
inline const char* names[]={"factory","instance","metadata","capture","registration","arguments","frame","body","key","decoration","sorting","result","cleanup","returned_key","destruction"};
struct Entry {unsigned long long calls=0,allocations=0,bytes=0;unsigned depth=0;};
struct Stats {Entry entries[Count]; unsigned long long capture_entries=0,frame_capture_entries=0,comparisons=0,instances=0; const char* selected=std::getenv("CP51_PHASE");unsigned selected_depth=0;
~Stats(){for(int i=0;i<Count;++i)std::fprintf(stderr,"phase %s calls=%llu allocations=%llu bytes=%llu\n",names[i],entries[i].calls,entries[i].allocations,entries[i].bytes);std::fprintf(stderr,"events instances=%llu capture_entries=%llu frame_capture_entries=%llu comparisons=%llu\n",instances,capture_entries,frame_capture_entries,comparisons);}};
inline Stats stats;
inline void allocation(std::size_t n){for(int i=0;i<Count;++i)if(stats.entries[i].depth){++stats.entries[i].allocations;stats.entries[i].bytes+=n;}}
struct Phase {Id id;bool open=true,selected=false;Phase(Id x):id(x){++stats.entries[id].calls;++stats.entries[id].depth;selected=stats.selected&&std::strcmp(stats.selected,names[id])==0;if(selected&&stats.selected_depth++==0){CALLGRIND_TOGGLE_COLLECT;}}
void finish(){if(!open)return;if(selected&&--stats.selected_depth==0){CALLGRIND_TOGGLE_COLLECT;}--stats.entries[id].depth;open=false;}~Phase(){finish();}};
template<Id id,class F> __attribute__((noinline)) auto run(F&& f)->decltype(f()){Phase phase(id);return f();}
}
