#pragma once
// CP16 Host layer: C2 (Nift receives SIGINT/SIGTERM: managed children are
// terminated, then Nift ends with the signal), the Linux subreaper and the
// orphan sweep for root-context ends (design §11.4, §11.9).
#include "rt/host/process.h"

#include <cstddef>
#include <functional>

namespace nift::rt::host::runtime {

// ---- C2: Nift receives SIGINT / SIGTERM ----
struct SignalCleanupOptions {
    Millis kill_after{2000};
    Millis post_kill_wait{5000};
    // Test hook: called instead of re-raising the signal (the default ends
    // Nift with the signal's default action, so the exit status is 130/143).
    std::function<void(int)> on_done;
};
bool install_signal_cleanup(SignalCleanupOptions options = SignalCleanupOptions());
void uninstall_signal_cleanup();
// True once SIGINT/SIGTERM arrived with the handler installed: Nift is
// shutting down. From then on no new managed process is launched
// (`host.shutting_down`) and HostCall refuses every externally visible op
// (writes, spawns); the build engine checks the same flag before starting a
// build or a page render (wiring later). Running code may continue to its
// shutdown path while the watcher terminates the managed children (SIGTERM,
// grace, SIGKILL, bounded wait); then Nift ends with the signal. If the
// program exits first, an atexit handler waits for the watcher (bounded) and
// re-raises, so the exit status is always signal-derived. This is a bounded
// normal-shutdown guarantee only: SIGKILL or a crash runs none of it.
bool termination_requested();
bool shutting_down();
void request_shutdown_for_test(int signo);  // marks shutting down without a signal (tests)
void clear_shutdown_for_test();

// ---- subreaper and orphan sweep (Linux) ----
bool enable_subreaper();   // false where unavailable
bool subreaper_enabled();
// Terminates and reaps processes reparented to Nift that belong to no
// indexed group and to no detached session (sid), excluding pids for which
// `foreign` returns true (children launched by other code, e.g. legacy job
// control). Call at the end of a root context (script, build, --auto pass).
// Returns the number of orphans terminated.
std::size_t sweep_orphans(Millis kill_after, Millis post_kill_wait,
                          const std::function<bool(long pid)>& foreign = nullptr);

} // namespace nift::rt::host::runtime
