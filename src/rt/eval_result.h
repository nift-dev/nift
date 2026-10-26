#pragma once
// CLI-only value presentation. Function tags are output strings, never a VM
// type encoding or a callable/resource bridge into the legacy evaluator.
#include "rt/legacy_bridge.h"

namespace nift::rt {
bool eval_result_to_legacy(const Value& value, nift::RuntimeValue& out, std::string& error);
} // namespace nift::rt
