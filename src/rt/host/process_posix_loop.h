#pragma once
// CP16 Host layer, POSIX internals: one group's state transitions without
// blocking -- signalling (pipeline, child, tree), cancellation start, reaping,
// draining and stdin writing, capture limits, and finishing the result.
#ifndef _WIN32
#include "rt/host/process_posix_state.h"

#include <cstdint>
#include <string>

namespace nift::rt::host::posix {

bool kill_suppressed_for_test();
// Live (non-zombie) members in the group (Linux: Nift's session only).
bool group_has_members(const State& s);
void send_signal(State& s, int sig);
void close_stdin_end(State& s);
void begin_cancel(State& s, CancelScope scope, CancelReason reason, Millis kill_after, std::size_t child_stage = 0);
void record_exit(StageRt& stage, int status);
void reap_some(State& s, const void* key);
void drain(State& s, int& fd, Buffer& b, bool is_out);
void write_stdin(State& s);
void check_file_limits(State& s);
bool read_file_bounded(int fd, std::uint64_t limit, std::string& out);
bool restore_terminal(State& s);
void finish(State& s, const void* key);
// One pass of state transitions (no blocking). Returns finished.
bool step(State& s, const void* key);

} // namespace nift::rt::host::posix
#endif  // !_WIN32
