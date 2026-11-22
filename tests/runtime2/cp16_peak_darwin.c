// Darwin allocator interposition for the CP16 large-block peak checks.
#include <malloc/malloc.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define BIG (256u << 10)

// Only remove allocations observed by this tracker. Darwin can free blocks
// allocated by libSystem before the interposer loaded or through other APIs.
struct allocation { const void* pointer; size_t size; };
static struct allocation allocations[1024];
static atomic_flag tracking = ATOMIC_FLAG_INIT;
static size_t live, peak;
static int overflowed;

static void lock_tracker(void) {
    while (atomic_flag_test_and_set_explicit(&tracking, memory_order_acquire)) {}
}

static void unlock_tracker(void) {
    atomic_flag_clear_explicit(&tracking, memory_order_release);
}

static void added(const void* pointer) {
    const size_t size = pointer ? malloc_size(pointer) : 0;
    if (size < BIG) return;
    lock_tracker();
    struct allocation* slot = allocations;
    const struct allocation* end = allocations + sizeof allocations / sizeof allocations[0];
    while (slot != end && slot->pointer) ++slot;
    if (slot == end) overflowed = 1;
    else {
        *slot = (struct allocation){pointer, size};
        live += size;
        if (live > peak) peak = live;
    }
    unlock_tracker();
}

static void removed(const void* pointer) {
    if (!pointer || malloc_size(pointer) < BIG) return;
    lock_tracker();
    for (size_t i = 0; i < sizeof allocations / sizeof allocations[0]; ++i) {
        if (allocations[i].pointer == pointer) {
            live -= allocations[i].size;
            allocations[i].pointer = NULL;
            break;
        }
    }
    unlock_tracker();
}

static void* tracked_malloc(size_t size) {
    void* pointer = malloc(size);
    added(pointer);
    return pointer;
}

static void* tracked_calloc(size_t count, size_t size) {
    void* pointer = calloc(count, size);
    added(pointer);
    return pointer;
}

static void tracked_free(void* pointer) {
    removed(pointer);
    free(pointer);
}

static void* tracked_realloc(void* pointer, size_t size) {
    removed(pointer);
    void* result = realloc(pointer, size);
    if (result) added(result);
    else if (size != 0) added(pointer);  // failed realloc retained the old buffer
    return result;
}

#define INTERPOSE(replacement, original)                                      \
    __attribute__((used, section("__DATA,__interpose")))                       \
    static const struct { const void* replacement_ptr; const void* original_ptr; } \
        interpose_##original = { (const void*)&replacement, (const void*)&original }

INTERPOSE(tracked_malloc, malloc);
INTERPOSE(tracked_calloc, calloc);
INTERPOSE(tracked_free, free);
INTERPOSE(tracked_realloc, realloc);

__attribute__((destructor)) static void report(void) {
    lock_tracker();
    const int failed = overflowed;
    const size_t measured = peak;
    unlock_tracker();
    if (failed) fprintf(stderr, "peak tracker ran out of allocation slots\n");
    else fprintf(stderr, "PEAK_LARGE_BYTES %zu\n", measured);
}
