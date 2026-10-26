#pragma once
// Common process-level VM options for script, eval and session entry points.
#include "rt/vm.h"
namespace nift::rt::detail {
void limits_from_environment(VmOptions& options);
}
