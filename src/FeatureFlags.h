#pragma once
// Runtime 2.0 CP05: diagnostic NIFT_P4_* switches were looked up with getenv()
// on every user function call (~9% of a flat-call benchmark). They are now
// cached and re-read only after the environment may have changed: Nift's own
// writers (the setenv/unsetenv builtins, build hooks, the CLI) and every Parser
// construction (so a host changing the environment between Engine renders is
// seen) call note_environment_changed(). Review finding: a process-lifetime
// latch ignored a script's setenv("NIFT_P4_TRACE", ...) mid-run.
#include <atomic>
#include <cstdint>
#include <cstdlib>

namespace nift::flags {
inline std::atomic<std::uint64_t>& environment_generation() { static std::atomic<std::uint64_t> g{1}; return g; }
inline void note_environment_changed() { environment_generation().fetch_add(1, std::memory_order_relaxed); }
// Slot holds (generation << 1) | value; a stale generation forces a re-read.
inline bool cached_flag(const char* name, std::atomic<std::uint64_t>& slot) {
    const std::uint64_t generation = environment_generation().load(std::memory_order_relaxed);
    const std::uint64_t cached = slot.load(std::memory_order_relaxed);
    if ((cached >> 1) == generation) return (cached & 1u) != 0;
    const bool value = std::getenv(name) != nullptr;
    slot.store((generation << 1) | (value ? 1u : 0u), std::memory_order_relaxed);
    return value;
}
inline bool p4_disable() { static std::atomic<std::uint64_t> s{0}; return cached_flag("NIFT_P4_DISABLE", s); }
inline bool p4_trace()   { static std::atomic<std::uint64_t> s{0}; return cached_flag("NIFT_P4_TRACE", s); }
inline bool p4_shape()   { static std::atomic<std::uint64_t> s{0}; return cached_flag("NIFT_P4_SHAPE", s); }
inline bool p4_bridge()  { static std::atomic<std::uint64_t> s{0}; return cached_flag("NIFT_P4_BRIDGE", s); }
} // namespace nift::flags
