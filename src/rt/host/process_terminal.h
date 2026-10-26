#pragma once
// CP21: one process-wide controlling-terminal ownership lease for Host groups.
// SIGTTOU disposition and the terminal's foreground process group are process
// state, not per-group state, so every handoff and restoration goes through
// this coordinator.
#ifndef _WIN32
#include <cstdint>

namespace nift::rt::host::posix {

// Takes the lease and saves the process's original SIGTTOU disposition, then
// installs SIG_IGN. False when another owner holds the lease or the signal
// disposition cannot be changed; no state is changed on failure.
bool terminal_acquire(const void* owner);
// Returns the terminal to the shell and restores the saved SIGTTOU disposition.
// Only the current owner can release; false leaves ownership unchanged or
// reports a failed tcsetpgrp/sigaction after clearing the lease.
bool terminal_release(const void* owner);
bool terminal_owned_by(const void* owner);

} // namespace nift::rt::host::posix
#endif  // !_WIN32
