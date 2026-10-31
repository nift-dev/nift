#pragma once
// CP16 VM host built-ins: environment and platform information.
#include "rt/natives/host_ops.h"

#include <string>

namespace nift::rt::natives {

// The value of platform(): the invocation's --platform selection, `native`
// unless the CLI sets another before any VM code runs (read-only afterwards,
// so isolate workers may read it). Legacy's ScriptRenderHost reports the same.
void set_script_platform(std::string platform);
const std::string& script_platform();

} // namespace nift::rt::natives
