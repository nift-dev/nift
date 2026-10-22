// LD_PRELOAD tracker: peak bytes held in large (>= 256 KiB) malloc blocks.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
static size_t live, peak; static void*(*rm)(size_t); static void(*rf)(void*); static void*(*rr)(void*,size_t);
static void init(void){ if(!rm){ rm=dlsym(RTLD_NEXT,"malloc"); rf=dlsym(RTLD_NEXT,"free"); rr=dlsym(RTLD_NEXT,"realloc"); } }
#define BIG (256u<<10)
void* malloc(size_t n){ init(); void*p=rm(n); if(p&&n>=BIG){ live+=malloc_usable_size(p); if(live>peak)peak=live; } return p; }
void free(void*p){ init(); if(p){ size_t u=malloc_usable_size(p); if(u>=BIG) live-=u; } rf(p); }
void* realloc(void*p,size_t n){ init(); size_t o=p?malloc_usable_size(p):0; void*q=rr(p,n); if(q){ if(o>=BIG) live-=o; size_t u=malloc_usable_size(q); if(u>=BIG){ live+=u; if(live>peak)peak=live; } } return q; }
__attribute__((destructor)) static void fin(void){ fprintf(stderr,"PEAK_LARGE_BYTES %zu\n",peak); }
