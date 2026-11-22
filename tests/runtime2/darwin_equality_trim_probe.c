#include <malloc/malloc.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic size_t requests;
static _Atomic size_t released;

// Diagnostic only: ask libmalloc to reclaim cached pages after releasing a
// sufficiently large allocation. Run the same program and keep the RSS gate.
static void free_with_pressure_relief(void *ptr) {
    size_t bytes = ptr ? malloc_size(ptr) : 0;
    malloc_zone_t *zone = bytes >= (1u << 20) ? malloc_zone_from_ptr(ptr) : NULL;
    free(ptr);
    if (zone) {
        atomic_fetch_add_explicit(&requests, 1, memory_order_relaxed);
        size_t reclaimed = malloc_zone_pressure_relief(zone, bytes);
        atomic_fetch_add_explicit(&released, reclaimed, memory_order_relaxed);
    }
}

__attribute__((destructor))
static void report(void) {
    fprintf(stderr, "allocator pressure relief: %zu requests, %zu bytes released\n",
            atomic_load_explicit(&requests, memory_order_relaxed), atomic_load_explicit(&released, memory_order_relaxed));
}

__attribute__((used, section("__DATA,__interpose")))
static const struct { void (*replacement)(void *); void (*original)(void *); }
    hooks[] = {{free_with_pressure_relief, free}};
