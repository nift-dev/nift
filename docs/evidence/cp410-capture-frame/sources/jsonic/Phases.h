#pragma once
#include <cstdio>
#include <cstdlib>
#include <new>
#include <valgrind/callgrind.h>
namespace cp51 { enum Id {Factory};
struct Stats {unsigned long long allocations=0,bytes=0,live=0,peak=0,base=0; bool active=false;~Stats(){std::fprintf(stderr,"parse allocations=%llu bytes=%llu peak_payload=%llu\n",allocations,bytes,peak);}};
inline Stats stats;
inline void allocated(std::size_t n){stats.live+=n;if(stats.active){++stats.allocations;stats.bytes+=n;if(stats.live-stats.base>stats.peak)stats.peak=stats.live-stats.base;}}
inline void freed(std::size_t n){stats.live-=n;}
struct Phase {Phase(Id){stats.base=stats.live;stats.active=true;CALLGRIND_TOGGLE_COLLECT;}~Phase(){CALLGRIND_TOGGLE_COLLECT;stats.active=false;}};
}
