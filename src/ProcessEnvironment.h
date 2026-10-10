#pragma once
#include <mutex>
// Protect POSIX environ snapshots from Nift-owned environment writers. Host
// applications/FFI must externally serialize their own setenv/unsetenv calls.
inline std::mutex& nift_environment_mutex(){static std::mutex mutex;return mutex;}
