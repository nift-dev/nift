// CP16 certification fix (owner Option A, 2026-10-23): secure_random_bytes
// checks the heap budget BEFORE its result buffer exists, fills that buffer in
// place and hands it over as the result (one allocation of `count` bytes, no
// second copy), and never touches the legacy value model.
//
// A replaced operator new records every allocation during the call. The
// adversarial case asks for far more than the remaining budget and requires a
// memory-budget refusal with no allocation anywhere near the requested size.
// Mutant 13 (tests/runtime2/host_mutants.sh) checks the budget only after
// allocating; this test must reject it ("random-budget-before-alloc").
//
// Linked with the CP14-isolated core (no RuntimeValue.cpp, legacy_bridge.cpp
// or render.cpp), so it is also a link-level proof that the Host natives no
// longer need the legacy value bridge.
#include "rt_test.h"

#include "rt/natives/host_ops.h"
#include "rt/natives/support.h"
#include "rt/string.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {
std::size_t g_outstanding = 0, g_peak = 0, g_largest = 0;
constexpr std::size_t kHeader = 16;
} // namespace

#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 11
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"  // malloc/free pairs inside the replacements
#endif

void* operator new(std::size_t n) {
    auto* p = static_cast<unsigned char*>(std::malloc(n + kHeader));
    if (!p) throw std::bad_alloc();
    std::memcpy(p, &n, sizeof n);
    g_outstanding += n;
    if (g_outstanding > g_peak) g_peak = g_outstanding;
    if (n > g_largest) g_largest = n;
    return p + kHeader;
}
void operator delete(void* q) noexcept {
    if (!q) return;
    auto* p = static_cast<unsigned char*>(q) - kHeader;
    std::size_t n;
    std::memcpy(&n, p, sizeof n);
    g_outstanding -= n;
    std::free(p);
}
void operator delete(void* q, std::size_t) noexcept { operator delete(q); }

using namespace nift::rt;
using namespace nift::rt::natives;

namespace {

struct Call {
    Status status;
    Value out;
    std::string error;
    std::size_t peak_extra;
    std::size_t largest;
};

Call random_bytes(std::int64_t count, std::size_t heap_limit) {
    Value args[1] = {Value::integer(count)};
    Context context;
    context.heap_limit = heap_limit;
    Call c{Status::Ok, Value(), {}, 0, 0};
    const std::size_t start = g_outstanding;
    g_peak = start;
    g_largest = 0;
    c.status = host_secure_random_bytes(context, args, 1, c.out, c.error);
    c.peak_extra = g_peak - start;
    c.largest = g_largest;
    return c;
}

constexpr std::size_t kMiB = std::size_t(1) << 20;
constexpr std::size_t kSmall = 64 << 10;  // nothing near the request may be allocated on refusal

// A failed expectation prints its label, so the mutant harness can check that
// the intended regression (not something else) rejected a mutant.
void expect(bool ok, const std::string& label) {
    ++rt_test::checks();
    if (!ok) {
        ++rt_test::failures();
        std::fprintf(stderr, "FAIL %s\n", label.c_str());
    }
}

} // namespace

int main() {
    // 1. Adversarial: 8,000,000 bytes requested under a 1 MiB budget.
    {
        const Call c = random_bytes(8000000, kMiB);
        expect(c.status != Status::Ok, "random-budget-refuses: oversized request must fail");
        expect(c.error.find("memory budget") != std::string::npos,
                     "random-budget-refuses: expected a memory-budget error, got: " + c.error);
        expect(c.largest < kSmall,
                     "random-budget-before-alloc: largest allocation " + std::to_string(c.largest) +
                         " bytes during a refused 8000000-byte request");
        expect(c.peak_extra < kSmall,
                     "random-budget-before-alloc: peak " + std::to_string(c.peak_extra) +
                         " bytes during a refused request");
    }
    // 2. At the maximum size with the budget disabled: exactly one result-sized
    //    buffer (no second copy), random content, the requested length.
    {
        const Call c = random_bytes(10000000, Context::kNoHeapLimit);
        expect(c.status == Status::Ok, "random-max: " + c.error);
        const auto& bytes = bytes_of(c.out);
        expect(bytes.size() == 10000000, "random-max: wrong length");
        expect(c.peak_extra < 10000000 + kSmall,
                     "random-single-buffer: peak " + std::to_string(c.peak_extra) +
                         " bytes for a 10000000-byte result (a second copy was made)");
        std::size_t zero = 0;
        for (std::uint8_t b : bytes) zero += b == 0;
        expect(zero < 200000, "random-content: " + std::to_string(zero) + " zero bytes of 10000000");
    }
    // 3. Fits the budget: succeeds.
    {
        const Call c = random_bytes(1000000, 8 * kMiB);
        expect(c.status == Status::Ok, "random-fits: " + c.error);
        expect(bytes_of(c.out).size() == 1000000, "random-fits: wrong length");
    }
    // 4. Zero bytes, and the unchanged argument errors.
    {
        const Call c = random_bytes(0, kMiB);
        expect(c.status == Status::Ok && bytes_of(c.out).empty(), "random-zero: " + c.error);
        const Call over = random_bytes(10000001, Context::kNoHeapLimit);
        expect(over.status != Status::Ok &&
                         over.error == "secure_random_bytes: result exceeds 10000000 bytes",
                     "random-limit-error: " + over.error);
        expect(over.largest < kSmall, "random-limit-error: allocated before the size check");
        const Call neg = random_bytes(-1, Context::kNoHeapLimit);
        expect(neg.status != Status::Ok &&
                         neg.error == "secure_random_bytes: byte count must be a non-negative integer",
                     "random-negative-error: " + neg.error);
    }
    return rt_test::finish("host-random-budget");
}
