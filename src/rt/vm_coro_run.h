#pragma once
// CP22 coroutine run control: the op_coro dispatcher, futures' methods,
// async calls through values, and the run's end (uncaught stops, main's end,
// the drain and the teardown of abandoned tasks).
#include "rt/vm_coro.h"

#include <string>

namespace nift::rt {

// `,"coro_spawned":n,...` for NIFT_VM_STATS (a VmResult of a coroutine program).
std::string coro_stats_json(const VmResult& result);

}  // namespace nift::rt
