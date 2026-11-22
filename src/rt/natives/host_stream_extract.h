#pragma once
// Internal native for the compiler's typed stream-extraction statement.
#include "rt/natives/host_ops.h"

namespace nift::rt::natives {

Status stream_extract_string(const Context&, const Value* args, std::uint32_t argc, Value& out, std::string& error);

} // namespace nift::rt::natives
