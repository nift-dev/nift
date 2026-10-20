#pragma once
// CP16 Host layer: the host loop -- one lazily started thread with monotonic
// timers and one-shot descriptor watches (design §11.3; CP22 scheduler candidate).
#include "rt/host/process.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace nift::rt::host::runtime {

// ---- host loop (timers and one-shot fd watches) ----
// One lazily started thread that sleeps in poll() until the nearest timer or
// a watched descriptor becomes readable -- no periodic polling. Callbacks run
// on the loop thread without the loop's lock held. Generic on purpose: CP22's
// scheduler can take this over (or host it) without changing its users.
using TaskId = std::uint64_t;
TaskId loop_at(Clock::time_point when, std::function<void()> fn);  // one-shot timer (monotonic clock)
TaskId loop_when_readable(int fd, std::function<void()> fn);      // one-shot; the loop owns `fd` and closes it
void loop_cancel(TaskId id);                                       // no-op for fired/unknown ids
std::size_t loop_pending();
std::size_t loop_fd_count();  // descriptors the loop owns (wake pipe + fd watches), for leak checks
// Async-signal-safe: SIGCHLD arrived (adopted orphans may be reapable); wakes the loop.
void loop_notify_sigchld();

} // namespace nift::rt::host::runtime
