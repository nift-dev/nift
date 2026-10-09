#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <valgrind/callgrind.h>
namespace cp51 {
enum Id {Factory,Instance,Metadata,Capture,Registration,Arguments,Frame,Body,Key,Decoration,Sorting,Result,Cleanup,ReturnKey,Destruction,Count};
inline const char* names[]={"factory","instance","metadata","capture","registration","arguments","frame","body","key","decoration","sorting","result","cleanup","returned_key","destruction"};
struct BindingCounts {unsigned long long entries=0,inserts=0,overwrites=0,value_refs=0,slot_refs=0,root_refs=0,path_vectors=0,path_components=0,path_key_bytes=0,default_slots=0,explicit_slots=0;};
struct Entry {unsigned long long calls=0,allocations=0,bytes=0,frees=0;unsigned depth=0;};
struct Stats {BindingCounts binding[2]; Entry entries[Count]; unsigned long long capture_entries=0,frame_capture_entries=0,comparisons=0,instances=0; const char* selected=std::getenv("CP51_PHASE");unsigned selected_depth=0;
~Stats(){for(int j=0;j<2;++j){const auto& b=binding[j];std::fprintf(stderr,"bindings %s entries=%llu inserts=%llu overwrites=%llu value_refs=%llu slot_refs=%llu root_refs=%llu path_vectors=%llu path_components=%llu path_key_bytes=%llu default_slots=%llu explicit_slots=%llu\n",j?"frame":"capture",b.entries,b.inserts,b.overwrites,b.value_refs,b.slot_refs,b.root_refs,b.path_vectors,b.path_components,b.path_key_bytes,b.default_slots,b.explicit_slots);}
for(int i=0;i<Count;++i){std::fprintf(stderr,"phase %s calls=%llu allocations=%llu bytes=%llu frees=%llu\n",names[i],entries[i].calls,entries[i].allocations,entries[i].bytes,entries[i].frees);}std::fprintf(stderr,"events instances=%llu capture_entries=%llu frame_capture_entries=%llu comparisons=%llu\n",instances,capture_entries,frame_capture_entries,comparisons);}};
inline Stats stats;
inline void binding_slot(bool explicit_slot){for(int j=0;j<2;++j)if(stats.entries[j?Frame:Capture].depth){if(explicit_slot)++stats.binding[j].explicit_slots;else ++stats.binding[j].default_slots;}}
inline void allocation(std::size_t n){for(int i=0;i<Count;++i)if(stats.entries[i].depth){++stats.entries[i].allocations;stats.entries[i].bytes+=n;}}
inline void deallocation(void* p){if(p)for(int i=0;i<Count;++i)if(stats.entries[i].depth)++stats.entries[i].frees;}
struct Phase {Id id;bool open=true,selected=false;Phase(Id x):id(x){++stats.entries[id].calls;++stats.entries[id].depth;selected=stats.selected&&std::strcmp(stats.selected,names[id])==0;if(selected&&stats.selected_depth++==0){CALLGRIND_TOGGLE_COLLECT;}}
void finish(){if(!open)return;if(selected&&--stats.selected_depth==0){CALLGRIND_TOGGLE_COLLECT;}--stats.entries[id].depth;open=false;}~Phase(){finish();}};
template<Id id,class F> __attribute__((noinline)) auto run(F&& f)->decltype(f()){Phase phase(id);return f();}
}
