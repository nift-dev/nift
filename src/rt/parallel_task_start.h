#pragma once
// Source-thread snapshot validation and bounded parallel task submission.
#include <memory>
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <new>
#include <system_error>
#endif
#include <string>

namespace nift::rt {
struct ParallelTaskRecord;
// C140 (a): the stable `limit.exceeded` text when no worker thread can be created (known-limitations.md).
inline constexpr const char* kParallelResourcesExhausted = "parallel task resources exhausted";
inline constexpr const char* kParallelBudgetExceeded = "parallel task memory budget exceeded";  // C140 (b)
struct VmResult;
// C140: records a task's budget or thread-creation failure with its runtime (ParallelRuntime::note_limit), so that
// the run ends with it even when no one awaits the task.
void note_parallel_limit(const ParallelTaskRecord& record, const VmResult& result);
// C140a/C140b, test-only. Mutant 11 keeps the old Internal mapping of a bad_alloc creating a task; builds 9, 11 and 12
// fail the n-th pass through a site, NIFT_TEST_PARALLEL_BAD_ALLOC="<site>:n": std::bad_alloc at "start" or
// "submit" (task creation), std::system_error (no thread) at "wait", "timer" or "cleanup" (a worker's ParallelBlock of
// that kind: Blocking, Timer, Cleanup); C141: "nothread:n" fails every worker-thread start from the n-th on (sticky).
// Builds 13-18 (C141 mutants) keep the hook too. Absent from production builds (the build guard refuses *MUTANT*
// defines on production objects).
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && (NIFT_ASYNC_MEMORY_MUTANT == 9 || NIFT_ASYNC_MEMORY_MUTANT >= 11)
// The variable holds one or more comma-separated "<site>:n" items, each counted separately ("wait:1,cleanup:1").
inline void inject_bad_alloc(const char* site) {
    static const char* const sites[] = {"start", "submit", "wait", "timer", "cleanup", "nothread"};
    static std::atomic<int> counts[6];
    const char* spec = std::getenv("NIFT_TEST_PARALLEL_BAD_ALLOC");
    const std::size_t n = std::strlen(site);
    int index = 0;
    while (index < 6 && std::strcmp(sites[index], site) != 0) ++index;  // rt-lint: allow-string-compare test-only
    if (!spec || index == 6) return;
    for (const char* at = std::strstr(spec, site); at; at = std::strstr(at + 1, site)) {
        if ((at != spec && at[-1] != ',') || at[n] != ':') continue;
        const int count = ++counts[index], nth = std::atoi(at + n + 1);
        if (index == 5 ? count < nth : count != nth) return;  // "nothread" stays failed from the n-th start on
        if (site[0] == 's') throw std::bad_alloc();
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
    }
}
// C141a (M17), test-only: polls `ready` every millisecond, for at most 10 s (vm_parallel_cleanup.cpp's hook).
template<class Ready> inline void wait_for_test(Ready ready) {
    for (int i = 0; i < 10000 && !ready(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
#define NIFT_INJECT_BAD_ALLOC(site) ::nift::rt::inject_bad_alloc(site)
// C141: a charge still reserved when the runtime is destroyed (a leak) aborts the fault-injection builds.
#define NIFT_CHECK_DRAINED(reserved) ((reserved) != 0 ? std::abort() : static_cast<void>(0))
#define NIFT_INJECT_WAIT(k) ::nift::rt::inject_bad_alloc((k) == ParallelWaitKind::Timer ? "timer" \
                                                     : (k) == ParallelWaitKind::Cleanup ? "cleanup" : "wait")
#else
#define NIFT_INJECT_BAD_ALLOC(site) static_cast<void>(0)
#define NIFT_CHECK_DRAINED(reserved) static_cast<void>(0)
#define NIFT_INJECT_WAIT(k) static_cast<void>(0)
#endif
// C141 test-only switches (mutants): 13 restores the cleanup-wait rethrow (std::terminate), 14 removes the sweep of
// queued work, 15 removes the refusal of new work once starved, 16 marks swept tasks "cancelled", 17 drops the
// starving block's notify (M1; C141a), 18 lets a shared wait block after a fatal limit (D-085), 19 sweeps in
// starve()'s own lock hold, after block_worker's (f448c7b8's order: the W1 window), 20 drops the last resumer's
// wake-up of the idle workers (S1), 21 admits new tasks after a noted (C140 (d)) limit, refusing only once starved
// (N1).
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT >= 13
inline constexpr int kStarveMutant = NIFT_ASYNC_MEMORY_MUTANT;
#else
inline constexpr int kStarveMutant = 0;
#endif
inline constexpr bool kCleanupRethrow = kStarveMutant == 13, kStarveSweeps = kStarveMutant != 14;
inline constexpr bool kStarveAdmits = kStarveMutant == 15, kStarveCancels = kStarveMutant == 16;
inline constexpr bool kStarveNotifies = kStarveMutant != 17, kFatalWaits = kStarveMutant != 18;
inline constexpr bool kSweepOutsideLock = kStarveMutant == 19;  // M19: the sweep in starve()'s own lock hold
inline constexpr bool kResumeWakes = kStarveMutant != 20, kNoteAdmits = kStarveMutant == 21;  // M20, M21
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
