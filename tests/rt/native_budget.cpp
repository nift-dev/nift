// Memory investigation C3 (permanent regression): natives that build results
// larger than their inputs check the heap budget before every substantial
// allocation (natives/support.h: reserve_result, BudgetedText). Before the
// fix join built its whole result and only then met the budget (memcheck:
// a 16x overshoot of the configured budget).
//
// A replaced operator new records every allocation (size, outstanding bytes,
// peak) and can be told to fail.
#include "rt_test.h"

#include "rt/aggregate.h"
#include "rt/heap_account.h"
#include "rt/natives/array_methods.h"
#include "rt/natives/string_methods.h"
#include "rt/natives/support.h"
#include "rt/string.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {
std::atomic<bool> g_fail{false};
std::size_t g_outstanding = 0, g_peak = 0, g_largest = 0;
long g_live = 0;
constexpr std::size_t kHeader = 16;
} // namespace

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"  // malloc/free pairs inside the replacements
#endif

void* operator new(std::size_t n) {
    if (g_fail.load(std::memory_order_relaxed)) throw std::bad_alloc();
    auto* p = static_cast<unsigned char*>(std::malloc(n + kHeader));
    if (!p) throw std::bad_alloc();
    std::memcpy(p, &n, sizeof n);
    g_outstanding += n;
    if (g_outstanding > g_peak) g_peak = g_outstanding;
    if (n > g_largest) g_largest = n;
    ++g_live;
    return p + kHeader;
}
void operator delete(void* q) noexcept {
    if (!q) return;
    auto* p = static_cast<unsigned char*>(q) - kHeader;
    std::size_t n;
    std::memcpy(&n, p, sizeof n);
    g_outstanding -= n;
    --g_live;
    std::free(p);
}
void operator delete(void* q, std::size_t) noexcept { operator delete(q); }

using namespace nift::rt;
using namespace nift::rt::natives;

namespace {

constexpr std::size_t kSlack = 96 << 10;  // support.h: unchecked growth bound per call

struct Call {
    Status status;
    Value out;
    std::string error;
    std::size_t peak_extra;  // peak outstanding bytes above the start of the call
    std::size_t largest;     // largest single allocation during the call
};

Call join(const Value& array, const Value& separator, std::size_t heap_limit) {
    Value args[2] = {array, separator};
    Context context;
    context.heap_limit = heap_limit;
    Call c{Status::Ok, Value(), {}, 0, 0};
    const std::size_t start = g_outstanding;
    g_peak = start;
    g_largest = 0;
    c.status = arr_join(context, args, 2, c.out, c.error);
    c.peak_extra = g_peak - start;
    c.largest = g_largest;
    return c;
}

Value strings(std::size_t count, std::size_t length) {
    Value a = make_array(count);
    for (std::size_t i = 0; i < count; ++i) array_push(a, make_string(std::string(length, char('a' + i % 26))));
    return a;
}

Call replace(const Value& text, const Value& pattern, const Value& replacement, std::size_t heap_limit) {
    Value args[3] = {text, pattern, replacement};
    Context context;
    context.heap_limit = heap_limit;
    Call c{Status::Ok, Value(), {}, 0, 0};
    const std::size_t start = g_outstanding;
    g_peak = start;
    g_largest = 0;
    c.status = str_replace(context, args, 3, c.out, c.error);
    c.peak_extra = g_peak - start;
    c.largest = g_largest;
    return c;
}

} // namespace

int main() {
    // 1. Overflow-checked sizes and the shared check.
    CHECK(size_add(1, 2) == 3);
    CHECK(size_add(kSizeOverflow - 1, 1) == kSizeOverflow);
    CHECK(size_add(kSizeOverflow, 5) == kSizeOverflow);
    CHECK(size_add(kSizeOverflow / 2 + 1, kSizeOverflow / 2 + 1) == kSizeOverflow);
    CHECK(size_mul(0, kSizeOverflow) == 0);
    CHECK(size_mul(kSizeOverflow, 0) == 0);
    CHECK(size_mul(std::size_t(1) << 32, std::size_t(1) << 32) == kSizeOverflow);
    Context unlimited;
    CHECK(reserve_result(unlimited, 1000));
    CHECK(!reserve_result(unlimited, kSizeOverflow));          // overflowed: refused even unbudgeted
    CHECK(!reserve_result(unlimited, kMaxResultBytes));        // beyond any std::string
    Context tight;
    tight.heap_limit = live_heap_bytes() + 4096;
    CHECK(reserve_result(tight, 1000));
    CHECK(!reserve_result(tight, 8192));
    Context spent;
    spent.heap_limit = 1;  // live total already above it
    CHECK(!reserve_result(spent, 0));

    // 2. Zero-length cases: empty array, empty separator, empty elements.
    {
        Call c = join(make_array(), make_string(","), 0);
        CHECK(c.status == Status::Ok && string_view_of(c.out).empty());
        Value empties = make_array();
        for (int i = 0; i < 1000; ++i) array_push(empties, make_string(""));
        c = join(empties, make_string(""), 0);
        CHECK(c.status == Status::Ok && string_view_of(c.out).empty());
        c = join(empties, make_string("-"), 0);
        CHECK(c.status == Status::Ok && string_view_of(c.out).size() == 999);
    }

    // 3. A large join over the budget is refused before the buffer outgrows
    //    the headroom: 64 x 64 KiB = 4 MiB of result, 1 MiB of headroom.
    {
        const Value big = strings(64, 64 << 10);
        const std::size_t live = live_heap_bytes(), headroom = 1 << 20;
        const long allocs = g_live;
        Call c = join(big, make_string(""), live + headroom);
        CHECK(c.status == Status::Memory);
        CHECK(c.peak_extra <= headroom);         // never held more than the budget allowed
        CHECK(live_heap_bytes() == live);        // nothing counted, nothing kept
        CHECK(g_live == allocs);                 // the partial buffer was freed
        // Within budget: same result as unbudgeted, peak within the headroom.
        Call ok = join(big, make_string("|"), live + (16 << 20));
        Call ref = join(big, make_string("|"), 0);
        CHECK(ok.status == Status::Ok && ref.status == Status::Ok);
        CHECK(string_view_of(ok.out) == string_view_of(ref.out));
        CHECK(string_view_of(ok.out).size() == 64 * (64 << 10) + 63);
        CHECK(ok.peak_extra <= (16u << 20));
    }

    // 4. Large separators: the separators alone exceed the budget.
    {
        const Value many = strings(4096, 1);
        const std::string sep(4096, ',');
        const std::size_t live = live_heap_bytes();
        Call c = join(many, make_string(sep), live + (2 << 20));  // result ~16 MiB
        CHECK(c.status == Status::Memory);
        CHECK(c.peak_extra <= (2u << 20));
    }

    // 5. Repeated expansion: s = [s, s].join("") until the budget refuses;
    //    the live total never passes the budget plus the bounded slack.
    {
        const std::size_t limit = live_heap_bytes() + (8 << 20);
        Value s = make_string(std::string(1000, 'x'));
        int steps = 0;
        Status last = Status::Ok;
        while (steps < 40) {
            Value pair = make_array(2);
            array_push(pair, s);
            array_push(pair, s);
            const Value none = make_string("");
            const std::size_t live = live_heap_bytes();
            CHECK(live <= limit + kSlack);
            Call c = join(pair, none, limit);
            last = c.status;
            CHECK(c.peak_extra <= (live < limit ? limit - live : 0) + kSlack);
            if (c.status != Status::Ok) break;
            s = c.out;
            CHECK(live_heap_bytes() <= limit + kSlack);
            ++steps;
        }
        CHECK(last == Status::Memory);
        CHECK(steps >= 10 && steps < 40);  // 1000 * 2^12 = 4 MiB fits; 8 MiB plus its operands does not
    }

    // 6. Allocation failure inside the native: bad_alloc propagates (the VM
    //    reports "out of memory"), nothing leaks, the count is unchanged.
    {
        const Value big = strings(8, 64 << 10);
        const std::size_t live = live_heap_bytes();
        const long allocs = g_live;
        bool threw = false;
        g_fail = true;
        try {
            Value out;
            std::string error;
            Value args[2] = {big, make_string("")};
            Context context;
            arr_join(context, args, 2, out, error);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        g_fail = false;
        CHECK(threw);
        CHECK(live_heap_bytes() == live);
        CHECK(g_live == allocs);
    }

    // 7. Element errors keep their legacy message and order.
    {
        Value mixed = make_array();
        array_push(mixed, make_string("a"));
        array_push(mixed, make_array());
        Call c = join(mixed, make_string(","), 0);
        CHECK(c.status == Status::Error && c.error == "join: elements must be renderable scalar values");
    }
    // 8. replace: large replacements over the budget are refused before the
    //    buffer outgrows the headroom; within budget the result is exact.
    {
        const Value text = make_string(std::string(1 << 16, 'a'));       // 65,536 matches of "a"
        const Value wide = make_string(std::string(64, 'b'));            // 4 MiB result
        const std::size_t live = live_heap_bytes();
        const long allocs = g_live;
        Call c = replace(text, make_string("a"), wide, live + (1 << 20));
        CHECK(c.status == Status::Memory);
        CHECK(c.peak_extra <= (1u << 20));
        CHECK(live_heap_bytes() == live && g_live == allocs);
        Call ok = replace(text, make_string("a"), wide, live + (16 << 20));
        CHECK(ok.status == Status::Ok && string_view_of(ok.out).size() == (std::size_t(1) << 22));
        // Zero-length pattern: legacy's error, nothing built.
        Call empty = replace(text, make_string(""), wide, 0);
        CHECK(empty.status == Status::Error && empty.error == "replace: old string must not be empty");
        // Shrinking needs at most the input's size; no match allocates nothing.
        Call shrink = replace(text, make_string("aa"), make_string(""), live_heap_bytes() + (128 << 10));
        CHECK(shrink.status == Status::Ok && string_view_of(shrink.out).empty());
        Call none = replace(text, make_string("z"), wide, live + 1);  // tight, but nothing to build
        CHECK(none.status == Status::Ok && none.out.heap() == text.heap());
    }
    // 9. Repeated expansion through replace: s = s.replace("x", "xx").
    {
        const std::size_t limit = live_heap_bytes() + (8 << 20);
        Value s = make_string(std::string(1000, 'x'));
        int steps = 0;
        Status last = Status::Ok;
        while (steps < 40) {
            const std::size_t live = live_heap_bytes();
            CHECK(live <= limit + kSlack);
            Call c = replace(s, make_string("x"), make_string("xx"), limit);
            last = c.status;
            CHECK(c.peak_extra <= (live < limit ? limit - live : 0) + kSlack);
            if (c.status != Status::Ok) break;
            s = c.out;
            ++steps;
        }
        CHECK(last == Status::Memory);
        CHECK(steps >= 10 && steps < 40);
    }
    return rt_test::finish("native-budget");
}
