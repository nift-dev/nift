// Accounting follow-up to memory investigation C1 (review item 4): the exact
// side table for objects of 1 GiB or more used a std::unordered_map, so
// heap_retally (noexcept, called after every resize) could allocate and
// terminate when memory was exhausted. The table is now a fixed array; when
// it is full the accounting reports itself over any budget (SIZE_MAX), never
// an under-count.
//
// Built with -DNIFT_RT_SATURATED_SLOTS=1 (one slot) so that the overflow is
// reached with two 1 GiB reservations (address space only, never touched;
// run under ulimit -v in test-rt-value).
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/value.h"
#include "rt_test.h"

#include <atomic>
#include <cstdlib>
#include <limits>
#include <new>
#include <string>

namespace {
std::atomic<bool> g_fail{false};
std::atomic<long> g_failed{0};
} // namespace

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"  // malloc/free pairs inside the replacements
#endif

void* operator new(std::size_t n) {
    if (g_fail.load(std::memory_order_relaxed)) {
        g_failed.fetch_add(1, std::memory_order_relaxed);
        throw std::bad_alloc();
    }
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace nift::rt;

namespace {

constexpr std::size_t kGiB = std::size_t(1) << 30;

// A string object whose capacity is reserved (allocation happens here, with
// the failing allocator off) but not yet counted.
StringObject* reserved(std::size_t capacity) {
    auto* so = new StringObject(std::string());
    so->text.reserve(capacity);
    return so;
}

// Counts `so` with every allocation failing: must not throw or terminate.
void retally_failing(HeapObject* so) {
    g_failed = 0;
    g_fail = true;
    heap_retally(so);
    g_fail = false;
    CHECK(g_failed.load() == 0);  // nothing even tried to allocate
}

} // namespace

int main() {
    const std::size_t base = live_heap_bytes();
    // 1. A saturated object enters the side table without allocating.
    StringObject* a = reserved(kGiB + 4096);
    retally_failing(a);
    Value va = Value::adopt(a);
    CHECK(heap_saturated_objects() == 1);
    CHECK(!heap_accounting_overflowed());
    CHECK(live_heap_bytes() - base == heap_footprint(a));
    CHECK(live_heap_bytes() - base > kGiB);

    // 2. A second one finds the (one-slot) table full: no allocation, and the
    //    accounting reports over any budget instead of under-counting.
    StringObject* b = reserved(kGiB + 8192);
    retally_failing(b);
    Value vb = Value::adopt(b);
    CHECK(heap_accounting_overflowed());
    CHECK(live_heap_bytes() == std::numeric_limits<std::size_t>::max());

    // 3. Releasing both (with allocation failing) neither throws nor clears
    //    the overflow: it stays sticky for the thread.
    g_fail = true;
    va = Value();
    vb = Value();
    g_fail = false;
    CHECK(heap_saturated_objects() == 0);
    CHECK(heap_accounting_overflowed());
    CHECK(live_heap_bytes() == std::numeric_limits<std::size_t>::max());
    return rt_test::finish("heap-account-oom");
}
