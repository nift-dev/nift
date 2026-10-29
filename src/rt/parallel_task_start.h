#pragma once
// Source-thread snapshot validation and bounded parallel task submission.
#include <memory>
#include <string>

namespace nift::rt {
struct ParallelTaskRecord;
// Snapshots source Values and queues a task. False with `error` when the
// limit is exhausted or the OS thread cannot be created.
bool start_parallel_task(std::shared_ptr<ParallelTaskRecord> record, std::string& error);
} // namespace nift::rt
