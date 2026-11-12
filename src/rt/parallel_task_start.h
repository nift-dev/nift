#pragma once
// Source-thread snapshot validation and bounded parallel task submission.
#include <memory>
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT >= 11 && NIFT_ASYNC_MEMORY_MUTANT <= 12
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
// C140a F3/F8: test-only. Mutant 11 keeps the old Internal mapping of a bad_alloc creating a task; builds 11 and 12
// fail the n-th pass through a site, NIFT_TEST_PARALLEL_BAD_ALLOC="<site>:n": std::bad_alloc at "start" or
// "submit" (task creation), std::system_error (no thread) at "timer" or "cleanup" (a ParallelBlock of that kind).
// Absent from production builds (the build guard refuses *MUTANT* defines on production objects).
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT >= 11 && NIFT_ASYNC_MEMORY_MUTANT <= 12
inline void inject_bad_alloc(const char* site) {
    static std::atomic<int> count{0};
    const char* spec = std::getenv("NIFT_TEST_PARALLEL_BAD_ALLOC");
    const std::size_t n = std::strlen(site);
    const bool here = spec && std::strncmp(spec, site, n) == 0;  // rt-lint: allow-string-compare test-only
    if (!here || spec[n] != ':' || ++count != std::atoi(spec + n + 1)) return;
    if (site[0] == 's') throw std::bad_alloc();
    throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
}
#define NIFT_INJECT_BAD_ALLOC(site) ::nift::rt::inject_bad_alloc(site)
#define NIFT_INJECT_WAIT(k) ::nift::rt::inject_bad_alloc((k) == ParallelWaitKind::Timer ? "timer" \
                                                     : (k) == ParallelWaitKind::Cleanup ? "cleanup" : "-")
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
