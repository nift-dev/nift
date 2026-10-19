#pragma once
// CP16 Host layer: from run()'s validated options (run_options.h) to a
// SpawnRequest (process.h). This is where the D-034 stdin forms meet the one
// stdin mechanism of the process layer:
//
//   omitted / "inherit" -> StdinMode::Inherit (today's behaviour)
//   "null"              -> StdinMode::Null (/dev/null)
//   {"text": s}         -> StdinMode::Text: written by the event loop while
//                          stdout/stderr are drained (no deadlock); an early
//                          exit or broken pipe keeps the process result
//   {"file": p}         -> read by the Host through HostCall (op `read`):
//                          normal OS path semantics, the --fs-root policy,
//                          dependency recording in builds, the read limit;
//                          then fed exactly like {"text"} -- the child gets
//                          the bytes that were recorded
//
// Redirections of the command language (CP19/CP20 `< file`) and
// cmd().stdin(path) keep using SpawnStage::stdin_path (the child opens the
// file): the same StdinMode slot, not a competing mechanism.
#include "rt/host/host_call.h"
#include "rt/host/process.h"
#include "rt/host/run_options.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nift::rt::host {

OutputMode output_mode_for(RunMode mode);

struct RunRequestContext {
    bool in_build = false;                                // CP17: HandleTable::context_in_build()
    std::uint64_t read_limit = std::uint64_t(1) << 30;    // NIFT_READ_LIMIT value (host-deps ReadLimit.h, later)
    std::function<void()> flush_output;                   // stream mode
    std::string builtin = "run";
};

// Fills `out` (stages[0] = argv). Returns Ok, or the failure of the
// context check (process.detach_in_build) or of the stdin file read
// (io.open_failed, catchable; limit.exceeded; host policy refusals).
HostStatus build_spawn_request(const HostCallEnv& env, const RunOptions& options, const std::vector<std::string>& argv,
                               const RunRequestContext& context, SpawnRequest& out, HostError& error);

// The {"file": path} read on its own (exposed for tests).
HostStatus read_stdin_file(const HostCallEnv& env, const std::string& raw, const RunRequestContext& context,
                           std::string& bytes, HostError& error, std::string* open_path = nullptr);

} // namespace nift::rt::host
