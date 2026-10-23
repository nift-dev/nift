#pragma once
// CP16 Host layer, POSIX internals: launching a pipeline -- parent-side
// endpoints, exec preparation, one fork per stage (child setup between fork
// and exec, async-signal-safe), and the parent's record of each stage.
#ifndef _WIN32
#include "rt/host/process_posix_state.h"
#include "rt/host/process_posix_sys.h"

#include <string>
#include <sys/types.h>
#include <vector>

namespace nift::rt::host::posix {

// One launch in progress (ProcessGroup::start_impl).
struct Launch {
    State& s;
    const void* key = nullptr;
    bool detached = false;
    OutputMode mode = OutputMode::FileCapture;
    int out_child = -1, err_child = -1, in_child = -1;  // child ends (closed in the parent after forking)
    bool stdin_pipe = false;
    bool null_stdin = false;
    bool want_handoff = false;
    std::vector<PreparedExec> prepared;
    pid_t parent_pid = -1;
    int prev_read = -1;
    std::vector<int> reports;  // deferred exec-report read ends (H2)
    explicit Launch(State& state) : s(state) {}
    void fail(std::string message) {
        s.launch_failed = true;
        s.parent_error = std::move(message);
    }
};

// Capture files or pipes, the stdin pipe, and the terminal-handoff decision.
void open_endpoints(Launch& l);
// Environment snapshot, exec preparation per stage, index entry, SIGTTOU.
void prepare_stages(Launch& l);
// Pipes, fork, child setup and the parent's bookkeeping for stage i. False:
// a parent-side failure (recorded in the state); stop launching.
bool launch_stage(Launch& l, std::size_t i);
// Closes the parent's copies of the child ends.
void close_child_ends(Launch& l);

} // namespace nift::rt::host::posix
#endif  // !_WIN32
