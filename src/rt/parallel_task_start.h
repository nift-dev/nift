#pragma once
// Source-thread snapshot validation and bounded parallel task submission.
#include <memory>
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT == 11 || \
                                          NIFT_ASYNC_MEMORY_MUTANT == 12)
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <new>
#include <system_error>
#endif
#include <string>

namespace nift::rt {
struct ParallelTaskRecord;
// C140 (a): the stable `limit.exceeded` text when no worker thread can be created (known-limitations.md).
inline constexpr const char* kParallelResourcesExhausted = "parallel task resources exhausted";
struct VmResult;
// C140: records a task's budget or thread-creation failure with its runtime (ParallelRuntime::note_limit), so that
// the run ends with it even when no one awaits the task.
void note_parallel_limit(const ParallelTaskRecord& record, const VmResult& result);
// C140a/C140b, test-only. Mutant 11 keeps the old Internal mapping of a bad_alloc creating a task; builds 9, 11 and 12
// fail the n-th pass through a site, NIFT_TEST_PARALLEL_BAD_ALLOC="<site>:n": std::bad_alloc at "start" or
// "submit" (task creation), std::system_error (no thread) at "wait", "timer" or "cleanup" (a worker's ParallelBlock of
// that kind: Blocking, Timer, Cleanup).
// Absent from production builds (the build guard refuses *MUTANT* defines on production objects).
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT == 11 || \
                                          NIFT_ASYNC_MEMORY_MUTANT == 12)
// The variable holds one or more comma-separated "<site>:n" items, each counted separately ("wait:1,cleanup:1").
inline void inject_bad_alloc(const char* site) {
    static const char* const sites[] = {"start", "submit", "wait", "timer", "cleanup"};
    static std::atomic<int> counts[5];
    const char* spec = std::getenv("NIFT_TEST_PARALLEL_BAD_ALLOC");
    const std::size_t n = std::strlen(site);
    int index = 0;
    while (index < 5 && std::strcmp(sites[index], site) != 0) ++index;  // rt-lint: allow-string-compare test-only
    if (!spec || index == 5) return;
    for (const char* at = std::strstr(spec, site); at; at = std::strstr(at + 1, site)) {
        if ((at != spec && at[-1] != ',') || at[n] != ':') continue;
        if (++counts[index] != std::atoi(at + n + 1)) return;
        if (site[0] == 's') throw std::bad_alloc();
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
    }
}
#define NIFT_INJECT_BAD_ALLOC(site) ::nift::rt::inject_bad_alloc(site)
#define NIFT_INJECT_WAIT(k) ::nift::rt::inject_bad_alloc((k) == ParallelWaitKind::Timer ? "timer" \
                                                     : (k) == ParallelWaitKind::Cleanup ? "cleanup" : "wait")
#else
#define NIFT_INJECT_BAD_ALLOC(site) static_cast<void>(0)
#define NIFT_INJECT_WAIT(k) static_cast<void>(0)
#endif
inline constexpr bool kAllocFailureInternal =
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT == 11
    true;
#else
    false;
#endif
// Releases of a task's charge at its completion: exactly one (test-only mutants: 2 keeps it, 10 releases it twice).
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT == 2
inline constexpr int kCompletionReleases = 0;
#elif defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT == 10
inline constexpr int kCompletionReleases = 2;
#else
inline constexpr int kCompletionReleases = 1;
#endif
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT == 3  // test-only: the old Internal mapping
inline constexpr bool kThreadFailureInternal = true;
#else
inline constexpr bool kThreadFailureInternal = false;
#endif
// Snapshots source Values and queues a task. False with `error` when the
// limit is exhausted or the OS thread cannot be created.
bool start_parallel_task(std::shared_ptr<ParallelTaskRecord> record, std::string& error);
} // namespace nift::rt
