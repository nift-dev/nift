#pragma once
// Source-thread metadata/constant snapshot and destination atom operand remapping.
#include "rt/isolate_task.h"
namespace nift::rt {
bool remap_isolate_atoms(Program& clone, const IsolateTaskInput& input, std::string& error);
}
