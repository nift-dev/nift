// LD_PRELOAD tracker: peak bytes held in large (>= 256 KiB) malloc blocks.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef __FreeBSD__
#include <malloc_np.h>
#include <time.h>
#else
#include <malloc.h>
#endif
static size_t live, peak;
static int invalid;
static void*(*rm)(size_t); static void(*rf)(void*); static void*(*rr)(void*,size_t);
static void init(void){ if(!rm){ rm=dlsym(RTLD_NEXT,"malloc"); rf=dlsym(RTLD_NEXT,"free"); rr=dlsym(RTLD_NEXT,"realloc"); } }
#define BIG (256u<<10)
static void uncharge(size_t usable) {
    if (usable < BIG) return;
    if (usable > live) invalid = 1;
    else live -= usable;
}
void* malloc(size_t n){ init(); void*p=rm(n); size_t u=p?malloc_usable_size(p):0; if(u>=BIG){ live+=u; if(live>peak)peak=live; } return p; }
void free(void*p){ init(); if(p) uncharge(malloc_usable_size(p)); rf(p); }
void* realloc(void*p,size_t n){ init(); size_t o=p?malloc_usable_size(p):0; void*q=rr(p,n); if(q){ uncharge(o); size_t u=malloc_usable_size(q); if(u>=BIG){ live+=u; if(live>peak)peak=live; } } else if(!n && o>=BIG) invalid=1; return q; }
__attribute__((destructor)) static void fin(void) {
#ifdef __FreeBSD__
    // Short-lived children can exit before FreeBSD's RSS statistics clock
    // samples them. Keep the measured process on-CPU, even after a refusal.
    if (getenv("NIFT_TEST_RSS_SAMPLE")) {
        const clock_t start = clock();
        if (start != (clock_t)-1) {
            clock_t now;
            do { now = clock(); }
            while (now != (clock_t)-1 && now - start < CLOCKS_PER_SEC / 25);
        }
    }
#endif
    if (invalid) fputs("invalid large allocation tracker accounting\n", stderr);
    else fprintf(stderr, "PEAK_LARGE_BYTES %zu\n", peak);
}
