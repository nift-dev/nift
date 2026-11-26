#pragma once
// Synchronous argv processes using the platform Host launchers.

#include "rt/natives/builtin.h"

namespace nift::rt::natives {
Status process_check(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status process_argument(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status process_spread(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status process_run(const Context&, const Value*, std::uint32_t, Value&, std::string&);
Status process_run_stdout(const Context&, const Value*, std::uint32_t, Value&, std::string&);
} // namespace nift::rt::natives
