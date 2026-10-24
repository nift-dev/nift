// CP13B Phase A: Error value ownership and release under the 8 MB VM budget.
//
// Test-only. It exercises the exact ownership pattern the Phase B design uses
// for catches: make_error creates the Error, a Fault-like slot takes ownership,
// the catch-like slot takes it from there, and the next iteration overwrites
// (releases) it. It then checks that the accounted live rt heap returns to the
// baseline and never exceeds the budget. `leak` retains every 10th Error and
// must make the test fail: a passing normal run therefore does detect
// accumulation. The Phase B VM-level negative control is separate: a build
// that deliberately retains a handler or catch value must fail stress.py.
//
// Build with run_error_lifecycle.sh (it compiles this file with error.cpp,
// heap_account.cpp, value.cpp and string.cpp; no production file changes).
#include "rt/error.h"
#include "rt/heap_account.h"
#include "rt/value.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <sys/resource.h>

namespace {

constexpr int kCount = 1000000;
constexpr std::size_t kBudget = 8000000;

bool make_one(nift::rt::Value& out) {
    std::string error;
    return nift::rt::make_error("x", "user.raised", nift::rt::Value(), "", 0, 0, out, error);
}

int lifecycle(bool leak, std::size_t& base, std::size_t& peak, std::size_t& end) {
    using nift::rt::Value;
    base = nift::rt::live_heap_bytes();
    peak = base;
    std::vector<Value> retained;
    if (leak) retained.reserve(kCount / 10);
    Value fault;
    Value catch_slot;
    for (int i = 0; i < kCount; ++i) {
        Value error;
        if (!make_one(error)) return 1;
        fault = std::move(error);          // Throw moves the Error into the Fault
        catch_slot = std::move(fault);     // the catch moves it into its register
        if (leak && i % 10 == 0) retained.push_back(catch_slot);
        const std::size_t live = nift::rt::live_heap_bytes();
        peak = std::max(peak, live);
        catch_slot = Value();              // the next iteration overwrites the register
        if (live > kBudget) {
            std::fprintf(stderr, "accounted heap exceeded %zu at iteration %d: %zu\n", kBudget, i, live);
            return 1;
        }
    }
    catch_slot = Value();
    fault = Value();
    retained.clear();
    end = nift::rt::live_heap_bytes();
    return end == base ? 0 : 2;
}

} // namespace

int main(int argc, char** argv) {
    const bool leak = argc > 1 && std::strcmp(argv[1], "leak") == 0;
    std::size_t base = 0, peak = 0, end = 0;
    const int status = lifecycle(leak, base, peak, end);
    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    std::printf("mode=%s base=%zu peak=%zu end=%zu maxrss_kib=%ld\n", leak ? "leak" : "normal",
                base, peak, end, usage.ru_maxrss);
    if (leak) {
        std::printf("leak probe %s (it must fail the 8 MB budget)\n", status ? "failed as expected" : "did not fail");
        return status ? 1 : 0;
    }
    if (status != 0) {
        std::fprintf(stderr, "lifecycle check failed (status %d)\n", status);
        return 1;
    }
    std::printf("lifecycle check passed: %d constructed and released, accounted heap back to baseline\n", kCount);
    return 0;
}
