#pragma once
// Source-thread snapshot validation and bounded parallel task submission.
#include <memory>
#include <string>

namespace nift::rt {
struct ParallelTaskRecord;
// C140 (a): the stable `limit.exceeded` text when no worker thread can be created (known-limitations.md).
inline constexpr const char* kParallelResourcesExhausted = "parallel task resources exhausted";
struct VmResult;
// C140: records a task's budget or thread-creation failure with its runtime (ParallelRuntime::note_limit), so that
// the run ends with it even when no one awaits the task.
void note_parallel_limit(const ParallelTaskRecord& record, const VmResult& result);
#if defined(NIFT_ASYNC_MEMORY_MUTANT) && NIFT_ASYNC_MEMORY_MUTANT == 3  // test-only: the old Internal mapping
inline constexpr bool kThreadFailureInternal = true;
#else
inline constexpr bool kThreadFailureInternal = false;
#endif
// Snapshots source Values and queues a task. False with `error` when the
// limit is exhausted or the OS thread cannot be created.
bool start_parallel_task(std::shared_ptr<ParallelTaskRecord> record, std::string& error);
} // namespace nift::rt
