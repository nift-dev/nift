#pragma once
// CP16 Host layer, POSIX internals: driving a group over time -- the event
// loop, leftover (C1) and detached cleanup steps, settling, host-loop
// arming, and the lifecycle events reported to CP17 (process.h seam).
#ifndef _WIN32
#include "rt/host/process_posix_state.h"

#include <functional>
#include <mutex>
#include <optional>

namespace nift::rt::host::posix {

bool leftover_step(State& s);
bool detached_step(State& s);
bool group_settled(const State& s);
bool run_loop(State& s, const void* key, std::unique_lock<std::mutex>& lk, std::optional<Clock::time_point> until,
              const std::function<bool()>& stop);
void rearm(State& s);
OsOutcome outcome_of(const State& s);
std::function<void()> collect_lifecycle(State& s);

// Lock for a public operation; re-arms the host loop and delivers lifecycle
// events when it is released.
struct OpLock {
    State& s;
    std::unique_lock<std::mutex> lk;
    explicit OpLock(State& state) : s(state), lk(state.mu) {}
    ~OpLock() {
        if (!lk.owns_lock()) return;
        rearm(s);
        auto deliver = collect_lifecycle(s);
        lk.unlock();
        if (deliver) deliver();
    }
};

} // namespace nift::rt::host::posix
#endif  // !_WIN32
