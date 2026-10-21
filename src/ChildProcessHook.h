#pragma once
// Windows build-lock lifetime (see ProjectOwnership.cpp): the legacy process
// launcher reports every child it starts, so an owning build can record it.
// An inline variable keeps Process.cpp free of a link dependency on
// ProjectOwnership.cpp (the process unit tests link Process.cpp alone).
// POSIX never sets or calls it: an inherited flock already keeps the lock.
namespace nift {
inline void (*child_process_spawned)(unsigned long pid, unsigned long long creation_time) = nullptr;
}
