#pragma once
// CP16 Host layer: process launches through HostCall (trace v1/v2 per stage).
//
// v1 (legacy, CP08): one `spawn` line per stage BEFORE launching any stage,
// arg = program and arguments joined by single spaces, res = cwd as spelled.
// v2: one `spawn` line per stage AFTER the attempt: res "launched" or
// "launch_failed" (a value, never an error), argv array, absolute cwd; a
// request refused before launching (detach in a build, ...) is one `spawn`
// line with res "error" and the code, and no v1 line.
#include "rt/host/host_call.h"
#include "rt/host/process.h"

namespace nift::rt::host {

std::shared_ptr<ProcessGroup> start_traced(const HostCallEnv& env, SpawnRequest request, HostStatus& status, HostError& error);

} // namespace nift::rt::host
