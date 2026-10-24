// CP13B R1-3: both Error factories must be exception-safe under allocation failure.
#include "rt/error.h"
#include "rt/heap.h"
#include "rt/heap_account.h"
#include "rt/host/host_error.h"
#include "rt/natives/builtin.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
static int countdown = -1;
static long allocations = 0;
void* operator new(std::size_t n) {
    if (countdown == 0) { countdown = -1; throw std::bad_alloc(); }
    if (countdown > 0) --countdown;
    if (void* p = std::malloc(n)) { ++allocations; return p; }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { if (p) { --allocations; std::free(p); } }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }

namespace nift::rt::host { bool catchable(std::string_view) { return true; } }
namespace nift::rt::natives { bool is_recoverable_code(std::string_view) { return false; } }

int main() {
    using namespace nift::rt;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) { ++failures; std::fprintf(stderr, "CHECK failed: %s\n", what); }
    };
    const std::size_t baseline = live_heap_bytes();
    Value cause;
    std::string why;
    check(make_error(std::string(64, 'c'), "user.test.cause", Value(), {}, 0, 0, cause, why),
          "cause construction");
    Value original;
    check(make_error(std::string(1024, 'm'), "user.test", cause, {}, 0, 0, original, why),
          "original construction");
    const std::size_t after_objects = live_heap_bytes();
    const SourceOrigin origin{std::string(1024, 'p'), 3, 1};

    const long before_clone = allocations;
    countdown = 1;  // ErrorObject allocation succeeds, the next string copy fails
    bool clone_threw = false;
    try { (void)clone_error_with_origin(original, origin); }
    catch (const std::bad_alloc&) { clone_threw = true; }
    countdown = -1;
    check(clone_threw, "clone allocation failure injected");
    check(allocations == before_clone, "clone leaves no allocation");
    check(live_heap_bytes() == after_objects, "clone leaves no accounting imbalance");

    const long before_system = allocations;
    std::string message(1024, 'f'), code = "io.open_failed";
    countdown = 1;
    bool system_threw = false;
    try { Value out; make_system_error(std::move(code), std::move(message), origin, out, why); }
    catch (const std::bad_alloc&) { system_threw = true; }
    countdown = -1;
    check(system_threw, "system Error allocation failure injected");
    check(allocations == before_system, "system Error leaves no allocation");
    check(live_heap_bytes() == after_objects, "system Error leaves no accounting imbalance");

    // A subsequent successful construction still works and is accounted.
    Value clone = clone_error_with_origin(original, origin);
    check(clone.is(Kind::Error) && clone.heap_as<ErrorObject>()->line == 3,
          "clone succeeds after failure");
    check(clone.heap_as<ErrorObject>()->cause.is(Kind::Error), "clone keeps the cause");
    Value system;
    check(make_system_error("io.open_failed", "failed", origin, system, why),
          "system Error succeeds after failure");
    check(system.is(Kind::Error) && system.heap_as<ErrorObject>()->code == "io.open_failed",
          "system Error has the requested code");
    check(live_heap_bytes() > baseline, "successful Errors are accounted");
    clone = Value();
    system = Value();
    original = Value();
    cause = Value();
    check(live_heap_bytes() == baseline, "all Error allocations released");
    std::printf("error_oom_unit: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
