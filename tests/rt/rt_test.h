#pragma once
// Minimal check harness for src/rt tests: CHECK counts and reports failures
// without aborting, so one run lists every broken expectation.
#include <cstdio>
#include <cstdlib>

namespace rt_test {
inline long& checks() { static long n = 0; return n; }
inline long& failures() { static long n = 0; return n; }
inline int finish(const char* suite) {
    std::printf("%s: %ld checks, %ld failures\n", suite, checks(), failures());
    return failures() == 0 ? 0 : 1;
}
} // namespace rt_test

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++rt_test::checks();                                                              \
        if (!(cond)) {                                                                    \
            ++rt_test::failures();                                                        \
            if (rt_test::failures() <= 50)                                                \
                std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                                 \
    } while (0)
